#include "StatsCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace readstats {

namespace {

// A page glanced at for less than this was flipped past, not read.
constexpr uint32_t kMinPageMs = 2000;
// Opening a book to check where you were is not a reading session.
constexpr uint32_t kMinSessionMs = 60000;
// The per-book pace needs a handful of turns before it beats the pooled one,
// and the pooled one needs more before it beats no estimate at all.
constexpr uint32_t kBookPaceTurns = 5;
constexpr uint32_t kPooledPaceTurns = 20;
// Book positions are floats recomputed from page counts, so "the same page"
// has to tolerate rounding.
constexpr float kSamePercent = 0.001f;
// A forward turn that moves the book by more than this is a jump, even if it
// lands on page one of a chapter.
constexpr float kMaxTurnPercent = 3.0f;

constexpr uint8_t kMagic[4] = {'R', 'S', 'T', 'S'};
constexpr uint8_t kVersion = 1;
constexpr size_t kMaxString = 1024;

class Writer {
 public:
  explicit Writer(std::vector<uint8_t>& out) : out_(out) {}
  void u8(uint8_t v) { out_.push_back(v); }
  void u16(uint16_t v) { raw(&v, 2); }
  void u32(uint32_t v) { raw(&v, 4); }
  void i32(int32_t v) { raw(&v, 4); }
  void u64(uint64_t v) { raw(&v, 8); }
  void f32(float v) { raw(&v, 4); }
  void str(const std::string& s) {
    const size_t n = std::min(s.size(), kMaxString);
    u16(static_cast<uint16_t>(n));
    out_.insert(out_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
  }

 private:
  // Little-endian on every target this builds for (Xtensa, x86, arm64, wasm32).
  void raw(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    out_.insert(out_.end(), b, b + n);
  }
  std::vector<uint8_t>& out_;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t len) : data_(data), len_(len) {}
  bool ok() const { return ok_; }
  bool atEnd() const { return pos_ == len_; }
  uint8_t u8() {
    uint8_t v = 0;
    raw(&v, 1);
    return v;
  }
  uint16_t u16() {
    uint16_t v = 0;
    raw(&v, 2);
    return v;
  }
  uint32_t u32() {
    uint32_t v = 0;
    raw(&v, 4);
    return v;
  }
  int32_t i32() {
    int32_t v = 0;
    raw(&v, 4);
    return v;
  }
  uint64_t u64() {
    uint64_t v = 0;
    raw(&v, 8);
    return v;
  }
  float f32() {
    float v = 0;
    raw(&v, 4);
    return std::isfinite(v) ? v : 0.0f;
  }
  std::string str() {
    const uint16_t n = u16();
    if (!ok_ || n > kMaxString || len_ - pos_ < n) {
      ok_ = false;
      return {};
    }
    std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
    pos_ += n;
    return s;
  }
  // A count that promises more records than there are bytes left is corrupt,
  // and trusting it would reserve gigabytes.
  bool plausible(uint32_t count, size_t minRecordBytes) const { return count <= (len_ - pos_) / minRecordBytes; }

 private:
  void raw(void* p, size_t n) {
    if (!ok_ || len_ - pos_ < n) {
      ok_ = false;
      return;
    }
    std::memcpy(p, data_ + pos_, n);
    pos_ += n;
  }
  const uint8_t* data_;
  size_t len_;
  size_t pos_ = 0;
  bool ok_ = true;
};

bool samePosition(const Position& a, const Position& b) {
  return a.page == b.page && a.pageCount == b.pageCount && std::fabs(a.percent - b.percent) < kSamePercent;
}

}  // namespace

// Howard Hinnant's days_from_civil / civil_from_days.
int32_t dayFromCivil(int year, const int month, const int day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int yoe = year - era * 400;
  const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDay(int32_t z, int& year, int& month, int& dayOfMonth) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const int doe = z - era * 146097;
  const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int mp = (5 * doy + 2) / 153;
  dayOfMonth = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = yoe + era * 400 + (month <= 2 ? 1 : 0);
}

int weekdayOf(const int32_t day) {
  // 1970-01-01 was a Thursday.
  const int w = static_cast<int>((day + 3) % 7);
  return w < 0 ? w + 7 : w;
}

int daysInMonth(const int year, const int month) {
  return dayFromCivil(month == 12 ? year + 1 : year, month == 12 ? 1 : month + 1, 1) - dayFromCivil(year, month, 1);
}

