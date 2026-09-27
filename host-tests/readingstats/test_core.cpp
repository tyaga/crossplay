// The tracker driven with synthetic page views, and the file round trip.
//
// Every property here is about time -- what one page view is worth, when a
// session ends, what counts as a turn -- so the clock is a plain number the
// test advances, and the day is passed in rather than read.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/readingstats/StatsCore.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL: %s\n", what);
  }
}

using readstats::Position;
using readstats::Store;
using readstats::TimeLeft;
using readstats::Tracker;

constexpr int32_t kDay = 20000;
constexpr uint32_t kSec = 1000;
constexpr uint32_t kMin = 60 * kSec;

Position at(const int page, const int count, const float percent) {
  Position p;
  p.page = page;
  p.pageCount = count;
  p.percent = percent;
  return p;
}

// Reads `pages` pages of one chapter, `each` ms apiece, starting at page 1.
uint32_t readPages(Tracker& t, uint32_t now, const int pages, const uint32_t each, const char* path = "/a.epub",
                   const int32_t day = kDay) {
  for (int i = 1; i <= pages; ++i) {
    t.pageShown(now, day, path, path, "Author", at(i, 100, static_cast<float>(i) * 0.1f));
    now += each;
  }
  return now;
}

void testCalendar() {
  check(readstats::dayFromCivil(1970, 1, 1) == 0, "epoch is day 0");
  check(readstats::dayFromCivil(2026, 9, 27) == 20723, "2026-09-27");
  int y = 0, m = 0, d = 0;
  readstats::civilFromDay(20723, y, m, d);
  check(y == 2026 && m == 9 && d == 27, "civil round trip");
  check(readstats::weekdayOf(20723) == 6, "2026-09-27 is a Sunday");
  check(readstats::weekdayOf(0) == 3, "1970-01-01 is a Thursday");
  check(readstats::daysInMonth(2024, 2) == 29 && readstats::daysInMonth(2026, 2) == 28, "February");
  check(readstats::daysInMonth(2026, 12) == 31, "December");
}

void testPageTime() {
  Store s;
  Tracker t(s);
  const uint32_t end = readPages(t, 0, 11, 30 * kSec);
  // Ten views ended by a turn; the eleventh is still open.
  check(s.books.size() == 1, "one book");
  check(s.books[0].ms == 10 * 30 * kSec, "ten views of thirty seconds");
  check(s.msOn(kDay) == 10 * 30 * kSec, "the day got the same time");
  check(s.books[0].pages == 10 && s.pagesOn(kDay) == 10, "ten pages");

  // Closing credits the open page up to now.
  t.sleep(end + 20 * kSec, kDay, false);
  check(s.books[0].ms == 10 * 30 * kSec + 50 * kSec, "the open page is credited at sleep");
  check(s.books[0].sessions == 1, "one session");
  check(t.openBook() == -1, "sleep closes the session");
}

void testIdleCeiling() {
  Store s;
  s.settings.idleMinutes = 5;
  Tracker t(s);
  t.pageShown(0, kDay, "/a.epub", "A", "", at(1, 50, 1.0f));
  t.pageShown(40 * kMin, kDay, "/a.epub", "A", "", at(2, 50, 1.1f));
  check(s.books[0].ms == 5 * kMin, "a page left open counts for the ceiling only");
  check(s.books[0].pages == 1, "the page still counts as read");
  check(s.books[0].paceTurns == 0, "an idle page is kept out of the pace");
}

void testRepaintIsNotATurn() {
  Store s;
  Tracker t(s);
  t.pageShown(0, kDay, "/a.epub", "A", "", at(3, 20, 5.0f));
  t.pageShown(10 * kSec, kDay, "/a.epub", "A", "", at(3, 20, 5.0f));
  t.pageShown(20 * kSec, kDay, "/a.epub", "A", "", at(3, 20, 5.0f));
  t.pageShown(30 * kSec, kDay, "/a.epub", "A", "", at(4, 20, 5.2f));
  check(s.books[0].ms == 30 * kSec, "repaints keep the page's clock running");
  check(s.books[0].pages == 1, "and do not turn it");
}

