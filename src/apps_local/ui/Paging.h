#pragma once

// Paged lists: which page a row is on, how many pages there are, and one step
// between them. Pages rather than scrolling because an e-ink panel repaints the
// whole screen either way, and a page that always starts in the same place is
// one a thumb can learn.
namespace paging {

inline int pageFor(const int selected, const int rowsPerPage) {
  if (rowsPerPage <= 0 || selected <= 0) return 0;
  return selected / rowsPerPage;
}

inline int pageCountFor(const int itemCount, const int rowsPerPage) {
  if (rowsPerPage <= 0 || itemCount <= 0) return 1;
  return (itemCount + rowsPerPage - 1) / rowsPerPage;
}

// One step of `delta` pages, wrapping at both ends: a page key that stops
// working at the last page reads as a broken key.
inline int pageStep(const int page, const int pageCount, const int delta) {
  if (pageCount <= 1) return 0;
  // Modulo of a negative left operand is negative in C++, so the step is
  // normalised into 0..pageCount-1 before it is added.
  const int step = ((delta % pageCount) + pageCount) % pageCount;
  const int from = page < 0 ? 0 : page % pageCount;
  return (from + step) % pageCount;
}

}  // namespace paging
