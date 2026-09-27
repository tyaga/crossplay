#pragma once

#include <string>

namespace fb2 {

// Called with 0..100 while a conversion runs.
using ProgressFn = void (*)(void* ctx, int percent);

// Rewrites the FictionBook 2 file at `fb2Path` as an EPUB at `epubPath`, so the
// EPUB reader -- pagination, dictionary, footnotes, contents, covers -- reads
// it like any other book.
//
// Two passes over the file. The first learns what the second needs before it
// reaches it: which chapter every id lands in (note links point forward, into
// a body that comes last) and the type of every embedded image (the images
// come after the text that shows them, and the reader picks a decoder by file
// extension). The second writes the chapters, then the images, then the
// package files.
//
// Chapters split at the main body's first two levels of <section>; each
// further body (notes, comments) is one chapter of its own. A file that stops
// parsing part-way converts up to the fault rather than failing whole: FB2 in
// the wild is often hand-edited, and the pages before a stray '&' are still a
// book.
bool convertToEpub(const std::string& fb2Path, const std::string& epubPath, ProgressFn progress = nullptr,
                   void* progressCtx = nullptr);

}  // namespace fb2
