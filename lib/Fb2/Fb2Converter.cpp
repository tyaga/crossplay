#include "Fb2Converter.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Fb2Xml.h"
#include "ZipStoreWriter.h"

namespace fb2 {

namespace {

constexpr int MAX_DEPTH = 96;
// The main body splits into chapters at its first two <section> levels, and
// the contents lists one level further.
constexpr int SPLIT_DEPTH = 2;
constexpr int TOC_DEPTH = 3;
// Progress shares of the two passes: the second also decodes and writes images.
constexpr int SCAN_SHARE = 35;
// Bytes of a chapter's opening kept for its contents label.
constexpr size_t OPENING_LENGTH = 64;
constexpr size_t LABEL_LENGTH = 48;

constexpr char CSS[] =
    "h1,h2,h3,h4,h5,h6{text-align:center}\n"
    ".subtitle{text-align:center}\n"
    ".epigraph,.cite{margin-left:2em;margin-right:1em}\n"
    ".epigraph{font-style:italic}\n"
    ".text-author{text-align:right}\n"
    ".poem{margin-left:1.5em}\n"
    ".stanza{margin-bottom:1em}\n"
    ".v{text-indent:0;text-align:left}\n"
    ".image{text-align:center;text-indent:0}\n";

enum class Kind : uint8_t {
  Other,
  Description,
  TitleInfo,
  BookTitle,
  Author,
  FirstName,
  MiddleName,
  LastName,
  Nickname,
  Lang,
  Coverpage,
  Annotation,
  Body,
  Section,
  Title,
  Binary,
  Skip,
};

struct IdChapter {
  uint32_t hash;
  uint16_t chapter;
  bool operator<(const IdChapter& o) const { return hash < o.hash; }
};

struct BinaryImage {
  uint32_t hash;
  std::string name;  // entry name under OEBPS/images/
  const char* mediaType;
};

struct TocEntry {
  std::string label;
  uint16_t chapter;
  uint8_t depth;
  std::string anchor;  // empty when the entry opens its chapter
};

void appendEscaped(std::string& out, const char* s, const size_t len, const bool attribute) {
  for (size_t i = 0; i < len; i++) {
    const char c = s[i];
    if (c == '&') {
      out += "&amp;";
    } else if (c == '<') {
      out += "&lt;";
    } else if (c == '>') {
      out += "&gt;";
    } else if (attribute && c == '"') {
      out += "&quot;";
    } else {
      out += c;
    }
  }
}

std::string escaped(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  appendEscaped(out, s.data(), s.size(), true);
  return out;
}

// FB2 ids are free text; XHTML ids and our entry names are not.
std::string safeId(const char* id) {
  std::string out;
  if (id[0] >= '0' && id[0] <= '9') out = "id";
  for (const char* p = id; *p; ++p) {
    const char c = *p;
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    out += ok ? c : '_';
  }
  return out;
}

void collapseInto(std::string& out, const char* s, const int len) {
  for (int i = 0; i < len; i++) {
    const char c = s[i];
    const bool space = c == ' ' || c == '\n' || c == '\r' || c == '\t';
    if (space) {
      if (!out.empty() && out.back() != ' ') out += ' ';
    } else {
      out += c;
    }
  }
}

std::string trimmed(const std::string& s) {
  const size_t a = s.find_first_not_of(' ');
  if (a == std::string::npos) return {};
  const size_t b = s.find_last_not_of(' ');
  return s.substr(a, b - a + 1);
}

bool hasCyrillic(const std::string& s) {
  for (size_t i = 0; i + 1 < s.size(); i++) {
    const auto b = static_cast<uint8_t>(s[i]);
    if (b == 0xD0 || b == 0xD1) return true;
  }
  return false;
}

class Converter {
 public:
  bool run(const std::string& fb2Path, const std::string& epubPath, ProgressFn progress, void* ctx);

 private:
  enum class Pass : uint8_t { Scan, Write };

