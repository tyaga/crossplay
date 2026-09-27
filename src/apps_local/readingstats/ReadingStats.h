#pragma once

// The device side of reading statistics: the three hooks upstream code calls,
// the stats file, and locked access for the READING app.
//
// The status bar hook runs on the render task and the other two on the main
// loop, so everything goes through one mutex. The file is written only from
// the main loop.

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "StatsCore.h"

namespace readstats {

// BaseTheme::drawStatusBar, on every paint of a reader page. Records the page
// view and writes the time-left label into `out`, or an empty string.
void onReaderStatusBar(int currentPage, int pageCount, float bookPercent, char* out, size_t cap);

// Every pass of the main loop.
void service(bool readerActive);

// enterDeepSleep, for an attended sleep, before the reader is torn down.
void beforeSleep(bool fromTimeout);

// Today's day number in local time, or kNoDay while the clock is unset.
int32_t today();

class Access {
 public:
  Access();
  Store& store;
  Tracker& tracker;

 private:
  std::lock_guard<std::mutex> guard_;
};

// Writes the file now if anything changed. For the settings screen, whose
// edits should not wait for the next page turn.
void saveNow();

}  // namespace readstats
