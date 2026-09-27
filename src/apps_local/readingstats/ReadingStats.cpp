#include "ReadingStats.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "CrossPointState.h"
#include "RecentBooksStore.h"

namespace readstats {
namespace {

constexpr const char* kPath = "/.crosspoint/reading-stats.bin";
constexpr const char* kTmpPath = "/.crosspoint/reading-stats.bin.tmp";
constexpr const char* kBadPath = "/.crosspoint/reading-stats.bin.bad";

// Any wall clock below this (2023-11-14) has not been set: a flat battery
// clears the RTC and time() restarts near the epoch.
constexpr time_t kClockFloor = 1700000000;

// While a book is open the file is rewritten at most this often; leaving the
// reader writes it at once.
constexpr uint32_t kSaveEveryMs = 2 * 60 * 1000;

std::mutex gMutex;
Store gStore;
Tracker gTracker(gStore);
bool gLoaded = false;
uint32_t gLastSave = 0;

void load() {
  gLoaded = true;
  if (!Storage.exists(kPath)) return;
  HalFile file;
  if (!Storage.openFileForRead("RSTAT", kPath, file)) return;
  const size_t size = file.size();
  auto bytes = makeUniqueNoThrow<uint8_t[]>(size > 0 ? size : 1);
  if (!bytes) {
    LOG_ERR("RSTAT", "OOM: %u bytes of stats", static_cast<unsigned>(size));
    return;
  }
  const int got = file.read(bytes.get(), size);
  file = HalFile{};
  if (got != static_cast<int>(size) || !gStore.deserialize(bytes.get(), size)) {
    // Kept aside rather than overwritten: the next save would otherwise
    // replace whatever history is still recoverable from it.
    LOG_ERR("RSTAT", "Unreadable stats file; moved to %s", kBadPath);
    Storage.remove(kBadPath);
    Storage.rename(kPath, kBadPath);
    return;
  }
  LOG_INF("RSTAT", "Loaded %u books, %u days", static_cast<unsigned>(gStore.books.size()),
          static_cast<unsigned>(gStore.days.size()));
}

void save(std::unique_lock<std::mutex>& lock) {
  const std::vector<uint8_t> bytes = gStore.serialize();
  gTracker.markClean();
  gLastSave = millis();
  lock.unlock();

  HalFile file;
  if (!Storage.openFileForWrite("RSTAT", kTmpPath, file)) return;
  const bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  file = HalFile{};
  if (!ok) {
    LOG_ERR("RSTAT", "Short write to %s; the previous file is left alone", kTmpPath);
    Storage.remove(kTmpPath);
    lock.lock();
    gTracker.markDirty();
    return;
  }
  Storage.remove(kPath);
  Storage.rename(kTmpPath, kPath);
}

}  // namespace

int32_t today() {
  const time_t now = time(nullptr);
  if (now < kClockFloor) return kNoDay;
  struct tm local{};
  localtime_r(&now, &local);
  return dayFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

void onReaderStatusBar(const int currentPage, const int pageCount, const float bookPercent, char* out,
                       const size_t cap) {
  out[0] = '\0';
  std::lock_guard<std::mutex> guard(gMutex);
  if (!gLoaded) return;

  const std::string& path = APP_STATE.openEpubPath;
  if (path.empty()) return;
  const char* title = "";
  const char* author = "";
  const auto& recent = RECENT_BOOKS.getBooks();
  if (!recent.empty() && recent.front().path == path) {
    title = recent.front().title.c_str();
    author = recent.front().author.c_str();
  }

  Position pos;
  pos.page = currentPage;
  pos.pageCount = pageCount;
  pos.percent = bookPercent;
  gTracker.pageShown(millis(), today(), path.c_str(), title, author, pos);

  const int32_t seconds = gTracker.secondsLeft(pos, gStore.settings.timeLeft);
  if (seconds < 0) return;
  char duration[12];
  formatShort(duration, sizeof(duration), static_cast<uint32_t>(seconds));
  snprintf(out, cap, "%s left", duration);
}

void service(const bool readerActive) {
  std::unique_lock<std::mutex> lock(gMutex);
  if (!gLoaded) load();
  const uint32_t now = millis();
  if (!readerActive) gTracker.away(now);
  gTracker.poll(now, today());
  if (!gTracker.dirty()) return;
  if (readerActive && now - gLastSave < kSaveEveryMs) return;
  save(lock);
}

void beforeSleep(const bool fromTimeout) {
  std::unique_lock<std::mutex> lock(gMutex);
  if (!gLoaded) return;
  gTracker.sleep(millis(), today(), fromTimeout);
  if (gTracker.dirty()) save(lock);
}

void saveNow() {
  std::unique_lock<std::mutex> lock(gMutex);
  if (gLoaded && gTracker.dirty()) save(lock);
}

Access::Access() : store(gStore), tracker(gTracker), guard_(gMutex) {}

}  // namespace readstats
