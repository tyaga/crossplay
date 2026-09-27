#include <cstdio>
#include <string>
#include <vector>

#include "../../src/util/WordCaptureCore.h"

using word_capture::Token;

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const char* what) {
  checks++;
  if (!ok) {
    failures++;
    std::printf("  FAIL %s\n", what);
  }
}

void checkEq(const std::string& got, const std::string& want, const char* what) {
  checks++;
  if (got != want) {
    failures++;
    std::printf("  FAIL %s\n       got:  [%s]\n       want: [%s]\n", what, got.c_str(), want.c_str());
  }
}

// Tokens as the layout emits them: one row per inner vector, punctuation as
// tokens of its own, `breakBefore` marking the rows that follow a paragraph gap.
std::vector<Token> page(const std::vector<std::vector<const char*>>& rows, const std::vector<bool>& breakBefore = {}) {
  std::vector<Token> out;
  for (size_t r = 0; r < rows.size(); r++) {
    for (size_t i = 0; i < rows[r].size(); i++) {
      const bool gap = r < breakBefore.size() && breakBefore[r] && i == 0;
      out.push_back({rows[r][i], static_cast<unsigned>(r), gap});
    }
  }
  return out;
}

size_t find(const std::vector<Token>& tokens, const char* text) {
  for (size_t i = 0; i < tokens.size(); i++) {
    if (std::string(tokens[i].text) == text) return i;
  }
  return tokens.size();
}

}  // namespace

int main() {
  // --- languageOf ---------------------------------------------------------
  checkEq(word_capture::languageOf("nl-en"), "nl", "a pair names the looked-up language first");
  checkEq(word_capture::languageOf("en-ru-bars"), "en", "a suffix after the pair is ignored");
  checkEq(word_capture::languageOf("is-ru"), "is", "Icelandic");
  checkEq(word_capture::languageOf("DE_en"), "de", "upper case and underscore are accepted");
  checkEq(word_capture::languageOf("deu-eng"), "deu", "three-letter codes");
  checkEq(word_capture::languageOf("webster"), "", "a name with no code disables saving");
  checkEq(word_capture::languageOf("x-en"), "", "one letter is not a code");
  checkEq(word_capture::languageOf("en"), "en", "a bare code is a code");
  checkEq(word_capture::languageOf(""), "", "no dictionary, no language");

  // --- fileStem -----------------------------------------------------------
  checkEq(word_capture::fileStem("House"), "house", "case folds, so House and house are one word");
  checkEq(word_capture::fileStem("þýska"), "þýska", "non-ASCII letters are kept as they are");
  checkEq(word_capture::fileStem("AC/DC"), "ac_dc", "a slash cannot be in a file name");
  checkEq(word_capture::fileStem("what?"), "what_", "neither can a question mark");
  checkEq(word_capture::fileStem(".hidden"), "hidden", "a leading dot would hide the file from the import");
  checkEq(word_capture::fileStem("etc."), "etc", "FAT refuses a trailing dot");
  checkEq(word_capture::fileStem("..."), "_", "a name of nothing but dots still names a file");
  {
    std::string longWord;
    for (int i = 0; i < 40; i++) longWord += "ä";  // two bytes each
    const std::string stem = word_capture::fileStem(longWord);
    check(stem.size() <= word_capture::kMaxStemBytes, "a long headword is capped");
    check(stem.size() % 2 == 0, "the cap does not split a two-byte character");
  }

  // --- header -------------------------------------------------------------
  {
    word_capture::Meta meta;
    meta.headword = "huis";
    meta.lookedUp = "huizen";
    meta.dictionary = "nl-en";
    meta.context = "De huizen\nstaan aan het water.";
    meta.source = "Dik Trom";
    meta.html = true;
    checkEq(word_capture::header(meta),
            "word: huis\nlooked-up: huizen\ndictionary: nl-en\nlanguage: nl\nformat: html\n"
            "context: De huizen staan aan het water.\nsource: Dik Trom\n\n",
            "the header the importer parses, one value per line");
    meta.lookedUp = "huis";
    meta.context = "";
    meta.html = false;
    checkEq(word_capture::header(meta),
            "word: huis\ndictionary: nl-en\nlanguage: nl\nformat: text\nsource: Dik Trom\n\n",
            "a looked-up form equal to the headword and an empty context are left out");
  }

  // --- contextSentence ----------------------------------------------------
  {
    const auto tokens = page({{"It", "was", "late", "."},
                              {"The", "device", "hummed", ",", "then"},
                              {"stopped", "."},
                              {"Nobody", "noticed", "."}});
    checkEq(word_capture::contextSentence(tokens, find(tokens, "device"), 240), "The device hummed, then stopped.",
            "the sentence spans rows and stops at the full stops either side");
    checkEq(word_capture::contextSentence(tokens, find(tokens, "late"), 240), "It was late.",
            "the first sentence of the page starts at the page");
  }
  {
    const auto tokens =
        page({{"2", ".", "Power", "&", "Startup"}, {"To", "turn", "the", "device", "on", "."}}, {false, true});
    checkEq(word_capture::contextSentence(tokens, find(tokens, "device"), 240), "To turn the device on.",
            "a heading above a paragraph break is not part of the sentence");
  }
  {
    const auto tokens = page({{"He", "said", "“", "wait", "”", "(", "twice", ")", "!"}});
    checkEq(word_capture::contextSentence(tokens, find(tokens, "wait"), 240), "He said “wait” (twice)!",
            "quotes and brackets hug the words they enclose");
  }
  {
    const auto tokens = page({{"A", "well-known", "ordi-"}, {"nary", "day", "."}});
    checkEq(word_capture::contextSentence(tokens, find(tokens, "day"), 240), "A well-known ordinary day.",
            "a word broken across lines is rejoined, a hyphen inside a line is kept");
  }
  {
    const auto tokens = page({{"One", "two", "three", "four", "five", "six", "seven", "eight", "."}});
    const std::string cut = word_capture::contextSentence(tokens, find(tokens, "six"), 20);
    check(cut.size() <= 20, "a long sentence is capped");
    check(cut.find("six") != std::string::npos, "the cap keeps the word the sentence is the context of");
  }
  {
    const auto tokens = page({{"Wat", "is", "dat", "?"}, {"Een", "huis", "…"}, {"Of", "niet", "."}});
    checkEq(word_capture::contextSentence(tokens, find(tokens, "huis"), 240), "Een huis…",
            "a question mark and an ellipsis end sentences too");
  }
  checkEq(word_capture::contextSentence(page({{"a"}}), 5, 240), "", "an index past the end gives nothing");

  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