void testTurnsAndJumps() {
  check(readstats::isSingleForward(at(4, 20, 10.0f), at(5, 20, 10.3f)), "next page");
  check(readstats::isSingleForward(at(20, 20, 30.0f), at(1, 15, 30.2f)), "next chapter");
  check(!readstats::isSingleForward(at(5, 20, 10.3f), at(4, 20, 10.0f)), "back is not forward");
  check(!readstats::isSingleForward(at(4, 20, 10.0f), at(14, 20, 12.0f)), "a ten-page skip is a jump");
  check(!readstats::isSingleForward(at(4, 20, 10.0f), at(1, 30, 40.0f)), "a TOC jump is a jump");
  check(!readstats::isSingleForward(at(4, 20, 10.0f), at(1, 30, 11.0f)), "chapter start from mid-chapter");

  Store s;
  Tracker t(s);
  t.pageShown(0, kDay, "/a.epub", "A", "", at(4, 20, 10.0f));
  t.pageShown(1 * kSec, kDay, "/a.epub", "A", "", at(5, 20, 10.3f));
  check(s.books[0].pages == 0, "a page flipped past in a second is not read");
  t.pageShown(31 * kSec, kDay, "/a.epub", "A", "", at(1, 30, 40.0f));
  check(s.books[0].pages == 0, "a jump is not a page");
  check(s.books[0].ms == 31 * kSec, "but the time before it counts");
}

void testAwayAndBack() {
  Store s;
  Tracker t(s);
  t.pageShown(0, kDay, "/a.epub", "A", "", at(1, 20, 1.0f));
  // Dictionary for two minutes, then back to the same page.
  t.away(40 * kSec);
  t.poll(90 * kSec, kDay);
  check(t.openBook() == 0, "a short absence keeps the session");
  t.pageShown(160 * kSec, kDay, "/a.epub", "A", "", at(1, 20, 1.0f));
  t.pageShown(170 * kSec, kDay, "/a.epub", "A", "", at(2, 20, 1.1f));
  check(s.books[0].ms == 170 * kSec, "time in the dictionary is reading time");

  // Home, and nothing for a long time: the tail is cut where the reader left.
  t.away(200 * kSec);
  t.poll(200 * kSec + 6 * kMin, kDay);
  check(t.openBook() == -1, "a long absence closes the session");
  check(s.books[0].ms == 200 * kSec, "the tail stops at the moment the reader left");
  check(s.books[0].sessions == 1, "and it was one session");
}

void testSwitchingBooks() {
  Store s;
  Tracker t(s);
  t.pageShown(0, kDay, "/a.epub", "A", "", at(1, 20, 1.0f));
  t.pageShown(90 * kSec, kDay, "/b.epub", "B", "", at(1, 20, 1.0f));
  check(s.books.size() == 2, "two books");
  check(s.books[0].ms == 90 * kSec, "the first book keeps its open page");
  check(s.books[0].sessions == 1, "and its session");
  check(t.openBook() == 1, "the second is open");
}

void testTimeoutSleep() {
  Store s;
  Tracker t(s);
  const uint32_t end = readPages(t, 0, 7, 40 * kSec);
  const uint64_t before = s.books[0].ms;
  // The device slept on its own ten minutes after the last turn.
  t.sleep(end - 40 * kSec + 10 * kMin, kDay, true);
  check(s.books[0].ms == before + 40 * kSec, "an unattended last page is worth an average page");
}

void testMovedBookKeepsHistory() {
  Store s;
  const int a = s.findOrAdd("/books/x.epub", "War and Peace", "Tolstoy");
  s.credit(a, 5 * kMin, kDay);
  const int b = s.findOrAdd("/read/x.epub", "War and Peace", "Tolstoy");
  check(a == b, "same title and author is the same book");
  check(s.books[a].path == "/read/x.epub", "and it takes the new path");
  const int c = s.findOrAdd("/books/y.epub", "", "");
  check(c != a, "an untitled book is only matched by path");
}

void testStreaks() {
  Store s;
  s.settings.goalMinutes = 20;
  const int b = s.findOrAdd("/a.epub", "A", "");
  for (int d = 0; d < 5; ++d) s.credit(b, 25 * kMin, kDay - 10 + d);  // a five-day run, ending a week ago
  for (int d = 0; d < 3; ++d) s.credit(b, 30 * kMin, kDay - 3 + d);   // three days up to yesterday
  s.credit(b, 5 * kMin, kDay);                                        // today, short of the goal
  check(s.streak(kDay) == 3, "an unfinished today does not break the streak");
  s.credit(b, 20 * kMin, kDay);
  check(s.streak(kDay) == 4, "a finished today extends it");
  check(s.bestStreak() == 5, "the older run is the best");
  s.credit(b, 10 * kMin, kDay - 4);
  check(s.bestStreak() == 5 && s.streak(kDay) == 4, "a day short of the goal is not a streak day");
  s.settings.goalMinutes = 0;
  check(s.streak(kDay) == 5, "with no goal any minute counts");
}

