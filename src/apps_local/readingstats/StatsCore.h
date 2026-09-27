#pragma once

// Reading statistics: the page views the reader draws, turned into time read
// per day and per book, pages turned, streaks and a reading pace.
//
// Freestanding C++17 (no Arduino, no storage, no clock), so host-tests/readingstats
// drives it with synthetic timestamps. The device side, ReadingStats.cpp, feeds
// it millis() and the local day number and owns the file.
//
// Time is credited per page view, and one view is worth at most the idle
// ceiling (Settings::idleMinutes). A device left open on a page therefore adds
// minutes, not hours, and nothing has to guess when the reader looked away.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace readstats {

// Days are counted from 1970-01-01 in local time. -1 means the wall clock has
// not been set, and such time still counts towards the book but towards no day.
constexpr int32_t kNoDay = -1;

int32_t dayFromCivil(int year, int month, int day);
void civilFromDay(int32_t day, int& year, int& month, int& dayOfMonth);
// 0 = Monday.
int weekdayOf(int32_t day);
int daysInMonth(int year, int month);

// Where the reader is, as the status bar knows it.
struct Position {
  int page = 0;  // 1-based, within the chapter (TXT and XTC: within the book)
  int pageCount = 0;
  float percent = 0;  // through the whole book, 0..100
};

enum class TimeLeft : uint8_t { Off = 0, Chapter = 1, Book = 2 };

struct Settings {
  uint16_t goalMinutes = 30;  // 0: no goal, a streak day is any day with a minute of reading
  uint8_t idleMinutes = 5;
  TimeLeft timeLeft = TimeLeft::Chapter;

  uint32_t idleMs() const { return static_cast<uint32_t>(idleMinutes) * 60000u; }
  uint32_t streakMs() const { return goalMinutes > 0 ? goalMinutes * 60000u : 60000u; }
};

struct Day {
  int32_t day = kNoDay;
  uint32_t ms = 0;
  uint32_t pages = 0;
};

struct Book {
  std::string path;
  std::string title;
  std::string author;
  uint64_t ms = 0;
  uint32_t pages = 0;
  uint32_t sessions = 0;
  int32_t firstDay = kNoDay;
  int32_t lastDay = kNoDay;
  // Recency without the clock: bumped from Store::sequence whenever the book is read.
  uint32_t seen = 0;
  float percent = 0;

  // The pace sample: only views that ended in a single forward turn and stayed
  // under the idle ceiling, so jumps, skims and idle pages do not skew it.
  uint32_t paceMs = 0;
  uint32_t paceTurns = 0;
  float pacePercent = 0;
};

class Store {
 public:
  Settings settings;
  std::vector<Day> days;  // ascending by day
  std::vector<Book> books;
  uint32_t sequence = 0;

  // Matches by path first, then by title and author, so a book moved to another
  // folder keeps its history and picks up the new path.
  int findOrAdd(const char* path, const char* title, const char* author);

  void credit(int book, uint32_t ms, int32_t day);
  void addPage(int book, int32_t day);

  const Day* findDay(int32_t day) const;
  uint32_t msOn(int32_t day) const;
  uint32_t pagesOn(int32_t day) const;
  uint64_t msBetween(int32_t first, int32_t last) const;
  uint32_t pagesBetween(int32_t first, int32_t last) const;
  bool metGoal(int32_t day) const { return msOn(day) >= settings.streakMs(); }

  // Consecutive days meeting the streak threshold, ending today. A today that
  // has not met it yet does not break the streak; it just is not counted.
  int streak(int32_t today) const;
  int bestStreak() const;
  int daysRead() const;
  uint64_t totalMs() const;
  uint32_t totalPages() const;

  // Book indices, most recently read first.
  std::vector<int> booksByRecency() const;

  std::vector<uint8_t> serialize() const;
  // Leaves the store untouched and returns false when the bytes are not a
  // complete stats file.
  bool deserialize(const uint8_t* data, size_t len);

 private:
  Day& dayRecord(int32_t day);
};

// Milliseconds a page takes, from this book's own sample, or from every book
// together while this one has too few turns. 0 when neither knows yet.
uint32_t msPerPage(const Store& store, int book);
// Milliseconds a percent of this book takes. 0 until a percent has been read.
uint32_t msPerPercent(const Store& store, int book);

class Tracker {
 public:
  explicit Tracker(Store& store) : store_(store) {}

  // Called on every paint of a reader page, including repaints of the same one.
  void pageShown(uint32_t nowMs, int32_t day, const char* path, const char* title, const char* author,
                 const Position& pos);
  // The reader stopped being the screen (a menu, the dictionary, Home). The
  // session stays open for one idle ceiling in case the same book comes back.
  void away(uint32_t nowMs);
  // Closes a session that has been away longer than the idle ceiling.
  void poll(uint32_t nowMs, int32_t day);
  // `fromTimeout`: the device slept because nobody touched it, so the last page
  // was not being read all that time and gets an average page's worth.
  void sleep(uint32_t nowMs, int32_t day, bool fromTimeout);

  int openBook() const { return open_; }
  bool dirty() const { return dirty_; }
  void markClean() { dirty_ = false; }
  void markDirty() { dirty_ = true; }

  // Seconds left for the open book at `pos`, or -1 when there is no estimate.
  int32_t secondsLeft(const Position& pos, TimeLeft mode) const;

 private:
  void finish(uint32_t tailMs, int32_t day);

  Store& store_;
  int open_ = -1;
  Position pos_{};
  uint32_t shownAt_ = 0;
  uint32_t sessionMs_ = 0;
  bool away_ = false;
  uint32_t awayAt_ = 0;
  bool dirty_ = false;
};

// A single forward page turn, as opposed to a jump, a skip or a step back.
bool isSingleForward(const Position& from, const Position& to);

// "45 min", "2 h 05 min", "<1 min".
void formatDuration(char* out, size_t cap, uint64_t ms);
// The compact form the status bar has room for: "45m", "2h05m".
void formatShort(char* out, size_t cap, uint32_t seconds);

}  // namespace readstats