bool isSingleForward(const Position& from, const Position& to) {
  const float delta = to.percent - from.percent;
  if (delta < -kSamePercent || delta > kMaxTurnPercent) return false;
  if (to.page == from.page + 1) return true;
  // Into the next chapter: its first page, from the last page of the one before.
  return to.page == 1 && from.pageCount > 0 && from.page >= from.pageCount && delta > kSamePercent;
}

// --- Store -----------------------------------------------------------------

int Store::findOrAdd(const char* path, const char* title, const char* author) {
  for (size_t i = 0; i < books.size(); ++i) {
    if (books[i].path == path) {
      if (title[0] != '\0') books[i].title = title;
      if (author[0] != '\0') books[i].author = author;
      return static_cast<int>(i);
    }
  }
  if (title[0] != '\0') {
    for (size_t i = 0; i < books.size(); ++i) {
      if (books[i].title == title && books[i].author == author) {
        books[i].path = path;
        return static_cast<int>(i);
      }
    }
  }
  Book book;
  book.path = path;
  book.title = title;
  book.author = author;
  books.push_back(std::move(book));
  return static_cast<int>(books.size() - 1);
}

Day& Store::dayRecord(const int32_t day) {
  auto it = std::lower_bound(days.begin(), days.end(), day, [](const Day& d, int32_t v) { return d.day < v; });
  if (it == days.end() || it->day != day) {
    Day fresh;
    fresh.day = day;
    it = days.insert(it, fresh);
  }
  return *it;
}

void Store::credit(const int book, const uint32_t ms, const int32_t day) {
  if (book < 0 || static_cast<size_t>(book) >= books.size() || ms == 0) return;
  Book& b = books[book];
  b.ms += ms;
  b.seen = ++sequence;
  if (day == kNoDay) return;
  dayRecord(day).ms += ms;
  if (b.firstDay == kNoDay || day < b.firstDay) b.firstDay = day;
  if (day > b.lastDay) b.lastDay = day;
}

void Store::addPage(const int book, const int32_t day) {
  if (book < 0 || static_cast<size_t>(book) >= books.size()) return;
  books[book].pages += 1;
  if (day != kNoDay) dayRecord(day).pages += 1;
}

const Day* Store::findDay(const int32_t day) const {
  const auto it = std::lower_bound(days.begin(), days.end(), day, [](const Day& d, int32_t v) { return d.day < v; });
  return it != days.end() && it->day == day ? &*it : nullptr;
}

uint32_t Store::msOn(const int32_t day) const {
  const Day* d = findDay(day);
  return d ? d->ms : 0;
}

uint32_t Store::pagesOn(const int32_t day) const {
  const Day* d = findDay(day);
  return d ? d->pages : 0;
}

uint64_t Store::msBetween(const int32_t first, const int32_t last) const {
  uint64_t sum = 0;
  for (const Day& d : days) {
    if (d.day >= first && d.day <= last) sum += d.ms;
  }
  return sum;
}

uint32_t Store::pagesBetween(const int32_t first, const int32_t last) const {
  uint32_t sum = 0;
  for (const Day& d : days) {
    if (d.day >= first && d.day <= last) sum += d.pages;
  }
  return sum;
}

int Store::streak(const int32_t today) const {
  int32_t d = metGoal(today) ? today : today - 1;
  int count = 0;
  while (metGoal(d)) {
    ++count;
    --d;
  }
  return count;
}

int Store::bestStreak() const {
  int best = 0;
  int run = 0;
  int32_t previous = kNoDay;
  const uint32_t threshold = settings.streakMs();
  for (const Day& d : days) {
    if (d.ms < threshold) {
      run = 0;
      continue;
    }
    run = (run > 0 && d.day == previous + 1) ? run + 1 : 1;
    previous = d.day;
    best = std::max(best, run);
  }
  return best;
}

int Store::daysRead() const {
  int count = 0;
  for (const Day& d : days) {
    if (d.ms >= 60000) ++count;
  }
  return count;
}

uint64_t Store::totalMs() const {
  uint64_t sum = 0;
  for (const Book& b : books) sum += b.ms;
  return sum;
}

uint32_t Store::totalPages() const {
  uint32_t sum = 0;
  for (const Book& b : books) sum += b.pages;
  return sum;
}

std::vector<int> Store::booksByRecency() const {
  std::vector<int> order;
  order.reserve(books.size());
  for (size_t i = 0; i < books.size(); ++i) {
    if (books[i].ms > 0) order.push_back(static_cast<int>(i));
  }
  std::sort(order.begin(), order.end(), [this](int a, int b) { return books[a].seen > books[b].seen; });
  return order;
}

