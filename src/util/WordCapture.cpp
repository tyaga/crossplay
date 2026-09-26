#include "WordCapture.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace word_capture {

namespace {

constexpr char kInboxDir[] = "/study/.inbox";
constexpr char kEntryPath[] = "/study/.inbox/.entry";

}  // namespace

bool rememberEntry(const std::string& raw) {
  if (!Storage.ensureDirectoryExists(kInboxDir)) {
    LOG_ERR("WORDCAP", "cannot create %s", kInboxDir);
    return false;
  }
  HalFile file;
  if (!Storage.openFileForWrite("WORDCAP", kEntryPath, file)) return false;
  const size_t n = raw.size() < kMaxEntryBytes ? raw.size() : kMaxEntryBytes;
  if (file.write(raw.data(), n) != n) {
    LOG_ERR("WORDCAP", "short write to %s", kEntryPath);
    return false;
  }
  return true;
}

Result save(const Meta& meta) {
  const std::string lang = languageOf(meta.dictionary);
  if (lang.empty() || meta.headword.empty()) return Result::Failed;

  char dir[48];
  std::snprintf(dir, sizeof(dir), "%s/%s", kInboxDir, lang.c_str());
  if (!Storage.ensureDirectoryExists(dir)) {
    LOG_ERR("WORDCAP", "cannot create %s", dir);
    return Result::Failed;
  }
  const std::string path = std::string(dir) + "/" + fileStem(meta.headword) + ".txt";
  if (Storage.exists(path.c_str())) return Result::AlreadySaved;

  HalFile entry;
  if (!Storage.openFileForRead("WORDCAP", kEntryPath, entry)) return Result::Failed;

  // Written beside the target and renamed, so an import never picks up a
  // header without its entry.
  const std::string temp = path + ".part";
  {
    HalFile out;
    if (!Storage.openFileForWrite("WORDCAP", temp.c_str(), out)) return Result::Failed;
    const std::string head = header(meta);
    if (out.write(head.data(), head.size()) != head.size()) {
      LOG_ERR("WORDCAP", "short write to %s", temp.c_str());
      return Result::Failed;
    }
    uint8_t buf[256];
    for (;;) {
      const int n = entry.read(buf, sizeof(buf));
      if (n <= 0) break;
      if (out.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
        LOG_ERR("WORDCAP", "short write to %s", temp.c_str());
        return Result::Failed;
      }
    }
  }
  if (!Storage.rename(temp.c_str(), path.c_str())) {
    LOG_ERR("WORDCAP", "cannot rename %s into place", temp.c_str());
    Storage.remove(temp.c_str());
    return Result::Failed;
  }
  LOG_INF("WORDCAP", "saved %s", path.c_str());
  return Result::Saved;
}

std::vector<Outgoing> collectForSync(const size_t maxFiles, const size_t maxBytesEach, const size_t maxTotal) {
  std::vector<Outgoing> out;
  HalFile root = Storage.open(kInboxDir);
  if (!root.isOpen() || !root.isDirectory()) return out;
  size_t total = 0;
  for (;;) {
    HalFile langDir = root.openNextFile();
    if (!langDir.isOpen()) break;
    char lang[8];
    langDir.getName(lang, sizeof(lang));
    if (!langDir.isDirectory() || lang[0] == '.' || languageOf(lang) != lang) continue;
    for (;;) {
      if (out.size() >= maxFiles || total >= maxTotal) return out;
      HalFile entry = langDir.openNextFile();
      if (!entry.isOpen()) break;
      char name[96];
      entry.getName(name, sizeof(name));
      const size_t len = std::strlen(name);
      if (entry.isDirectory() || name[0] == '.' || len < 5 || std::strcmp(name + len - 4, ".txt") != 0) continue;
      size_t want = entry.size();
      if (want > maxBytesEach) want = maxBytesEach;
      if (want == 0 || total + want > maxTotal) continue;
      Outgoing word;
      word.file = std::string(lang) + "/" + name;
      word.bytes.resize(want);
      const int got = entry.read(&word.bytes[0], want);
      if (got <= 0) continue;
      word.bytes.resize(static_cast<size_t>(got));
      total += word.bytes.size();
      out.push_back(std::move(word));
    }
  }
  return out;
}

void removeSent(const std::vector<std::string>& files) {
  for (const auto& file : files) {
    const size_t slash = file.find('/');
    if (slash == std::string::npos || file.find("..") != std::string::npos ||
        file.find('/', slash + 1) != std::string::npos) {
      continue;
    }
    const std::string path = std::string(kInboxDir) + "/" + file;
    if (!Storage.remove(path.c_str())) LOG_ERR("WORDCAP", "cannot remove %s", path.c_str());
  }
}

}  // namespace word_capture
