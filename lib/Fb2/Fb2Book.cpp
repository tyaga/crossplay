#include "Fb2Book.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ZipFile.h>

#include <cstdio>
#include <functional>

#include "Fb2Metadata.h"

namespace fb2 {

namespace {

constexpr char CACHE_DIR[] = "/.crosspoint";
// Raised whenever the converter's output changes, so books made by an older
// one are made again rather than read with its mistakes.
constexpr int CONVERTER_VERSION = 1;

std::string stemFor(const std::string& fb2Path) {
  return std::string(CACHE_DIR) + "/fb2_" + std::to_string(std::hash<std::string>{}(fb2Path));
}

std::string recordPathFor(const std::string& fb2Path) { return stemFor(fb2Path) + ".src"; }

// What the EPUB was made from: the converter and the book's size and time.
std::string sourceStamp(const std::string& fb2Path) {
  HalFile file;
  if (!Storage.openFileForRead("FB2", fb2Path, file)) return {};
  char stamp[64];
  snprintf(stamp, sizeof(stamp), "%d %u %u\n", CONVERTER_VERSION, static_cast<unsigned>(file.fileSize()),
           static_cast<unsigned>(file.modificationTime()));
  return stamp;
}

std::string epubCachePathFor(const std::string& epubPath) {
  return std::string(CACHE_DIR) + "/epub_" + std::to_string(std::hash<std::string>{}(epubPath));
}

bool extractZipEntry(const std::string& zipPath, const std::string& outPath) {
  ZipFile zip(zipPath);
  const std::string entry = fb2EntryName(zip);
  if (entry.empty()) {
    LOG_ERR("FB2", "No .fb2 inside %s", zipPath.c_str());
    return false;
  }
  HalFile out;
  if (!Storage.openFileForWrite("FB2", outPath, out)) return false;
  const bool ok = zip.readFileToStream(entry.c_str(), out, 4096);
  out.flush();
  out.close();
  if (!ok) Storage.remove(outPath.c_str());
  return ok;
}

}  // namespace

bool isFb2Path(const std::string_view path) {
  return FsHelpers::checkFileExtension(path, ".fb2") || FsHelpers::checkFileExtension(path, ".fb2.zip");
}

std::string epubPathFor(const std::string& fb2Path) { return stemFor(fb2Path) + ".epub"; }

bool epubIsCurrent(const std::string& fb2Path) {
  const std::string epubPath = epubPathFor(fb2Path);
  const std::string recordPath = recordPathFor(fb2Path);
  if (!Storage.exists(epubPath.c_str()) || !Storage.exists(recordPath.c_str())) return false;
  const std::string stamp = sourceStamp(fb2Path);
  return !stamp.empty() && Storage.readFile(recordPath.c_str()).c_str() == stamp;
}

bool prepareEpub(const std::string& fb2Path, ProgressFn progress, void* progressCtx) {
  if (epubIsCurrent(fb2Path)) return true;
  const std::string stamp = sourceStamp(fb2Path);
  if (stamp.empty()) return false;
  // The book changed, or was never converted: pages laid out from an older
  // EPUB would point into text that is not there any more.
  removeEpub(fb2Path);

  const std::string epubPath = epubPathFor(fb2Path);
  const std::string partPath = epubPath + ".part";
  std::string source = fb2Path;
  std::string unpacked;
  if (FsHelpers::checkFileExtension(fb2Path, ".zip")) {
    unpacked = stemFor(fb2Path) + ".unpacked.fb2";
    if (!extractZipEntry(fb2Path, unpacked)) return false;
    source = unpacked;
  }

  const bool converted = convertToEpub(source, partPath, progress, progressCtx);
  if (!unpacked.empty()) Storage.remove(unpacked.c_str());
  if (!converted) {
    Storage.remove(partPath.c_str());
    return false;
  }
  if (!Storage.rename(partPath.c_str(), epubPath.c_str())) {
    LOG_ERR("FB2", "Cannot install %s", epubPath.c_str());
    Storage.remove(partPath.c_str());
    return false;
  }
  if (!Storage.writeFile(recordPathFor(fb2Path).c_str(), String(stamp.c_str()))) {
    LOG_ERR("FB2", "Cannot record the source of %s; it will be converted again", epubPath.c_str());
  }
  return true;
}

void removeEpub(const std::string& fb2Path) {
  const std::string epubPath = epubPathFor(fb2Path);
  Epub(epubPath, CACHE_DIR).clearCache();
  Storage.remove(epubPath.c_str());
  Storage.remove(recordPathFor(fb2Path).c_str());
}

bool moveEpub(const std::string& oldPath, const std::string& newPath) {
  const std::string oldEpub = epubPathFor(oldPath);
  if (!Storage.exists(oldEpub.c_str())) return true;
  const std::string newEpub = epubPathFor(newPath);
  Storage.remove(newEpub.c_str());
  Storage.remove(recordPathFor(newPath).c_str());
  if (!Storage.rename(oldEpub.c_str(), newEpub.c_str())) {
    LOG_ERR("FB2", "Cannot move %s -> %s", oldEpub.c_str(), newEpub.c_str());
    return false;
  }
  // A caller that moves book caches itself (the file browser) has moved this
  // one already.
  const std::string oldCache = epubCachePathFor(oldEpub);
  if (Storage.exists(oldCache.c_str())) {
    Epub(newEpub, CACHE_DIR).clearCache();
    Storage.rename(oldCache.c_str(), epubCachePathFor(newEpub).c_str());
  }
  // The stamp names size and time, which a rename keeps.
  Storage.rename(recordPathFor(oldPath).c_str(), recordPathFor(newPath).c_str());
  return true;
}

std::string epubCachePathForBook(const std::string& fb2Path) { return epubCachePathFor(epubPathFor(fb2Path)); }

}  // namespace fb2