std::vector<uint8_t> Store::serialize() const {
  std::vector<uint8_t> out;
  out.reserve(64 + days.size() * 12 + books.size() * 160);
  Writer w(out);
  for (const uint8_t m : kMagic) w.u8(m);
  w.u8(kVersion);
  w.u16(settings.goalMinutes);
  w.u8(settings.idleMinutes);
  w.u8(static_cast<uint8_t>(settings.timeLeft));
  w.u32(sequence);
  w.u32(static_cast<uint32_t>(days.size()));
  for (const Day& d : days) {
    w.i32(d.day);
    w.u32(d.ms);
    w.u32(d.pages);
  }
  w.u32(static_cast<uint32_t>(books.size()));
  for (const Book& b : books) {
    w.str(b.path);
    w.str(b.title);
    w.str(b.author);
    w.u64(b.ms);
    w.u32(b.pages);
    w.u32(b.sessions);
    w.i32(b.firstDay);
    w.i32(b.lastDay);
    w.u32(b.seen);
    w.f32(b.percent);
    w.u32(b.paceMs);
    w.u32(b.paceTurns);
    w.f32(b.pacePercent);
  }
  return out;
}

bool Store::deserialize(const uint8_t* data, const size_t len) {
  Reader r(data, len);
  for (const uint8_t m : kMagic) {
    if (r.u8() != m) return false;
  }
  if (r.u8() != kVersion) return false;

  Settings s;
  s.goalMinutes = r.u16();
  s.idleMinutes = r.u8();
  const uint8_t mode = r.u8();
  s.timeLeft = mode <= static_cast<uint8_t>(TimeLeft::Book) ? static_cast<TimeLeft>(mode) : TimeLeft::Chapter;
  if (s.idleMinutes == 0) s.idleMinutes = 5;
  const uint32_t seq = r.u32();

  const uint32_t dayCount = r.u32();
  if (!r.ok() || !r.plausible(dayCount, 12)) return false;
  std::vector<Day> ds;
  ds.reserve(dayCount);
  for (uint32_t i = 0; i < dayCount; ++i) {
    Day d;
    d.day = r.i32();
    d.ms = r.u32();
    d.pages = r.u32();
    if (!ds.empty() && d.day <= ds.back().day) return false;
    ds.push_back(d);
  }

  const uint32_t bookCount = r.u32();
  // Three empty strings and the fixed fields.
  if (!r.ok() || !r.plausible(bookCount, 6 + 48)) return false;
  std::vector<Book> bs;
  bs.reserve(bookCount);
  for (uint32_t i = 0; i < bookCount && r.ok(); ++i) {
    Book b;
    b.path = r.str();
    b.title = r.str();
    b.author = r.str();
    b.ms = r.u64();
    b.pages = r.u32();
    b.sessions = r.u32();
    b.firstDay = r.i32();
    b.lastDay = r.i32();
    b.seen = r.u32();
    b.percent = r.f32();
    b.paceMs = r.u32();
    b.paceTurns = r.u32();
    b.pacePercent = r.f32();
    bs.push_back(std::move(b));
  }
  if (!r.ok() || !r.atEnd()) return false;

  settings = s;
  sequence = seq;
  days = std::move(ds);
  books = std::move(bs);
  return true;
}

uint32_t msPerPage(const Store& store, const int book) {
  if (book >= 0 && static_cast<size_t>(book) < store.books.size()) {
    const Book& b = store.books[book];
    if (b.paceTurns >= kBookPaceTurns) return b.paceMs / b.paceTurns;
  }
  uint64_t ms = 0;
  uint32_t turns = 0;
  for (const Book& b : store.books) {
    ms += b.paceMs;
    turns += b.paceTurns;
  }
  return turns >= kPooledPaceTurns ? static_cast<uint32_t>(ms / turns) : 0;
}

uint32_t msPerPercent(const Store& store, const int book) {
  if (book < 0 || static_cast<size_t>(book) >= store.books.size()) return 0;
  const Book& b = store.books[book];
  if (b.pacePercent < 1.0f) return 0;
  return static_cast<uint32_t>(static_cast<float>(b.paceMs) / b.pacePercent);
}

// --- Tracker ---------------------------------------------------------------

