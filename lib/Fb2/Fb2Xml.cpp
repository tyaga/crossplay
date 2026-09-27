#include "Fb2Xml.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "Fb2Encoding.h"

namespace fb2 {

namespace {

constexpr size_t READ_CHUNK = 4096;

// The encoding named by the XML declaration at the head of the file, or empty.
std::string declaredEncoding(const char* head, const size_t len) {
  const std::string text(head, len);
  const size_t declEnd = text.find("?>");
  // A UTF-8 byte-order mark may sit in front of the declaration.
  const size_t declStart = text.find("<?xml");
  if (declStart == std::string::npos || declStart > 3) return {};
  const size_t at = text.find("encoding", declStart);
  if (at == std::string::npos || (declEnd != std::string::npos && at > declEnd)) return {};
  const size_t quote = text.find_first_of("\"'", at);
  if (quote == std::string::npos) return {};
  const size_t close = text.find(text[quote], quote + 1);
  if (close == std::string::npos) return {};
  return text.substr(quote + 1, close - quote - 1);
}

bool validUtf8(const uint8_t* data, const size_t len) {
  size_t i = 0;
  while (i < len) {
    const uint8_t b = data[i];
    int follow = 0;
    if (b < 0x80) {
      i++;
      continue;
    }
    if ((b & 0xE0) == 0xC0 && b >= 0xC2) {
      follow = 1;
    } else if ((b & 0xF0) == 0xE0) {
      follow = 2;
    } else if ((b & 0xF8) == 0xF0 && b <= 0xF4) {
      follow = 3;
    } else {
      return false;
    }
    // A sequence cut by the end of the sample is not evidence either way.
    if (i + follow >= len) return true;
    for (int k = 1; k <= follow; k++) {
      if ((data[i + k] & 0xC0) != 0x80) return false;
    }
    i += 1 + follow;
  }
  return true;
}

// Both encodings keep Cyrillic in 0xC0-0xFF with the cases swapped, and running
// text is mostly lower case: Windows-1251 puts that in 0xE0-0xFF, KOI8-R in
// 0xC0-0xDF.
const char* guessSingleByte(const uint8_t* data, const size_t len) {
  int upperHalf = 0;
  int lowerHalf = 0;
  for (size_t i = 0; i < len; i++) {
    if (data[i] >= 0xE0) {
      upperHalf++;
    } else if (data[i] >= 0xC0) {
      lowerHalf++;
    }
  }
  if (upperHalf == 0 && lowerHalf == 0) return "windows-1252";
  return upperHalf >= lowerHalf ? "windows-1251" : "koi8-r";
}

}  // namespace

const char* localName(const char* name) {
  const char* colon = strrchr(name, ':');
  return colon ? colon + 1 : name;
}

const char* attribute(const XML_Char** atts, const char* wanted) {
  for (int i = 0; atts && atts[i]; i += 2) {
    if (strcmp(localName(atts[i]), wanted) == 0) return atts[i + 1];
  }
  return nullptr;
}

uint32_t idHash(const char* id) {
  uint32_t hash = 2166136261u;
  for (const char* p = id; *p; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return hash;
}

XML_Parser createParser(const uint8_t* head, const size_t len, const XmlHandlers& handlers) {
  const std::string declared = declaredEncoding(reinterpret_cast<const char*>(head), len);
  const char* forced = nullptr;
  if (declared.empty()) forced = validUtf8(head, len) ? "UTF-8" : guessSingleByte(head, len);
  XML_Parser parser = XML_ParserCreate(forced);
  if (!parser) {
    LOG_ERR("FB2", "OOM: XML parser");
    return nullptr;
  }
  XML_SetUnknownEncodingHandler(parser, unknownEncodingHandler, nullptr);
  XML_SetUserData(parser, handlers.userData);
  XML_SetElementHandler(parser, handlers.start, handlers.end);
  XML_SetCharacterDataHandler(parser, handlers.text);
  return parser;
}

XmlResult parseFile(const std::string& path, const XmlHandlers& handlers, XML_Parser& parserOut, ProgressFn progress,
                    void* progressCtx, const int progressFrom, const int progressTo) {
  HalFile file;
  if (!Storage.openFileForRead("FB2", path, file)) {
    LOG_ERR("FB2", "Cannot open %s", path.c_str());
    return XmlResult::Broken;
  }
  const size_t total = file.fileSize();
  auto buffer = makeUniqueNoThrow<uint8_t[]>(READ_CHUNK);
  if (!buffer) {
    LOG_ERR("FB2", "OOM: %u-byte read buffer", static_cast<unsigned>(READ_CHUNK));
    return XmlResult::Broken;
  }

  int got = file.read(buffer.get(), READ_CHUNK);
  if (got <= 0) return XmlResult::Broken;
  XML_Parser parser = createParser(buffer.get(), static_cast<size_t>(got), handlers);
  if (!parser) return XmlResult::Broken;
  parserOut = parser;

  XmlResult result = XmlResult::Complete;
  size_t consumed = 0;
  int lastPercent = -1;
  while (true) {
    consumed += static_cast<size_t>(got);
    const bool last = got <= 0 || consumed >= total;
    const XML_Status status =
        XML_Parse(parser, reinterpret_cast<const char*>(buffer.get()), got > 0 ? got : 0, last ? XML_TRUE : XML_FALSE);
    if (status == XML_STATUS_SUSPENDED ||
        (status == XML_STATUS_ERROR && XML_GetErrorCode(parser) == XML_ERROR_ABORTED)) {
      result = XmlResult::Stopped;
      break;
    }
    if (status == XML_STATUS_ERROR) {
      LOG_ERR("FB2", "%s: %s at line %lu", path.c_str(), XML_ErrorString(XML_GetErrorCode(parser)),
              static_cast<unsigned long>(XML_GetCurrentLineNumber(parser)));
      result = XmlResult::Broken;
      break;
    }
    if (progress && total > 0) {
      const int percent =
          progressFrom + static_cast<int>((static_cast<uint64_t>(consumed) * (progressTo - progressFrom)) / total);
      if (percent != lastPercent) {
        lastPercent = percent;
        progress(progressCtx, percent);
      }
    }
    if (last) break;
    // A book takes seconds; let the other tasks and the watchdog run.
    if ((consumed / READ_CHUNK) % 8 == 0) delay(1);
    got = file.read(buffer.get(), READ_CHUNK);
    if (got < 0) {
      result = XmlResult::Broken;
      break;
    }
  }
  parserOut = nullptr;
  XML_ParserFree(parser);
  return result;
}

}  // namespace fb2
