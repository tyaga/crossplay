#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fb2 {

// Writes a ZIP archive of STORED (uncompressed) entries, one open entry at a
// time. Stored because the output is an EPUB that is read back on this device:
// deflating it would cost time on every page for no space the card lacks.
//
// Each entry's CRC and size are only known once its data is written, so the
// local header is written with zeros and patched in place when the entry ends.
class ZipStoreWriter {
 public:
  bool open(const std::string& path);
  bool beginEntry(const std::string& name);
  bool write(const void* data, size_t len);
  bool write(const std::string& text) { return write(text.data(), text.size()); }
  bool endEntry();
  // Writes the central directory and closes the file. False leaves a broken
  // archive behind; the caller removes it.
  bool finish();
  void abandon();
  bool failed() const { return failed_; }

 private:
  struct Entry {
    std::string name;
    uint32_t offset = 0;
    uint32_t crc = 0;
    uint32_t size = 0;
  };

  bool flushBuffer();
  bool writeRaw(const void* data, size_t len);
  bool put16(uint16_t v);
  bool put32(uint32_t v);

  HalFile file_;
  std::vector<Entry> entries_;
  bool inEntry_ = false;
  bool failed_ = false;
  uint32_t position_ = 0;
  uint32_t crc_ = 0;
  uint32_t entrySize_ = 0;
  uint8_t buffer_[2048];
  size_t buffered_ = 0;
};

}  // namespace fb2
