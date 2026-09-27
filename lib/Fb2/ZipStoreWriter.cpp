#include "ZipStoreWriter.h"

#include <Logging.h>
#include <MinizConfig.h>

#include <cstring>

namespace fb2 {

namespace {
constexpr uint32_t LOCAL_HEADER_SIG = 0x04034b50;
constexpr uint32_t CENTRAL_HEADER_SIG = 0x02014b50;
constexpr uint32_t END_OF_CENTRAL_SIG = 0x06054b50;
constexpr uint16_t ZIP_VERSION = 20;
// 1980-01-01 00:00, the earliest DOS date: the archive is a cache, not a record.
constexpr uint16_t DOS_TIME = 0;
constexpr uint16_t DOS_DATE = (1 << 5) | 1;
// Offset of the CRC field inside a local header.
constexpr uint32_t LOCAL_CRC_OFFSET = 14;
}  // namespace

bool ZipStoreWriter::open(const std::string& path) {
  entries_.clear();
  inEntry_ = false;
  failed_ = false;
  position_ = 0;
  buffered_ = 0;
  if (!Storage.openFileForWrite("FB2", path, file_)) {
    LOG_ERR("FB2", "Cannot create %s", path.c_str());
    failed_ = true;
    return false;
  }
  return true;
}

bool ZipStoreWriter::flushBuffer() {
  if (buffered_ == 0 || failed_) return !failed_;
  if (file_.write(buffer_, buffered_) != buffered_) {
    LOG_ERR("FB2", "Write failed at %u", static_cast<unsigned>(position_));
    failed_ = true;
    return false;
  }
  buffered_ = 0;
  return true;
}

bool ZipStoreWriter::writeRaw(const void* data, size_t len) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  while (len > 0 && !failed_) {
    const size_t room = sizeof(buffer_) - buffered_;
    const size_t chunk = len < room ? len : room;
    memcpy(buffer_ + buffered_, bytes, chunk);
    buffered_ += chunk;
    bytes += chunk;
    len -= chunk;
    position_ += static_cast<uint32_t>(chunk);
    if (buffered_ == sizeof(buffer_)) flushBuffer();
  }
  return !failed_;
}

bool ZipStoreWriter::put16(const uint16_t v) {
  const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
  return writeRaw(b, 2);
}

bool ZipStoreWriter::put32(const uint32_t v) {
  const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                        static_cast<uint8_t>(v >> 24)};
  return writeRaw(b, 4);
}

bool ZipStoreWriter::beginEntry(const std::string& name) {
  if (failed_ || inEntry_) return false;
  Entry entry;
  entry.name = name;
  entry.offset = position_;
  entries_.push_back(std::move(entry));
  put32(LOCAL_HEADER_SIG);
  put16(ZIP_VERSION);
  put16(0);  // flags
  put16(0);  // method: stored
  put16(DOS_TIME);
  put16(DOS_DATE);
  put32(0);  // crc, patched by endEntry
  put32(0);  // compressed size
  put32(0);  // uncompressed size
  put16(static_cast<uint16_t>(name.size()));
  put16(0);  // extra length
  writeRaw(name.data(), name.size());
  crc_ = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, nullptr, 0));
  entrySize_ = 0;
  inEntry_ = true;
  return !failed_;
}

bool ZipStoreWriter::write(const void* data, const size_t len) {
  if (!inEntry_ || failed_) return false;
  if (len == 0) return true;
  crc_ = static_cast<uint32_t>(mz_crc32(crc_, static_cast<const unsigned char*>(data), len));
  entrySize_ += static_cast<uint32_t>(len);
  return writeRaw(data, len);
}

bool ZipStoreWriter::endEntry() {
  if (!inEntry_ || failed_) return false;
  inEntry_ = false;
  Entry& entry = entries_.back();
  entry.crc = crc_;
  entry.size = entrySize_;
  if (!flushBuffer()) return false;
  const uint8_t patch[12] = {
      static_cast<uint8_t>(crc_),
      static_cast<uint8_t>(crc_ >> 8),
      static_cast<uint8_t>(crc_ >> 16),
      static_cast<uint8_t>(crc_ >> 24),
      static_cast<uint8_t>(entrySize_),
      static_cast<uint8_t>(entrySize_ >> 8),
      static_cast<uint8_t>(entrySize_ >> 16),
      static_cast<uint8_t>(entrySize_ >> 24),
      static_cast<uint8_t>(entrySize_),
      static_cast<uint8_t>(entrySize_ >> 8),
      static_cast<uint8_t>(entrySize_ >> 16),
      static_cast<uint8_t>(entrySize_ >> 24),
  };
  if (!file_.seekSet(entry.offset + LOCAL_CRC_OFFSET) || file_.write(patch, sizeof(patch)) != sizeof(patch) ||
      !file_.seekSet(position_)) {
    LOG_ERR("FB2", "Cannot patch the header of %s", entry.name.c_str());
    failed_ = true;
    return false;
  }
  return true;
}

bool ZipStoreWriter::finish() {
  if (inEntry_) endEntry();
  if (failed_) {
    abandon();
    return false;
  }
  const uint32_t centralStart = position_;
  for (const Entry& entry : entries_) {
    put32(CENTRAL_HEADER_SIG);
    put16(ZIP_VERSION);  // made by
    put16(ZIP_VERSION);  // needed
    put16(0);            // flags
    put16(0);            // method
    put16(DOS_TIME);
    put16(DOS_DATE);
    put32(entry.crc);
    put32(entry.size);
    put32(entry.size);
    put16(static_cast<uint16_t>(entry.name.size()));
    put16(0);  // extra
    put16(0);  // comment
    put16(0);  // disk
    put16(0);  // internal attributes
    put32(0);  // external attributes
    put32(entry.offset);
    writeRaw(entry.name.data(), entry.name.size());
  }
  const uint32_t centralSize = position_ - centralStart;
  put32(END_OF_CENTRAL_SIG);
  put16(0);
  put16(0);
  put16(static_cast<uint16_t>(entries_.size()));
  put16(static_cast<uint16_t>(entries_.size()));
  put32(centralSize);
  put32(centralStart);
  put16(0);
  const bool ok = flushBuffer();
  file_.flush();
  file_.close();
  return ok;
}

void ZipStoreWriter::abandon() {
  if (file_) file_.close();
  inEntry_ = false;
  failed_ = true;
}

}  // namespace fb2
