#include "Fb2Metadata.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Print.h>
#include <ZipFile.h>

#include <cstring>
#include <vector>

#include "Fb2Book.h"
#include "Fb2Xml.h"

namespace fb2 {

namespace {

// Reads <title-info> and stops at the end of <description>, before the text.
struct MetadataReader {
  XML_Parser parser = nullptr;
  Metadata* out = nullptr;
  int depth = 0;
  int titleInfoDepth = 0;
  int authorDepth = 0;
  std::string* capture = nullptr;
  std::string first, middle, last, nick;
  std::vector<std::string> authors;
  bool sawRoot = false;

  static void XMLCALL onStart(void* user, const XML_Char* rawName, const XML_Char**) {
    auto* self = static_cast<MetadataReader*>(user);
    const char* name = localName(rawName);
    self->depth++;
    if (self->depth == 1) {
      self->sawRoot = strcmp(name, "FictionBook") == 0;
      if (!self->sawRoot) XML_StopParser(self->parser, XML_FALSE);
      return;
    }
    if (strcmp(name, "body") == 0) {
      XML_StopParser(self->parser, XML_FALSE);
      return;
    }
    if (strcmp(name, "title-info") == 0) {
      self->titleInfoDepth = self->depth;
    } else if (self->titleInfoDepth > 0 && self->depth == self->titleInfoDepth + 1) {
      if (strcmp(name, "book-title") == 0) {
        self->capture = &self->out->title;
      } else if (strcmp(name, "lang") == 0) {
        self->capture = &self->out->language;
      } else if (strcmp(name, "author") == 0) {
        self->authorDepth = self->depth;
        self->first.clear();
        self->middle.clear();
        self->last.clear();
        self->nick.clear();
      }
    } else if (self->authorDepth > 0 && self->depth == self->authorDepth + 1) {
      if (strcmp(name, "first-name") == 0) self->capture = &self->first;
      if (strcmp(name, "middle-name") == 0) self->capture = &self->middle;
      if (strcmp(name, "last-name") == 0) self->capture = &self->last;
      if (strcmp(name, "nickname") == 0) self->capture = &self->nick;
    }
  }

  static void XMLCALL onEnd(void* user, const XML_Char* rawName) {
    auto* self = static_cast<MetadataReader*>(user);
    const char* name = localName(rawName);
    if (self->depth == self->authorDepth) {
      self->authorDepth = 0;
      std::string full;
      for (const std::string* part : {&self->first, &self->middle, &self->last}) {
        if (part->empty()) continue;
        if (!full.empty()) full += ' ';
        full += *part;
      }
      if (full.empty()) full = self->nick;
      if (!full.empty()) self->authors.push_back(full);
    }
    if (self->depth == self->titleInfoDepth) self->titleInfoDepth = 0;
    self->capture = nullptr;
    self->depth--;
    if (strcmp(name, "description") == 0) XML_StopParser(self->parser, XML_FALSE);
  }

  static void XMLCALL onText(void* user, const XML_Char* s, const int len) {
    auto* self = static_cast<MetadataReader*>(user);
    if (!self->capture) return;
    for (int i = 0; i < len; i++) {
      const char c = s[i];
      const bool space = c == ' ' || c == '\n' || c == '\r' || c == '\t';
      if (space) {
        if (!self->capture->empty() && self->capture->back() != ' ') *self->capture += ' ';
      } else {
        *self->capture += c;
      }
    }
  }

  XmlHandlers handlers() {
    XmlHandlers h;
    h.userData = this;
    h.start = &onStart;
    h.end = &onEnd;
    h.text = &onText;
    return h;
  }

  void finish() {
    auto trim = [](std::string& s) {
      while (!s.empty() && s.back() == ' ') s.pop_back();
    };
    trim(out->title);
    trim(out->language);
    out->author.clear();
    for (std::string& a : authors) {
      trim(a);
      if (a.empty()) continue;
      if (!out->author.empty()) out->author += ", ";
      out->author += a;
    }
    if (out->language.empty()) {
      bool cyrillic = false;
      for (const char c : out->title)
        cyrillic = cyrillic || static_cast<uint8_t>(c) == 0xD0 || static_cast<uint8_t>(c) == 0xD1;
      out->language = cyrillic ? "ru" : "en";
    }
  }
};

// Feeds an .fb2 entry of a zip straight into the parser, stopping the inflate
// as soon as the description has been read.
class ParserSink final : public Print {
 public:
  explicit ParserSink(MetadataReader& reader) : reader_(reader) {}
  ~ParserSink() override {
    if (parser_) XML_ParserFree(parser_);
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    if (done_) return 0;
    if (!parser_) {
      parser_ = createParser(data, len, reader_.handlers());
      if (!parser_) {
        done_ = true;
        return 0;
      }
      reader_.parser = parser_;
    }
    if (XML_Parse(parser_, reinterpret_cast<const char*>(data), static_cast<int>(len), XML_FALSE) != XML_STATUS_OK) {
      done_ = true;
      return 0;
    }
    return len;
  }

 private:
  MetadataReader& reader_;
  XML_Parser parser_ = nullptr;
  bool done_ = false;
};

}  // namespace

// The first .fb2 entry of an .fb2.zip, or empty.
std::string fb2EntryName(ZipFile& zip) {
  std::string found;
  zip.enumerateFilePaths([&found](const std::string_view name) {
    if (found.empty() && FsHelpers::checkFileExtension(name, ".fb2")) found = std::string(name);
  });
  return found;
}

bool readMetadata(const std::string& path, Metadata& out) {
  out = Metadata{};
  MetadataReader reader;
  reader.out = &out;
  if (FsHelpers::checkFileExtension(path, ".zip")) {
    ZipFile zip(path);
    const std::string entry = fb2EntryName(zip);
    if (entry.empty()) return false;
    ParserSink sink(reader);
    zip.readFileToStream(entry.c_str(), sink, 1024, /*allowEarlyStop=*/true);
  } else if (parseFile(path, reader.handlers(), reader.parser) == XmlResult::Broken && !reader.sawRoot) {
    return false;
  }
  if (!reader.sawRoot) return false;
  reader.finish();
  return true;
}

}  // namespace fb2