  static void XMLCALL onStart(void* user, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL onEnd(void* user, const XML_Char* name);
  static void XMLCALL onText(void* user, const XML_Char* s, int len);

  void start(const char* name, const XML_Char** atts);
  void end(const char* name);
  void text(const char* s, int len);

  void reset();
  void out(const char* s);
  void out(const std::string& s) { out(s.c_str()); }
  void outText(const char* s, int len);
  void markContent() {
    if (chapterOpen && !inAnnotation) chapterHasContent = true;
  }
  void beginChapter();
  void endChapter();
  bool inMainBody() const { return bodyIndex == 1; }
  int titleLevel() const;
  const IdChapter* findId(uint32_t hash) const;
  const BinaryImage* findImage(const char* href) const;
  void emitImage(const char* href, bool inlineImage);
  void writePackage();
  std::string chapterName(uint16_t chapter) const;
  std::string authorLine() const;

  Pass pass = Pass::Scan;
  XML_Parser parser = nullptr;
  ZipStoreWriter zip;

  // Element stack: what each open element is, and what closes its output.
  Kind kinds[MAX_DEPTH] = {};
  const char* closers[MAX_DEPTH] = {};
  bool textual[MAX_DEPTH] = {};  // opened inline content: p, v, subtitle, ...
  int depth = 0;
  int skipDepth = 0;  // >0 while inside an element whose content is dropped

  // Description
  std::string title;
  std::vector<std::string> authors;
  std::string first, middle, last, nick;
  std::string language;
  std::string coverHref;
  std::string annotation;  // XHTML, written at the head of the first chapter
  bool inAnnotation = false;
  // Known in both passes, unlike `annotation`, which only the write pass
  // fills: whether the first chapter starts with content decides where every
  // later chapter boundary falls, and the passes must agree on those.
  bool annotationHasText = false;
  std::string* capture = nullptr;  // plain-text field being read

  // Bodies and chapters
  int bodyIndex = 0;
  int sectionDepth = 0;
  unsigned sectionCounter = 0;
  uint16_t chapter = 0;
  bool chapterOpen = false;
  bool chapterHasContent = false;
  bool anyChapter = false;
  int textDepth = 0;  // >0 inside p, v, subtitle, text-author: inline content
  int titleDepth = 0;
  // A body's or a section's title is a heading and a contents entry; a poem's
  // or a stanza's is neither.
  bool titleIsHeading = false;
  std::string titleText;
  std::vector<std::string> sectionAnchors;  // per open section
  std::vector<bool> sectionOpensChapter;

  // Binaries
  bool inBinary = false;
  uint8_t b64[4] = {};
  int b64Len = 0;

  // Learned by the scan pass
  std::vector<IdChapter> ids;
  std::vector<BinaryImage> images;
  // Collected by the write pass
  std::vector<TocEntry> toc;
  std::vector<uint16_t> imagesWritten;
  // Each chapter's opening words, the contents label of a chapter whose
  // section has no title.
  std::vector<std::string> openings;

  std::string buffer;  // reused for escaped text
};

void Converter::reset() {
  depth = 0;
  skipDepth = 0;
  title.clear();
  authors.clear();
  first.clear();
  middle.clear();
  last.clear();
  nick.clear();
  language.clear();
  coverHref.clear();
  annotation.clear();
  inAnnotation = false;
  annotationHasText = false;
  capture = nullptr;
  bodyIndex = 0;
  sectionDepth = 0;
  sectionCounter = 0;
  chapter = 0;
  chapterOpen = false;
  chapterHasContent = false;
  anyChapter = false;
  textDepth = 0;
  titleDepth = 0;
  titleText.clear();
  sectionAnchors.clear();
  sectionOpensChapter.clear();
  inBinary = false;
  b64Len = 0;
}

std::string Converter::chapterName(const uint16_t index) const {
  char name[16];
  snprintf(name, sizeof(name), "c%03u.xhtml", static_cast<unsigned>(index));
  return name;
}

void Converter::out(const char* s) {
  if (inAnnotation) {
    if (pass == Pass::Write) annotation += s;
    return;
  }
  if (!chapterOpen) return;
  chapterHasContent = true;
  if (pass == Pass::Write) zip.write(s, strlen(s));
}

void Converter::outText(const char* s, const int len) {
  bool blank = true;
  for (int i = 0; i < len && blank; i++) blank = s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t';
  if (inAnnotation) {
    if (!blank) annotationHasText = true;
    if (pass == Pass::Write) appendEscaped(annotation, s, static_cast<size_t>(len), false);
    return;
  }
  if (!chapterOpen) return;
  if (!blank) chapterHasContent = true;
  if (pass != Pass::Write) return;
  if (openings.size() <= chapter) openings.resize(chapter + 1u);
  if (openings[chapter].size() < OPENING_LENGTH) collapseInto(openings[chapter], s, len);
  buffer.clear();
  appendEscaped(buffer, s, static_cast<size_t>(len), false);
  zip.write(buffer.data(), buffer.size());
}

void Converter::beginChapter() {
  if (chapterOpen && !chapterHasContent) return;  // reuse an empty chapter rather than leave a blank page
  endChapter();
  if (anyChapter) chapter++;
  chapterOpen = true;
  chapterHasContent = false;
  anyChapter = true;
  if (pass != Pass::Write) return;
  zip.beginEntry("OEBPS/" + chapterName(chapter));
  std::string head =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"" +
      escaped(language) + "\"><head><title>" + escaped(title) +
      "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head><body>\n";
  zip.write(head);
}

void Converter::endChapter() {
  if (!chapterOpen) return;
  chapterOpen = false;
  if (pass == Pass::Write) {
    zip.write("\n</body></html>\n", 16);
    zip.endEntry();
  }
}

int Converter::titleLevel() const {
  if (bodyIndex > 1) return 3;
  return std::min(sectionDepth + 1, 6);
}

const IdChapter* Converter::findId(const uint32_t hash) const {
  const auto it = std::lower_bound(ids.begin(), ids.end(), IdChapter{hash, 0});
  return it != ids.end() && it->hash == hash ? &*it : nullptr;
}

const BinaryImage* Converter::findImage(const char* href) const {
  if (!href || href[0] != '#') return nullptr;
  const uint32_t hash = idHash(href + 1);
  for (const BinaryImage& image : images) {
    if (image.hash == hash) return &image;
  }
  return nullptr;
}

void Converter::emitImage(const char* href, const bool inlineImage) {
  // The scan pass has not met the binaries yet: marking content here, in both
  // passes, keeps their chapter boundaries identical.
  if (href) markContent();
  if (pass != Pass::Write) return;
  const BinaryImage* image = findImage(href);
  if (!image) return;
  std::string tag = "<img src=\"images/" + image->name + "\" alt=\"\"/>";
  if (!inlineImage) tag = "<p class=\"image\">" + tag + "</p>";
  out(tag);
}

std::string Converter::authorLine() const {
  std::string line;
  for (const std::string& a : authors) {
    if (a.empty()) continue;
    if (!line.empty()) line += ", ";
    line += a;
  }
  return line;
}

void XMLCALL Converter::onStart(void* user, const XML_Char* name, const XML_Char** atts) {
  static_cast<Converter*>(user)->start(localName(name), atts);
}

void XMLCALL Converter::onEnd(void* user, const XML_Char* name) { static_cast<Converter*>(user)->end(localName(name)); }

void XMLCALL Converter::onText(void* user, const XML_Char* s, const int len) {
  static_cast<Converter*>(user)->text(s, len);
}

void Converter::start(const char* name, const XML_Char** atts) {
  if (depth >= MAX_DEPTH - 1) {
    depth++;
    return;
  }
  const Kind parent = depth > 0 ? kinds[depth - 1] : Kind::Other;
  Kind kind = Kind::Other;
  const char* closer = nullptr;
  depth++;

  if (skipDepth > 0) {
    kinds[depth - 1] = Kind::Skip;
    closers[depth - 1] = nullptr;
    textual[depth - 1] = false;
    return;
  }
  textual[depth - 1] = false;

  const bool inDescription = std::any_of(kinds, kinds + depth - 1, [](Kind k) { return k == Kind::Description; });

  if (strcmp(name, "description") == 0) {
    kinds[depth - 1] = Kind::Description;
    closers[depth - 1] = nullptr;
    return;
  }
  if (inDescription) {
    if (strcmp(name, "title-info") == 0) {
      kind = Kind::TitleInfo;
    } else if (parent == Kind::TitleInfo && strcmp(name, "book-title") == 0) {
      kind = Kind::BookTitle;
      capture = &title;
    } else if (parent == Kind::TitleInfo && strcmp(name, "author") == 0) {
      kind = Kind::Author;
      first.clear();
      middle.clear();
      last.clear();
      nick.clear();
    } else if (parent == Kind::Author && strcmp(name, "first-name") == 0) {
      capture = &first;
    } else if (parent == Kind::Author && strcmp(name, "middle-name") == 0) {
      capture = &middle;
    } else if (parent == Kind::Author && strcmp(name, "last-name") == 0) {
      capture = &last;
    } else if (parent == Kind::Author && strcmp(name, "nickname") == 0) {
      capture = &nick;
    } else if (parent == Kind::TitleInfo && strcmp(name, "lang") == 0) {
      capture = &language;
    } else if (parent == Kind::TitleInfo && strcmp(name, "coverpage") == 0) {
      kind = Kind::Coverpage;
    } else if (parent == Kind::Coverpage && strcmp(name, "image") == 0) {
      if (coverHref.empty()) {
        const char* href = attribute(atts, "href");
        if (href) coverHref = href;
      }
    } else if (parent == Kind::TitleInfo && strcmp(name, "annotation") == 0) {
      kind = Kind::Annotation;
      inAnnotation = true;
    } else if (!inAnnotation) {
      // document-info, publish-info, custom-info, and title-info fields the
      // book does not show: nothing inside them reaches the page.
      kind = Kind::Skip;
      if (parent == Kind::Description) skipDepth = depth;
    }
    if (!inAnnotation || kind == Kind::Annotation) {
      kinds[depth - 1] = kind;
      closers[depth - 1] = nullptr;
      return;
    }
  }

  if (strcmp(name, "binary") == 0) {
    kind = Kind::Binary;
    const char* id = attribute(atts, "id");
    const char* type = attribute(atts, "content-type");
    if (pass == Pass::Scan && id && type) {
      const char* ext = nullptr;
      const char* media = nullptr;
      if (strcmp(type, "image/jpeg") == 0 || strcmp(type, "image/jpg") == 0) {
        ext = ".jpg";
        media = "image/jpeg";
      } else if (strcmp(type, "image/png") == 0) {
        ext = ".png";
        media = "image/png";
      }
      if (ext) {
        char name[24];
        snprintf(name, sizeof(name), "img%u%s", static_cast<unsigned>(images.size()), ext);
        images.push_back({idHash(id), name, media});
      }
    }
    // Both passes close the chapter here, so a binary between two bodies
    // cannot shift the numbering of the second.
    endChapter();
    if (pass == Pass::Write && id) {
      const std::string href = std::string("#") + id;
      const BinaryImage* image = findImage(href.c_str());
      if (image) {
        inBinary = zip.beginEntry("OEBPS/images/" + image->name);
        b64Len = 0;
        if (inBinary) imagesWritten.push_back(static_cast<uint16_t>(image - images.data()));
      }
    }
    kinds[depth - 1] = kind;
    closers[depth - 1] = nullptr;
    return;
  }

  if (strcmp(name, "stylesheet") == 0) {
    skipDepth = depth;
    kinds[depth - 1] = Kind::Skip;
    closers[depth - 1] = nullptr;
    return;
  }

  const char* rawId = attribute(atts, "id");
  const char* id = rawId;

  if (strcmp(name, "body") == 0) {
    kind = Kind::Body;
    bodyIndex++;
    sectionDepth = 0;
    if (language.empty()) language = hasCyrillic(title) ? "ru" : "en";
    beginChapter();
    if (bodyIndex == 1 && annotationHasText) {
      out("<div class=\"annotation\">");
      if (pass == Pass::Write) out(annotation);
      out("</div>\n");
    }
  } else if (bodyIndex == 0 && !inAnnotation) {
    // Outside the text: nothing to write.
  } else if (strcmp(name, "section") == 0) {
    kind = Kind::Section;
    sectionDepth++;
    sectionCounter++;
    const bool opens = inMainBody() && sectionDepth <= SPLIT_DEPTH;
    if (opens) beginChapter();
    char generated[20];
    snprintf(generated, sizeof(generated), "fb2s%u", sectionCounter);
    const std::string anchor = id ? safeId(id) : std::string(generated);
    sectionAnchors.push_back(anchor);
    sectionOpensChapter.push_back(opens && !chapterHasContent);
    out("<div class=\"section\" id=\"" + anchor + "\">\n");
    closer = "</div>\n";
    id = nullptr;  // written on the div already
  } else if (strcmp(name, "title") == 0) {
    kind = Kind::Title;
    titleDepth = depth;
    titleIsHeading = parent == Kind::Section || parent == Kind::Body;
    titleText.clear();
    out("<div class=\"title\">");
    closer = "</div>\n";
  } else if (strcmp(name, "p") == 0) {
    const bool inTitle = titleDepth > 0;
    std::string open;
    if (inTitle && !titleIsHeading) {
      open = "<p class=\"subtitle\"><b";
      closer = "</b></p>\n";
    } else if (inTitle) {
      const int level = titleLevel();
      static const char* const hClose[] = {"", "</h1>\n", "</h2>\n", "</h3>\n", "</h4>\n", "</h5>\n", "</h6>\n"};
      open = "<h" + std::to_string(level);
      closer = hClose[level];
      if (!titleText.empty()) titleText += ' ';
    } else {
      open = "<p";
      closer = "</p>\n";
    }
    if (id) open += " id=\"" + safeId(id) + "\"";
    open += ">";
    out(open);
    textDepth++;
    textual[depth - 1] = true;
    id = nullptr;
  } else if (strcmp(name, "subtitle") == 0) {
    out("<p class=\"subtitle\"><b>");
    closer = "</b></p>\n";
    textDepth++;
    textual[depth - 1] = true;
  } else if (strcmp(name, "text-author") == 0) {
    out("<p class=\"text-author\"><i>");
    closer = "</i></p>\n";
    textDepth++;
    textual[depth - 1] = true;
  } else if (strcmp(name, "v") == 0) {
    out("<p class=\"v\">");
    closer = "</p>\n";
    textDepth++;
    textual[depth - 1] = true;
  } else if (strcmp(name, "date") == 0) {
    out("<p class=\"text-author\">");
    closer = "</p>\n";
    textDepth++;
    textual[depth - 1] = true;
  } else if (strcmp(name, "epigraph") == 0) {
    out("<blockquote class=\"epigraph\">\n");
    closer = "</blockquote>\n";
  } else if (strcmp(name, "cite") == 0) {
    out("<blockquote class=\"cite\">\n");
    closer = "</blockquote>\n";
  } else if (strcmp(name, "poem") == 0) {
    out("<div class=\"poem\">\n");
    closer = "</div>\n";
  } else if (strcmp(name, "stanza") == 0) {
    out("<div class=\"stanza\">\n");
    closer = "</div>\n";
  } else if (strcmp(name, "annotation") == 0) {
    out("<div class=\"annotation\">\n");
    closer = "</div>\n";
  } else if (strcmp(name, "empty-line") == 0) {
    out("<p>&#160;</p>\n");
  } else if (strcmp(name, "emphasis") == 0) {
    out("<i>");
    closer = "</i>";
  } else if (strcmp(name, "strong") == 0) {
    out("<b>");
    closer = "</b>";
  } else if (strcmp(name, "sup") == 0) {
    out("<sup>");
    closer = "</sup>";
  } else if (strcmp(name, "sub") == 0) {
    out("<sub>");
    closer = "</sub>";
  } else if (strcmp(name, "code") == 0) {
    out("<code>");
    closer = "</code>";
  } else if (strcmp(name, "table") == 0) {
    out("<table>\n");
    closer = "</table>\n";
  } else if (strcmp(name, "tr") == 0) {
    out("<tr>");
    closer = "</tr>\n";
  } else if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
    out(strcmp(name, "td") == 0 ? "<td>" : "<th>");
    closer = strcmp(name, "td") == 0 ? "</td>" : "</th>";
    textDepth++;
    textual[depth - 1] = true;
  } else if (strcmp(name, "image") == 0) {
    emitImage(attribute(atts, "href"), textDepth > 0);
  } else if (strcmp(name, "a") == 0) {
    const char* href = attribute(atts, "href");
    const IdChapter* target = (pass == Pass::Write && href && href[0] == '#') ? findId(idHash(href + 1)) : nullptr;
    markContent();
    if (target) {
      out("<a href=\"" + chapterName(target->chapter) + "#" + safeId(href + 1) + "\">");
      closer = "</a>";
    }
  }

