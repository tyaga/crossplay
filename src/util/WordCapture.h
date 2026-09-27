#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "WordCaptureCore.h"

// The SD card half of saving a looked-up word for Anki. See WordCaptureCore.h
// for the file format.
namespace word_capture {

enum class Result : uint8_t { Saved, AlreadySaved, Failed };

// Keep the raw entry being shown, so a later save() has it after the viewer
// has laid it out and freed the original. Only the first kMaxEntryBytes are
// kept: the senses a card is built from come first, and usage examples and
// etymology make up the long tail.
constexpr size_t kMaxEntryBytes = 8 * 1024;
bool rememberEntry(const std::string& raw);

// Write the remembered entry, under meta's header, to the inbox. AlreadySaved
// when this headword is in the inbox for this language, and nothing is written.
Result save(const Meta& meta);

// A saved word as a sync sends it: its path under the inbox ("nl/huis.txt")
// and its file, trimmed. The header comes first in the file, so a trim loses
// only the tail of the entry -- examples and later senses.
struct Outgoing {
  std::string file;
  std::string bytes;
};

// Saved words for one sync: at most maxFiles, each trimmed to maxBytesEach,
// maxTotal bytes in all. What does not fit waits for the next sync.
std::vector<Outgoing> collectForSync(size_t maxFiles, size_t maxBytesEach, size_t maxTotal);

// Delete the inbox files the bridge acknowledged. Names it did not send are
// ignored rather than trusted as paths.
void removeSent(const std::vector<std::string>& files);

}  // namespace word_capture
