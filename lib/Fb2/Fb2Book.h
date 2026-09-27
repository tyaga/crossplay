#pragma once

#include <string>
#include <string_view>

#include "Fb2Converter.h"

// An FB2 book on the card is read through an EPUB made from it: the reader,
// the library and the home screen open that EPUB wherever they would open the
// book itself. The EPUB lives in /.crosspoint beside the other book caches,
// named after the book's path, and is remade when the book changes.
namespace fb2 {

// .fb2, or an .fb2.zip holding one.
bool isFb2Path(std::string_view path);

// Where the EPUB made from `fb2Path` lives. It need not exist.
std::string epubPathFor(const std::string& fb2Path);

// Whether that EPUB exists and was made from the book as it is now.
bool epubIsCurrent(const std::string& fb2Path);

// Makes the EPUB unless epubIsCurrent(). False when the book cannot be read as
// FB2; nothing is left behind then.
bool prepareEpub(const std::string& fb2Path, ProgressFn progress = nullptr, void* progressCtx = nullptr);

// The EPUB reader's cache directory for the EPUB made from `fb2Path`: where
// its pages, progress and cover thumbnails live.
std::string epubCachePathForBook(const std::string& fb2Path);

// Removes the EPUB, its record, and the EPUB's own reader cache.
void removeEpub(const std::string& fb2Path);

// Moves the EPUB and its reader cache along with a book renamed or moved from
// `oldPath` to `newPath`, so reading position and pages survive. Not having an
// EPUB yet is not a failure.
bool moveEpub(const std::string& oldPath, const std::string& newPath);

}  // namespace fb2