  if (id) {
    // An id on an element that writes no tag of its own still needs a target.
    out("<span id=\"" + safeId(id) + "\"></span>");
  }
  // Recorded after the element's own chapter boundary, so a link lands in the
  // chapter the element was written to.
  if (pass == Pass::Scan && rawId && bodyIndex > 0) ids.push_back({idHash(rawId), chapter});

  kinds[depth - 1] = kind;
  closers[depth - 1] = closer;
}

void Converter::end(const char*) {
  if (depth <= 0) return;
  if (depth > MAX_DEPTH - 1) {
    depth--;
    return;
  }
  const Kind kind = kinds[depth - 1];
  const char* closer = closers[depth - 1];

  if (skipDepth == depth) skipDepth = 0;

  if (kind == Kind::Author) {
    std::string full;
    for (const std::string* part : {&first, &middle, &last}) {
      const std::string t = trimmed(*part);
      if (t.empty()) continue;
      if (!full.empty()) full += ' ';
      full += t;
    }
    if (full.empty()) full = trimmed(nick);
    authors.push_back(full);
  } else if (kind == Kind::Annotation) {
    inAnnotation = false;
  } else if (kind == Kind::Binary) {
    if (inBinary) {
      inBinary = false;
      zip.endEntry();
    }
  }

  if (closer) out(closer);

  if (textual[depth - 1] && textDepth > 0) textDepth--;

  if (kind == Kind::Title && titleDepth == depth) {
    titleDepth = 0;
    const std::string label = trimmed(titleText);
    if (pass == Pass::Write && titleIsHeading && !label.empty()) {
      if (bodyIndex > 1 && sectionDepth == 0) {
        toc.push_back({label, chapter, 1, ""});
      } else if (inMainBody() && sectionDepth >= 1 && sectionDepth <= TOC_DEPTH && !sectionAnchors.empty()) {
        const bool opens = sectionOpensChapter.back();
        toc.push_back({label, chapter, static_cast<uint8_t>(sectionDepth), opens ? "" : sectionAnchors.back()});
      }
    }
  } else if (kind == Kind::Section) {
    sectionDepth--;
    if (!sectionAnchors.empty()) sectionAnchors.pop_back();
    if (!sectionOpensChapter.empty()) sectionOpensChapter.pop_back();
  } else if (kind == Kind::Body) {
    sectionDepth = 0;
  }

  if (kind == Kind::BookTitle || capture) capture = nullptr;
  depth--;
}

void Converter::text(const char* s, const int len) {
  if (depth > MAX_DEPTH - 1 || skipDepth > 0) return;
  if (inBinary) {
    // Base64, decoded as it arrives, four characters at a time.
    uint8_t decoded[192];
    size_t n = 0;
    for (int i = 0; i < len; i++) {
      const char c = s[i];
      int v;
      if (c >= 'A' && c <= 'Z') {
        v = c - 'A';
      } else if (c >= 'a' && c <= 'z') {
        v = c - 'a' + 26;
      } else if (c >= '0' && c <= '9') {
        v = c - '0' + 52;
      } else if (c == '+') {
        v = 62;
      } else if (c == '/') {
        v = 63;
      } else if (c == '=') {
        v = -1;
      } else {
        continue;
      }
      b64[b64Len++] = static_cast<uint8_t>(v < 0 ? 64 : v);
      if (b64Len < 4) continue;
      b64Len = 0;
      decoded[n++] = static_cast<uint8_t>((b64[0] << 2) | ((b64[1] & 0x3F) >> 4));
      if (b64[2] != 64) decoded[n++] = static_cast<uint8_t>(((b64[1] & 0x0F) << 4) | ((b64[2] & 0x3F) >> 2));
      if (b64[2] != 64 && b64[3] != 64) decoded[n++] = static_cast<uint8_t>(((b64[2] & 0x03) << 6) | b64[3]);
      if (n + 3 > sizeof(decoded)) {
        zip.write(decoded, n);
        n = 0;
      }
    }
    if (n > 0) zip.write(decoded, n);
    return;
  }
  if (capture) {
    collapseInto(*capture, s, len);
    return;
  }
  const bool inDescription = std::any_of(kinds, kinds + depth, [](Kind k) { return k == Kind::Description; });
  if (inDescription && !inAnnotation) return;
  if (bodyIndex == 0 && !inAnnotation) return;
  if (depth > 0 && kinds[depth - 1] == Kind::Binary) return;
  if (titleDepth > 0 && pass == Pass::Write) collapseInto(titleText, s, len);
  outText(s, len);
}

void Converter::writePackage() {
  const std::string author = authorLine();
  const std::string bookTitle = title.empty() ? std::string("Untitled") : title;

  zip.beginEntry("OEBPS/style.css");
  zip.write(CSS, sizeof(CSS) - 1);
  zip.endEntry();

  // toc.ncx, nested by depth.
  std::string ncx =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head>"
      "<meta name=\"dtb:uid\" content=\"fb2\"/></head><docTitle><text>" +
      escaped(bookTitle) + "</text></docTitle><navMap>\n";
  // Every chapter gets an entry, so the reader can always name where it is: an
  // untitled one is labelled with its opening words, cut at a word.
  const unsigned chapterCount = anyChapter ? chapter + 1u : 0u;
  std::vector<TocEntry> complete;
  complete.reserve(toc.size() + chapterCount);
  size_t next = 0;
  for (unsigned c = 0; c < chapterCount; c++) {
    const bool titled = next < toc.size() && toc[next].chapter == c;
    if (!titled) {
      std::string label = c < openings.size() ? trimmed(openings[c]) : std::string();
      if (label.size() > LABEL_LENGTH) {
        size_t cut = label.rfind(' ', LABEL_LENGTH);
        if (cut == std::string::npos || cut < LABEL_LENGTH / 2) cut = LABEL_LENGTH;
        while (cut > 0 && (static_cast<uint8_t>(label[cut]) & 0xC0) == 0x80) cut--;
        label = label.substr(0, cut) + "\xE2\x80\xA6";
      }
      if (label.empty()) label = bookTitle;
      complete.push_back({label, static_cast<uint16_t>(c), 1, ""});
    }
    while (next < toc.size() && toc[next].chapter == c) complete.push_back(toc[next++]);
  }
  while (next < toc.size()) complete.push_back(toc[next++]);
  toc.swap(complete);
  if (toc.empty()) toc.push_back({bookTitle, 0, 1, ""});
  int open = 0;
  unsigned order = 0;
  for (const TocEntry& entry : toc) {
    const int d = std::max<int>(1, std::min<int>(entry.depth, open + 1));
    while (open >= d) {
      ncx += "</navPoint>\n";
      open--;
    }
    order++;
    std::string src = chapterName(entry.chapter);
    if (!entry.anchor.empty()) src += "#" + entry.anchor;
    ncx += "<navPoint id=\"n" + std::to_string(order) + "\" playOrder=\"" + std::to_string(order) +
           "\"><navLabel><text>" + escaped(entry.label) + "</text></navLabel><content src=\"" + src + "\"/>\n";
    open = d;
  }
  while (open-- > 0) ncx += "</navPoint>\n";
  ncx += "</navMap></ncx>\n";
  zip.beginEntry("OEBPS/toc.ncx");
  zip.write(ncx);
  zip.endEntry();

  const BinaryImage* cover = coverHref.empty() ? nullptr : findImage(coverHref.c_str());
  const bool coverWritten = cover && std::find(imagesWritten.begin(), imagesWritten.end(),
                                               static_cast<uint16_t>(cover - images.data())) != imagesWritten.end();

  std::string opf =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"bookid\">\n"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">\n"
      "<dc:title>" +
      escaped(bookTitle) + "</dc:title>\n";
  if (!author.empty()) opf += "<dc:creator opf:role=\"aut\">" + escaped(author) + "</dc:creator>\n";
  opf += "<dc:language>" + escaped(language) +
         "</dc:language>\n<dc:identifier id=\"bookid\">fb2:" + std::to_string(idHash(bookTitle.c_str())) +
         "</dc:identifier>\n";
  if (coverWritten) opf += "<meta name=\"cover\" content=\"i" + std::to_string(cover - images.data()) + "\"/>\n";
  opf +=
      "</metadata>\n<manifest>\n"
      "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
      "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n";
  const unsigned chapters = anyChapter ? chapter + 1u : 0u;
  for (unsigned i = 0; i < chapters; i++) {
    opf += "<item id=\"c" + std::to_string(i) + "\" href=\"" + chapterName(static_cast<uint16_t>(i)) +
           "\" media-type=\"application/xhtml+xml\"/>\n";
  }
  for (const uint16_t i : imagesWritten) {
    opf += "<item id=\"i" + std::to_string(i) + "\" href=\"images/" + images[i].name + "\" media-type=\"" +
           images[i].mediaType + "\"/>\n";
  }
  opf += "</manifest>\n<spine toc=\"ncx\">\n";
  for (unsigned i = 0; i < chapters; i++) opf += "<itemref idref=\"c" + std::to_string(i) + "\"/>\n";
  opf += "</spine>\n</package>\n";
  zip.beginEntry("OEBPS/content.opf");
  zip.write(opf);
  zip.endEntry();
}

bool Converter::run(const std::string& fb2Path, const std::string& epubPath, ProgressFn progress, void* ctx) {
  XmlHandlers handlers;
  handlers.userData = this;
  handlers.start = &Converter::onStart;
  handlers.end = &Converter::onEnd;
  handlers.text = &Converter::onText;

  pass = Pass::Scan;
  reset();
  const XmlResult scanned = parseFile(fb2Path, handlers, parser, progress, ctx, 0, SCAN_SHARE);
  if (scanned == XmlResult::Broken && !anyChapter) {
    LOG_ERR("FB2", "No readable text in %s", fb2Path.c_str());
    return false;
  }
  std::sort(ids.begin(), ids.end());

  if (!zip.open(epubPath)) return false;
  static constexpr char MIMETYPE[] = "application/epub+zip";
  static constexpr char CONTAINER[] =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
      "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>\n";
  zip.beginEntry("mimetype");
  zip.write(MIMETYPE, sizeof(MIMETYPE) - 1);
  zip.endEntry();
  zip.beginEntry("META-INF/container.xml");
  zip.write(CONTAINER, sizeof(CONTAINER) - 1);
  zip.endEntry();

  pass = Pass::Write;
  reset();
  toc.clear();
  imagesWritten.clear();
  openings.clear();
  parseFile(fb2Path, handlers, parser, progress, ctx, SCAN_SHARE, 100);
  if (inBinary) {
    inBinary = false;
    zip.endEntry();
  }
  endChapter();

  writePackage();
  if (!zip.finish()) {
    Storage.remove(epubPath.c_str());
    return false;
  }
  LOG_INF("FB2", "Converted %s: %u chapters, %u images, %u contents entries", fb2Path.c_str(),
          static_cast<unsigned>(chapter + 1), static_cast<unsigned>(imagesWritten.size()),
          static_cast<unsigned>(toc.size()));
  return true;
}

}  // namespace

bool convertToEpub(const std::string& fb2Path, const std::string& epubPath, ProgressFn progress, void* progressCtx) {
  Converter converter;
  return converter.run(fb2Path, epubPath, progress, progressCtx);
}

}  // namespace fb2
