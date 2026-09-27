#include <Fb2Converter.h>
#include <MinizConfig.h>
#include <gtest/gtest.h>

#include <map>
#include <string>

#include "HalStorage.h"

namespace {

// Reads back a STORED zip written by ZipStoreWriter, checking every entry's
// CRC and that the local header agrees with the central directory.
struct Archive {
  std::vector<std::string> order;
  std::map<std::string, std::string> entries;
  bool ok = false;
};

uint32_t le32(const std::vector<uint8_t>& b, const size_t at) {
  return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}
uint16_t le16(const std::vector<uint8_t>& b, const size_t at) { return b[at] | (b[at + 1] << 8); }

Archive readArchive(const std::string& path) {
  Archive archive;
  const auto found = fake::files.find(path);
  if (found == fake::files.end()) return archive;
  const std::vector<uint8_t>& b = found->second->bytes;
  if (b.size() < 22) return archive;
  const size_t eocd = b.size() - 22;
  if (le32(b, eocd) != 0x06054b50) return archive;
  const uint16_t count = le16(b, eocd + 10);
  size_t at = le32(b, eocd + 16);
  for (uint16_t i = 0; i < count; i++) {
    if (le32(b, at) != 0x02014b50) return archive;
    const uint32_t crc = le32(b, at + 16);
    const uint32_t size = le32(b, at + 24);
    const uint16_t nameLen = le16(b, at + 28);
    const uint32_t local = le32(b, at + 42);
    const std::string name(b.begin() + at + 46, b.begin() + at + 46 + nameLen);
    if (le32(b, local) != 0x04034b50 || le32(b, local + 14) != crc || le32(b, local + 22) != size) return archive;
    const size_t data = local + 30 + le16(b, local + 26) + le16(b, local + 28);
    const std::string content(b.begin() + data, b.begin() + data + size);
    if (mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(content.data()), content.size()) != crc) {
      return archive;
    }
    archive.order.push_back(name);
    archive.entries[name] = content;
    at += 46 + nameLen + le16(b, at + 30) + le16(b, at + 32);
  }
  archive.ok = true;
  return archive;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// 1x1 PNG and a few JPEG-ish bytes, to check the decode round-trips exactly.
constexpr char PNG_B64[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk\n  YPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==";
const std::string JPEG_BYTES = std::string("\xFF\xD8\xFF\xE0\x00\x10JFIF\x00\x01", 12);
constexpr char JPEG_B64[] = "/9j/4AAQSkZJRgAB";

std::string richBook() {
  return std::string(
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\" "
             "xmlns:l=\"http://www.w3.org/1999/xlink\">\n"
             "<description><title-info><genre>prose</genre>"
             "<author><first-name>Антон</first-name><middle-name>Павлович</middle-name>"
             "<last-name>Чехов</last-name></author>"
             "<author><nickname>Аноним</nickname></author>"
             "<book-title>Рассказы &amp; повести</book-title>"
             "<annotation><p>Сборник <emphasis>ранних</emphasis> рассказов.</p></annotation>"
             "<coverpage><image l:href=\"#cover.jpg\"/></coverpage><lang>ru</lang></title-info>"
             "<document-info><author><first-name>Не</first-name><last-name>Автор</last-name></author>"
             "<program-used>x</program-used></document-info></description>\n"
             "<body><title><p>Рассказы</p></title>"
             "<epigraph><p>Краткость \u2014 сестра таланта.</p><text-author>А. Ч.</text-author></epigraph>"
             "<section id=\"s1\"><title><p>Часть первая</p></title>"
             "<section><title><p>Толстый и тонкий</p></title>"
             "<p>На вокзале <strong>встретились</strong> два приятеля<a l:href=\"#n1\" type=\"note\">[1]</a>.</p>"
             "<section><title><p>Сцена</p></title><p>Глубже.</p><image l:href=\"#pic.png\"/></section>"
             "</section>"
             "<section><title><p>Хамелеон</p></title>"
             "<poem><title><p>Песня</p></title><stanza><v>Строка раз</v><v>Строка два</v></stanza></poem>"
             "<p>Ссылка назад<a l:href=\"#s1\">!</a> и <a l:href=\"http://example.com\">сайт</a>.</p>"
             "<empty-line/><cite><p>Цитата</p></cite><subtitle>* * *</subtitle></section>"
             "</section></body>\n"
             "<body name=\"notes\"><title><p>Примечания</p></title>"
             "<section id=\"n1\"><title><p>1</p></title><p>Текст сноски.</p></section></body>\n"
             "<binary id=\"cover.jpg\" content-type=\"image/jpeg\">") +
         JPEG_B64 + "</binary>\n<binary id=\"pic.png\" content-type=\"image/png\">" + PNG_B64 +
         "</binary>\n<binary id=\"anim.gif\" content-type=\"image/gif\">R0lGODlh</binary>\n</FictionBook>\n";
}

class Fb2ConverterTest : public ::testing::Test {
 protected:
  void SetUp() override { fake::reset(); }
};

TEST_F(Fb2ConverterTest, WritesAReadableEpub) {
  fake::add("/b.fb2", richBook());
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  const Archive epub = readArchive("/out.epub");
  ASSERT_TRUE(epub.ok);
  EXPECT_EQ(epub.order.front(), "mimetype");
  EXPECT_EQ(epub.entries.at("mimetype"), "application/epub+zip");
  EXPECT_TRUE(epub.entries.count("META-INF/container.xml"));

  const std::string& opf = epub.entries.at("OEBPS/content.opf");
  EXPECT_TRUE(contains(opf, "<dc:title>Рассказы &amp; повести</dc:title>"));
  EXPECT_TRUE(contains(opf, "<dc:creator opf:role=\"aut\">Антон Павлович Чехов, Аноним</dc:creator>"));
  EXPECT_FALSE(contains(opf, "Не Автор"));
  EXPECT_TRUE(contains(opf, "<dc:language>ru</dc:language>"));
  EXPECT_TRUE(contains(opf, "<meta name=\"cover\" content=\"i0\"/>"));
  EXPECT_TRUE(contains(opf, "href=\"images/img0.jpg\" media-type=\"image/jpeg\""));
  EXPECT_TRUE(contains(opf, "href=\"images/img1.png\" media-type=\"image/png\""));
  EXPECT_FALSE(contains(opf, "gif"));
}

TEST_F(Fb2ConverterTest, SplitsAtTheFirstTwoSectionLevels) {
  fake::add("/b.fb2", richBook());
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  const Archive epub = readArchive("/out.epub");
  ASSERT_TRUE(epub.ok);
  // c000: body title, epigraph, annotation. c001: part one. c002, c003: its
  // two sections, the third level staying inside c002. c004: notes.
  const std::string& c0 = epub.entries.at("OEBPS/c000.xhtml");
  EXPECT_TRUE(contains(c0, "<h1>Рассказы</h1>"));
  EXPECT_TRUE(contains(c0, "Сборник <i>ранних</i> рассказов."));
  EXPECT_TRUE(contains(c0, "<blockquote class=\"epigraph\">"));
  EXPECT_TRUE(contains(c0, "<p class=\"text-author\"><i>А. Ч.</i></p>"));
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c001.xhtml"), "<h2>Часть первая</h2>"));
  const std::string& c2 = epub.entries.at("OEBPS/c002.xhtml");
  EXPECT_TRUE(contains(c2, "<h3>Толстый и тонкий</h3>"));
  EXPECT_TRUE(contains(c2, "<b>встретились</b>"));
  EXPECT_TRUE(contains(c2, "<h4>Сцена</h4>"));
  EXPECT_TRUE(contains(c2, "<p class=\"image\"><img src=\"images/img1.png\" alt=\"\"/></p>"));
  const std::string& c3 = epub.entries.at("OEBPS/c003.xhtml");
  EXPECT_TRUE(contains(c3, "<p class=\"v\">Строка раз</p>"));
  EXPECT_TRUE(contains(c3, "<blockquote class=\"cite\">"));
  EXPECT_TRUE(contains(c3, "<p class=\"subtitle\"><b>* * *</b></p>"));
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c004.xhtml"), "Текст сноски."));
  EXPECT_EQ(epub.entries.count("OEBPS/c005.xhtml"), 0u);

  const std::string& opf = epub.entries.at("OEBPS/content.opf");
  for (int i = 0; i < 5; i++) EXPECT_TRUE(contains(opf, "<itemref idref=\"c" + std::to_string(i) + "\"/>"));
}

TEST_F(Fb2ConverterTest, LinksResolveToTheChapterHoldingTheTarget) {
  fake::add("/b.fb2", richBook());
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  const Archive epub = readArchive("/out.epub");
  ASSERT_TRUE(epub.ok);
  // Forward, into the notes body the text has not reached yet.
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c002.xhtml"), "<a href=\"c004.xhtml#n1\">[1]</a>"));
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c004.xhtml"), "id=\"n1\""));
  // Backward, to a section that opened its chapter.
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c003.xhtml"), "<a href=\"c001.xhtml#s1\">!</a>"));
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c001.xhtml"), "id=\"s1\""));
  // Outside the book: text, no link the reader would try to follow.
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c003.xhtml"), " и сайт."));
}

