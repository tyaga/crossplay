#pragma once

#include <expat.h>

#include <cstdint>
#include <string>

#include "Fb2Converter.h"

namespace fb2 {

// FB2 element and attribute names without their namespace prefix: files use
// "l:href", "xlink:href" and occasionally "fb:section" for the same things.
const char* localName(const char* name);
// The value of the first attribute whose local name is `wanted`, or nullptr.
const char* attribute(const XML_Char** atts, const char* wanted);

struct XmlHandlers {
  void* userData = nullptr;
  XML_StartElementHandler start = nullptr;
  XML_EndElementHandler end = nullptr;
  XML_CharacterDataHandler text = nullptr;
};

enum class XmlResult : uint8_t {
  Complete,  // parsed to the end of the file
  Stopped,   // a handler called XML_StopParser
  Broken,    // malformed XML, or the file could not be read; what came before stands
};

// A parser for a document whose first bytes are `head`, with the encoding
// sniffed as parseFile() describes. nullptr on OOM.
XML_Parser createParser(const uint8_t* head, size_t len, const XmlHandlers& handlers);

// Runs expat over a plain FB2 file, reporting progress as the share of the
// file read, mapped onto [progressFrom, progressTo]. A file whose XML
// declaration names no encoding is sniffed: valid UTF-8 stays UTF-8, anything
// else is read as Windows-1251 or KOI8-R, whichever its letter frequencies fit.
XmlResult parseFile(const std::string& path, const XmlHandlers& handlers, XML_Parser& parserOut,
                    ProgressFn progress = nullptr, void* progressCtx = nullptr, int progressFrom = 0,
                    int progressTo = 100);

// FNV-1a over an id, the key both passes of the converter agree on.
uint32_t idHash(const char* id);

}  // namespace fb2