void testNoClock() {
  Store s;
  Tracker t(s);
  readPages(t, 0, 4, 30 * kSec, "/a.epub", readstats::kNoDay);
  check(s.books[0].ms == 3 * 30 * kSec, "the book still gets its time");
  check(s.days.empty(), "but no day does");
}

void testTimeLeft() {
  Store s;
  Tracker t(s);
  check(t.secondsLeft(at(1, 10, 0), TimeLeft::Chapter) == -1, "no book, no estimate");
  readPages(t, 0, 4, 60 * kSec);
  check(t.secondsLeft(at(4, 100, 0.4f), TimeLeft::Chapter) == -1, "three turns are too few");
  readPages(t, 1000 * kSec, 10, 60 * kSec, "/b.epub");
  check(t.secondsLeft(at(10, 100, 1.0f), TimeLeft::Book) == -1, "under a percent read, no book estimate");
  readPages(t, 2000 * kSec, 12, 60 * kSec, "/c.epub");
  // /c.epub: eleven turns of a minute, 0.1% each.
  check(t.secondsLeft(at(12, 100, 1.2f), TimeLeft::Chapter) == 89 * 60, "89 pages of a minute left in the chapter");
  const int32_t book = t.secondsLeft(at(12, 100, 1.2f), TimeLeft::Book);
  check(book >= 98700 * 60 / 100 && book <= 98900 * 60 / 100, "98.8% of the book at ten minutes a percent");
  check(t.secondsLeft(at(12, 100, 1.2f), TimeLeft::Off) == -1, "off is off");
}

void testRoundTrip() {
  Store s;
  s.settings.goalMinutes = 45;
  s.settings.idleMinutes = 3;
  s.settings.timeLeft = TimeLeft::Book;
  Tracker t(s);
  readPages(t, 0, 12, 20 * kSec, "/книги/Война и мир.epub");
  t.sleep(300 * kSec, kDay, false);
  s.credit(0, 5 * kMin, kDay + 1);

  const std::vector<uint8_t> bytes = s.serialize();
  Store r;
  check(r.deserialize(bytes.data(), bytes.size()), "reads back");
  check(r.settings.goalMinutes == 45 && r.settings.idleMinutes == 3 && r.settings.timeLeft == TimeLeft::Book,
        "settings survive");
  check(r.books.size() == 1 && r.books[0].path == "/книги/Война и мир.epub", "the UTF-8 path survives");
  check(r.books[0].ms == s.books[0].ms && r.books[0].pages == s.books[0].pages, "book totals survive");
  check(r.books[0].paceTurns == s.books[0].paceTurns && r.books[0].sessions == 1, "pace and sessions survive");
  check(r.days.size() == 2 && r.msOn(kDay + 1) == 5 * kMin, "days survive");
  check(r.sequence == s.sequence, "the sequence survives");

  for (size_t cut = 0; cut < bytes.size(); cut += 7) {
    Store partial;
    partial.settings.goalMinutes = 99;
    if (partial.deserialize(bytes.data(), cut)) {
      check(false, "a truncated file is refused");
      break;
    }
    if (partial.settings.goalMinutes != 99) {
      check(false, "a refused file leaves the store alone");
      break;
    }
  }
  std::vector<uint8_t> bad = bytes;
  bad[0] = 'X';
  Store other;
  check(!other.deserialize(bad.data(), bad.size()), "a foreign file is refused");
}

void testFormatting() {
  char buf[32];
  readstats::formatDuration(buf, sizeof(buf), 30 * kSec);
  check(std::strcmp(buf, "<1 min") == 0, "under a minute");
  readstats::formatDuration(buf, sizeof(buf), 45 * kMin);
  check(std::strcmp(buf, "45 min") == 0, "minutes");
  readstats::formatDuration(buf, sizeof(buf), 125 * kMin);
  check(std::strcmp(buf, "2 h 05 min") == 0, "hours");
  readstats::formatShort(buf, sizeof(buf), 10);
  check(std::strcmp(buf, "1m") == 0, "never zero");
  readstats::formatShort(buf, sizeof(buf), 12 * 60 + 40);
  check(std::strcmp(buf, "13m") == 0, "rounded");
  readstats::formatShort(buf, sizeof(buf), 3 * 3600 + 5 * 60);
  check(std::strcmp(buf, "3h05m") == 0, "compact hours");
}

}  // namespace

int main() {
  testCalendar();
  testPageTime();
  testIdleCeiling();
  testRepaintIsNotATurn();
  testTurnsAndJumps();
  testAwayAndBack();
  testSwitchingBooks();
  testTimeoutSleep();
  testMovedBookKeepsHistory();
  testStreaks();
  testNoClock();
  testTimeLeft();
  testRoundTrip();
  testFormatting();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
