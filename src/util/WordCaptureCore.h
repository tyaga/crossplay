#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The pure half of saving a looked-up word for Anki: naming, the file header,
// and the sentence the word was read in. Freestanding C++17 so host-tests can
// drive it; WordCapture.cpp does the SD card side.
//
// A saved word is one file, /study/.inbox/<lang>/<stem>.txt: a header of
// "key: value" lines, a blank line, then the raw dictionary entry. One file per
// word makes "already saved" a single exists() and removal after import a
// single delete. tools_local/study/words_to_anki.py reads them.
namespace word_capture {

struct Meta {
  std::string headword;    // as the dictionary stores it: the card's front
  std::string lookedUp;    // as it appeared on the page, when that differs
  std::string dictionary;  // folder under /dictionaries, e.g. "nl-en"
  std::string context;     // the sentence it was read in
  std::string source;      // the book's title
  bool html = false;       // the entry is HTML (sametypesequence=h)
};

// ISO 639 code of the words a dictionary looks up, from its folder name:
// "nl-en" and "nl-ru" are both Dutch, "en-ru-bars" is English. Empty when
// the name does not start with a two- or three-letter code, which is what
// disables saving for that dictionary.
std::string languageOf(const std::string& dictionaryFolder);

// File name (no extension) for a headword. Lowercased ASCII so "House" and
// "house" are one word; bytes a FAT name cannot hold become '_'; capped at
// kMaxStemBytes without splitting a UTF-8 sequence.
constexpr size_t kMaxStemBytes = 64;
std::string fileStem(const std::string& headword);

// The header block, terminated by the blank line. Values are single-line:
// a newline inside a book title would otherwise start a new key.
std::string header(const Meta& meta);

// One laid-out token of the page, punctuation included: the layout keeps "."
// and "," as tokens of their own, so a list of only the selectable words has
// no sentence ends in it.
struct Token {
  const char* text;
  unsigned row;         // line on the page, for rejoining a hyphenated break
  bool paragraphStart;  // first token after a paragraph or heading break
};

// The sentence around tokens[index]: back to the previous sentence end or
// paragraph start, forward to the next, punctuation glued to its word, capped
// at maxBytes on a token boundary without losing the token at `index`. A word
// broken across lines with a trailing hyphen is rejoined. Empty when index is
// out of range.
std::string contextSentence(const std::vector<Token>& tokens, size_t index, size_t maxBytes);

}  // namespace word_capture
