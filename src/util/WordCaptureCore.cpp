#include "WordCaptureCore.h"

#include <cstring>

namespace word_capture {

namespace {

bool isAsciiLower(char c) { return c >= 'a' && c <= 'z'; }

bool endsSentence(const char* word) {
  const size_t n = std::strlen(word);
  if (n == 0) return false;
  // Closing quotes and brackets after the stop: "... end." ends, and so does
  // "... end.”" and "... end.)".
  size_t i = n;
  while (i > 0) {
    const unsigned char c = static_cast<unsigned char>(word[i - 1]);
    if (c == '"' || c == '\'' || c == ')' || c == ']') {
      i--;
      continue;
    }
    // U+201D and U+2019, right quotes: E2 80 9D / E2 80 99.
    if (i >= 3 && static_cast<unsigned char>(word[i - 3]) == 0xE2 && static_cast<unsigned char>(word[i - 2]) == 0x80 &&
        (c == 0x9D || c == 0x99)) {
      i -= 3;
      continue;
    }
    break;
  }
  if (i == 0) return false;
  const char last = word[i - 1];
  if (last == '.' || last == '!' || last == '?') return true;
  // U+2026 horizontal ellipsis: E2 80 A6.
  return i >= 3 && static_cast<unsigned char>(word[i - 3]) == 0xE2 && static_cast<unsigned char>(word[i - 2]) == 0x80 &&
         static_cast<unsigned char>(word[i - 1]) == 0xA6;
}

bool endsWithHyphen(const std::string& s) { return s.size() > 1 && s.back() == '-'; }

// Punctuation that sits against the word before it: "word," "word." "word)".
bool gluesLeft(const char* token) {
  const unsigned char c = static_cast<unsigned char>(token[0]);
  if (c != 0 && std::strchr(".,;:!?)]}%", c) != nullptr) return true;
  // U+201D, U+2019 right quotes and U+2026 ellipsis: E2 80 9D / 99 / A6.
  if (c == 0xE2 && static_cast<unsigned char>(token[1]) == 0x80) {
    const unsigned char d = static_cast<unsigned char>(token[2]);
    return d == 0x9D || d == 0x99 || d == 0xA6;
  }
  // U+00BB right guillemet: C2 BB.
  return c == 0xC2 && static_cast<unsigned char>(token[1]) == 0xBB;
}

// Punctuation that sits against the word after it: "(word" "“word".
bool gluesRight(const std::string& piece) {
  if (piece.size() == 1) return std::strchr("([{", piece[0]) != nullptr;
  // U+201C, U+2018 left quotes: E2 80 9C / 98; U+00AB left guillemet: C2 AB.
  if (piece.size() == 3 && static_cast<unsigned char>(piece[0]) == 0xE2 &&
      static_cast<unsigned char>(piece[1]) == 0x80) {
    const unsigned char d = static_cast<unsigned char>(piece[2]);
    return d == 0x9C || d == 0x98;
  }
  return piece.size() == 2 && static_cast<unsigned char>(piece[0]) == 0xC2 &&
         static_cast<unsigned char>(piece[1]) == 0xAB;
}

std::string oneLine(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) out.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : c);
  return out;
}

// Largest prefix of s no longer than max bytes that does not cut a UTF-8
// sequence: continuation bytes are 10xxxxxx.
size_t utf8Prefix(const std::string& s, size_t max) {
  if (s.size() <= max) return s.size();
  size_t n = max;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
  return n;
}

}  // namespace

std::string languageOf(const std::string& dictionaryFolder) {
  size_t n = 0;
  while (n < dictionaryFolder.size() && n < 4) {
    const char c = dictionaryFolder[n];
    if (isAsciiLower(c)) {
      n++;
    } else if (c >= 'A' && c <= 'Z') {
      n++;
    } else {
      break;
    }
  }
  if (n < 2 || n > 3) return "";
  if (n < dictionaryFolder.size() && dictionaryFolder[n] != '-' && dictionaryFolder[n] != '_') return "";
  std::string code = dictionaryFolder.substr(0, n);
  for (char& c : code) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return code;
}

std::string fileStem(const std::string& headword) {
  std::string out;
  out.reserve(headword.size());
  for (const char raw : headword) {
    const unsigned char c = static_cast<unsigned char>(raw);
    if (c < 0x20 || std::strchr("/\\:*?\"<>|", c) != nullptr) {
      out.push_back('_');
    } else if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  // A leading dot would hide the file from the inbox scan, and FAT refuses a
  // name that ends in a dot or a space.
  while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());
  out.resize(utf8Prefix(out, kMaxStemBytes));
  while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
  if (out.empty()) out = "_";
  return out;
}

std::string header(const Meta& meta) {
  std::string out;
  out.reserve(160 + meta.context.size() + meta.source.size());
  const auto line = [&out](const char* key, const std::string& value) {
    if (value.empty()) return;
    out.append(key);
    out.append(": ");
    out.append(oneLine(value));
    out.push_back('\n');
  };
  line("word", meta.headword);
  if (meta.lookedUp != meta.headword) line("looked-up", meta.lookedUp);
  line("dictionary", meta.dictionary);
  line("language", languageOf(meta.dictionary));
  out.append(meta.html ? "format: html\n" : "format: text\n");
  line("context", meta.context);
  line("source", meta.source);
  out.push_back('\n');
  return out;
}

std::string contextSentence(const std::vector<Token>& tokens, size_t index, size_t maxBytes) {
  if (index >= tokens.size()) return "";
  size_t first = index;
  while (first > 0 && !tokens[first].paragraphStart && !endsSentence(tokens[first - 1].text)) first--;
  size_t last = index;
  while (last + 1 < tokens.size() && !tokens[last + 1].paragraphStart && !endsSentence(tokens[last].text)) last++;

  // Pieces first, so the cap can drop words from either end.
  std::vector<std::string> pieces;
  pieces.reserve(last - first + 1);
  size_t target = 0;
  for (size_t i = first; i <= last; i++) {
    const char* text = tokens[i].text;
    const bool hyphenBreak = !pieces.empty() && endsWithHyphen(pieces.back()) && tokens[i].row != tokens[i - 1].row;
    if (hyphenBreak) {
      pieces.back().pop_back();
      pieces.back().append(text);
    } else if (!pieces.empty() && (gluesLeft(text) || gluesRight(pieces.back()))) {
      pieces.back().append(text);
    } else {
      pieces.emplace_back(text);
    }
    if (i == index) target = pieces.size() - 1;
  }

  size_t lo = target;
  size_t hi = target;
  size_t bytes = pieces[target].size();
  // Grow outwards, preferring what follows: the rest of the sentence usually
  // says more about the word than the clause before it.
  for (;;) {
    bool grew = false;
    if (hi + 1 < pieces.size() && bytes + 1 + pieces[hi + 1].size() <= maxBytes) {
      bytes += 1 + pieces[++hi].size();
      grew = true;
    }
    if (lo > 0 && bytes + 1 + pieces[lo - 1].size() <= maxBytes) {
      bytes += 1 + pieces[--lo].size();
      grew = true;
    }
    if (!grew) break;
  }
  std::string out;
  out.reserve(bytes);
  for (size_t i = lo; i <= hi; i++) {
    if (i > lo) out.push_back(' ');
    out.append(pieces[i]);
  }
  if (out.size() > maxBytes) out.resize(utf8Prefix(out, maxBytes));
  return out;
}

}  // namespace word_capture