void Tracker::pageShown(const uint32_t nowMs, const int32_t day, const char* path, const char* title,
                        const char* author, const Position& pos) {
  const uint32_t idle = store_.settings.idleMs();
  if (open_ >= 0) {
    const bool otherBook = store_.books[open_].path != path;
    const bool awayTooLong = away_ && nowMs - awayAt_ > idle;
    if (otherBook || awayTooLong) finish(std::min((away_ ? awayAt_ : nowMs) - shownAt_, idle), day);
  }

  if (open_ < 0) {
    open_ = store_.findOrAdd(path, title, author);
    pos_ = pos;
    shownAt_ = nowMs;
    sessionMs_ = 0;
    away_ = false;
    store_.books[open_].percent = pos.percent;
    store_.books[open_].seen = ++store_.sequence;
    dirty_ = true;
    return;
  }

  if (samePosition(pos, pos_)) {
    away_ = false;
    return;
  }

  const uint32_t dwell = nowMs - shownAt_;
  const bool wasIdle = dwell > idle;
  const uint32_t credited = wasIdle ? idle : dwell;
  store_.credit(open_, credited, day);
  sessionMs_ += credited;

  if (isSingleForward(pos_, pos) && dwell >= kMinPageMs) {
    store_.addPage(open_, day);
    if (!wasIdle) {
      Book& b = store_.books[open_];
      b.paceMs += dwell;
      b.paceTurns += 1;
      b.pacePercent += pos.percent - pos_.percent;
    }
  }

  // A page left open past the ceiling ends the session it was part of; the
  // next page starts another.
  if (wasIdle) {
    if (sessionMs_ >= kMinSessionMs) store_.books[open_].sessions += 1;
    sessionMs_ = 0;
  }

  store_.books[open_].percent = pos.percent;
  pos_ = pos;
  shownAt_ = nowMs;
  away_ = false;
  dirty_ = true;
}

void Tracker::away(const uint32_t nowMs) {
  if (open_ < 0 || away_) return;
  away_ = true;
  awayAt_ = nowMs;
}

void Tracker::poll(const uint32_t nowMs, const int32_t day) {
  if (open_ < 0 || !away_) return;
  const uint32_t idle = store_.settings.idleMs();
  if (nowMs - awayAt_ > idle) finish(std::min(awayAt_ - shownAt_, idle), day);
}

void Tracker::sleep(const uint32_t nowMs, const int32_t day, const bool fromTimeout) {
  if (open_ < 0) return;
  const uint32_t idle = store_.settings.idleMs();
  const uint32_t until = away_ ? awayAt_ : nowMs;
  uint32_t tail = std::min(until - shownAt_, idle);
  if (fromTimeout && !away_) tail = std::min(tail, msPerPage(store_, open_));
  finish(tail, day);
}

void Tracker::finish(const uint32_t tailMs, const int32_t day) {
  if (open_ < 0) return;
  store_.credit(open_, tailMs, day);
  sessionMs_ += tailMs;
  if (sessionMs_ >= kMinSessionMs) store_.books[open_].sessions += 1;
  open_ = -1;
  sessionMs_ = 0;
  away_ = false;
  dirty_ = true;
}

int32_t Tracker::secondsLeft(const Position& pos, const TimeLeft mode) const {
  if (open_ < 0 || mode == TimeLeft::Off) return -1;
  if (mode == TimeLeft::Chapter) {
    const uint32_t perPage = msPerPage(store_, open_);
    if (perPage == 0 || pos.pageCount <= 0) return -1;
    const int pagesLeft = std::max(0, pos.pageCount - pos.page) + 1;
    return static_cast<int32_t>((static_cast<uint64_t>(perPage) * pagesLeft) / 1000);
  }
  const uint32_t perPercent = msPerPercent(store_, open_);
  if (perPercent == 0) return -1;
  const float left = std::max(0.0f, 100.0f - pos.percent);
  return static_cast<int32_t>(static_cast<float>(perPercent) * left / 1000.0f);
}

// --- Formatting --------------------------------------------------------------

void formatDuration(char* out, const size_t cap, const uint64_t ms) {
  const uint64_t minutes = ms / 60000;
  if (minutes == 0) {
    std::snprintf(out, cap, "<1 min");
  } else if (minutes < 60) {
    std::snprintf(out, cap, "%u min", static_cast<unsigned>(minutes));
  } else {
    std::snprintf(out, cap, "%u h %02u min", static_cast<unsigned>(minutes / 60), static_cast<unsigned>(minutes % 60));
  }
}

void formatShort(char* out, const size_t cap, const uint32_t seconds) {
  uint32_t minutes = (seconds + 30) / 60;
  if (minutes == 0) minutes = 1;
  if (minutes < 60) {
    std::snprintf(out, cap, "%um", static_cast<unsigned>(minutes));
  } else {
    std::snprintf(out, cap, "%uh%02um", static_cast<unsigned>(minutes / 60), static_cast<unsigned>(minutes % 60));
  }
}

}  // namespace readstats