TEST_F(Fb2ConverterTest, ContentsNestBySectionDepth) {
  fake::add("/b.fb2", richBook());
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  const std::string ncx = readArchive("/out.epub").entries.at("OEBPS/toc.ncx");
  const size_t part = ncx.find("<text>Часть первая</text>");
  const size_t tolstoy = ncx.find("<text>Толстый и тонкий</text>");
  const size_t scene = ncx.find("<text>Сцена</text>");
  const size_t notes = ncx.find("<text>Примечания</text>");
  ASSERT_NE(part, std::string::npos);
  ASSERT_NE(tolstoy, std::string::npos);
  ASSERT_NE(scene, std::string::npos);
  ASSERT_NE(notes, std::string::npos);
  // Nested: no navPoint closes between a part and its first section.
  EXPECT_EQ(ncx.substr(part, tolstoy - part).find("</navPoint>"), std::string::npos);
  EXPECT_TRUE(contains(ncx, "<content src=\"c002.xhtml#fb2s3\"/>") || contains(ncx, "Сцена"));
  EXPECT_TRUE(contains(ncx, "<content src=\"c004.xhtml\"/>"));
  // The poem title is not a contents entry.
  EXPECT_FALSE(contains(ncx, "Песня"));
}

TEST_F(Fb2ConverterTest, DecodesImagesExactly) {
  fake::add("/b.fb2", richBook());
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  const Archive epub = readArchive("/out.epub");
  EXPECT_EQ(epub.entries.at("OEBPS/images/img0.jpg"), JPEG_BYTES);
  const std::string& png = epub.entries.at("OEBPS/images/img1.png");
  ASSERT_EQ(png.size(), 70u);
  EXPECT_EQ(png.substr(1, 3), "PNG");
}

