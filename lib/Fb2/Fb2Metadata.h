#pragma once

#include <string>

class ZipFile;

namespace fb2 {

struct Metadata {
  std::string title;
  // Every author of the book, "First Middle Last", joined with ", ".
  std::string author;
  // The book's <lang>, or a guess from its text when it has none.
  std::string language;
};

// Title, author and language of an .fb2 or .fb2.zip book, read from its
// <description> without touching the text. False when the file is not FB2.
bool readMetadata(const std::string& path, Metadata& out);

// The first .fb2 entry inside an .fb2.zip, or empty.
std::string fb2EntryName(ZipFile& zip);

}  // namespace fb2
