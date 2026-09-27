#pragma once

#include <string>

#include "Epub.h"

// FB2 books take their metadata from the same fake table as EPUBs.
namespace fb2 {

struct Metadata {
  std::string title;
  std::string author;
  std::string language;
};

inline bool readMetadata(const std::string& path, Metadata& out) {
  ++fake::parses;
  const auto& metadata = bookMetadata[path];
  if (!metadata.success) return false;
  out.title = metadata.title;
  out.author = metadata.author;
  return true;
}

}  // namespace fb2