std::string toCp1251(const std::string& utf8) {
  std::string out;
  for (size_t i = 0; i < utf8.size(); i++) {
    const auto c = static_cast<uint8_t>(utf8[i]);
    if (c < 0x80) {
      out += static_cast<char>(c);
      continue;
    }
    const uint32_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(utf8[++i]) & 0x3F);
    if (cp >= 0x410 && cp <= 0x44F)
      out += static_cast<char>(0xC0 + (cp - 0x410));
    else if (cp == 0x401)
      out += static_cast<char>(0xA8);
    else if (cp == 0x451)
      out += static_cast<char>(0xB8);
    else
      out += '?';
  }
  return out;
}

const std::string SMALL_BOOK_BODY =
    "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
    "<description><title-info><book-title>Ёлка</book-title></title-info></description>"
    "<body><section><p>Съешь же ещё этих мягких французских булок.</p></section></body></FictionBook>";

TEST_F(Fb2ConverterTest, ReadsDeclaredWindows1251) {
  fake::add("/b.fb2", toCp1251("<?xml version=\"1.0\" encoding=\"windows-1251\"?>" + SMALL_BOOK_BODY));
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  const Archive epub = readArchive("/out.epub");
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c000.xhtml"), "Съешь же ещё этих мягких французских булок."));
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/content.opf"), "<dc:title>Ёлка</dc:title>"));
  // No <lang>: a Cyrillic title says Russian.
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/content.opf"), "<dc:language>ru</dc:language>"));
}

TEST_F(Fb2ConverterTest, SniffsUndeclaredWindows1251) {
  fake::add("/b.fb2", toCp1251("<?xml version=\"1.0\"?>" + SMALL_BOOK_BODY));
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  EXPECT_TRUE(contains(readArchive("/out.epub").entries.at("OEBPS/c000.xhtml"), "французских булок"));
}

TEST_F(Fb2ConverterTest, KeepsWhatCameBeforeABreak) {
  fake::add("/b.fb2",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?><FictionBook><description><title-info>"
            "<book-title>Broken</book-title></title-info></description><body>"
            "<section><title><p>One</p></title><p>First chapter.</p></section>"
            "<section><p>Second & broken</p></section></body></FictionBook>");
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  const Archive epub = readArchive("/out.epub");
  ASSERT_TRUE(epub.ok);
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c000.xhtml"), "First chapter."));
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/c000.xhtml"), "</body></html>"));
  EXPECT_TRUE(contains(epub.entries.at("OEBPS/content.opf"), "<itemref idref=\"c0\"/>"));
}

TEST_F(Fb2ConverterTest, RefusesAFileWithNoBook) {
  fake::add("/b.fb2", "this is not xml at all");
  EXPECT_FALSE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  EXPECT_FALSE(Storage.exists("/out.epub"));
}

TEST_F(Fb2ConverterTest, EscapesTextForXhtml) {
  fake::add("/b.fb2",
            "<?xml version=\"1.0\"?><FictionBook><body><section><p>a &lt; b &amp;&amp; c &gt; d</p>"
            "</section></body></FictionBook>");
  ASSERT_TRUE(fb2::convertToEpub("/b.fb2", "/out.epub"));
  EXPECT_TRUE(contains(readArchive("/out.epub").entries.at("OEBPS/c000.xhtml"), "a &lt; b &amp;&amp; c &gt; d"));
}

}  // namespace
