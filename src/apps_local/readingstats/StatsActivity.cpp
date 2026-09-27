#include "StatsActivity.h"

#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "ReadingStats.h"
#include "StatsCore.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

constexpr uint16_t kGoalSteps[] = {0, 10, 15, 20, 30, 45, 60, 90, 120};
constexpr uint8_t kIdleSteps[] = {2, 3, 5, 10, 15};
constexpr const char* kMonthShort[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Book titles and authors come off the card in any script, and the Toybox
// faces are ASCII. The UI faces carry Latin, Cyrillic and Greek.
toybox::Faces bookFaces() { return toybox::Faces{UI_10_FONT_ID, UI_12_FONT_ID, toybox::kDisplayFontId}; }

void copyText(char* out, const size_t cap, const std::string& text) { std::snprintf(out, cap, "%s", text.c_str()); }

void dayLabel(char* out, const size_t cap, const int32_t day) {
  if (day == readstats::kNoDay) {
    std::snprintf(out, cap, "-");
    return;
  }
  int y = 0, m = 0, d = 0;
  readstats::civilFromDay(day, y, m, d);
  std::snprintf(out, cap, "%d %s %d", d, kMonthShort[(m + 11) % 12], y);
}

// The screens' text, owned here so that no lock is held while the panel
// refreshes: the status bar hook waits on the same lock.
struct BookRows {
  char titles[statsui::kBookRows][96];
  char subtitles[statsui::kBookRows][112];
  fui::ListItem items[statsui::kBookRows];
  char right[24];
  int count;
};

struct BookDetail {
  char title[256];
  char author[160];
  char values[10][32];
  statsui::BookLine lines[10];
  int count;
};

}  // namespace

std::unique_ptr<Activity> StatsActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<StatsActivity>(renderer, mappedInput);
}

void StatsActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  today_ = readstats::today();
  int32_t anchor = today_;
  if (anchor == readstats::kNoDay) {
    readstats::Access access;
    if (!access.store.days.empty()) anchor = access.store.days.back().day;
  }
  if (anchor != readstats::kNoDay) {
    int d = 0;
    readstats::civilFromDay(anchor, calYear_, calMonth_, d);
  }
  requestUpdate();
}

bool StatsActivity::monthIsCurrent() const {
  if (today_ == readstats::kNoDay) return true;
  int y = 0, m = 0, d = 0;
  readstats::civilFromDay(today_, y, m, d);
  return calYear_ > y || (calYear_ == y && calMonth_ >= m);
}

void StatsActivity::stepMonth(const int delta) {
  if (delta > 0 && monthIsCurrent()) return;
  calMonth_ += delta;
  if (calMonth_ < 1) {
    calMonth_ = 12;
    --calYear_;
  } else if (calMonth_ > 12) {
    calMonth_ = 1;
    ++calYear_;
  }
  calSelected_ = 0;
}

void StatsActivity::openBooks() {
  readstats::Access access;
  order_ = access.store.booksByRecency();
  booksFirst_ = 0;
  view_ = View::Books;
}

void StatsActivity::pageBooks(const int delta) {
  const int next = booksFirst_ + delta * statsui::kBookRows;
  if (next < 0 || next >= static_cast<int>(order_.size())) return;
  booksFirst_ = next;
}

void StatsActivity::cycleSetting(const int row) {
  {
    readstats::Access access;
    readstats::Settings& s = access.store.settings;
    if (row == statsui::RowGoal) {
      size_t i = 0;
      while (i < sizeof(kGoalSteps) / sizeof(kGoalSteps[0]) && kGoalSteps[i] != s.goalMinutes) ++i;
      s.goalMinutes = kGoalSteps[(i + 1) % (sizeof(kGoalSteps) / sizeof(kGoalSteps[0]))];
    } else if (row == statsui::RowTimeLeft) {
      s.timeLeft = static_cast<readstats::TimeLeft>((static_cast<uint8_t>(s.timeLeft) + 1) % 3);
    } else if (row == statsui::RowIdle) {
      size_t i = 0;
      while (i < sizeof(kIdleSteps) && kIdleSteps[i] != s.idleMinutes) ++i;
      s.idleMinutes = kIdleSteps[(i + 1) % sizeof(kIdleSteps)];
    }
    access.tracker.markDirty();
  }
  readstats::saveNow();
}

void StatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (view_) {
      case View::Home:
        shelf::leave(renderer, mappedInput);
        return;
      case View::Book:
        view_ = View::Books;
        break;
      default:
        view_ = View::Home;
        break;
    }
    requestUpdate();
    return;
  }

  const bool forward = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                       mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool backward = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                        mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (forward || backward) {
    if (view_ == View::Calendar) {
      handleAction(forward ? statsui::ActionNextMonth : statsui::ActionPrevMonth, 0, 0, 0);
    } else if (view_ == View::Books) {
      handleAction(forward ? statsui::ActionPageOlder : statsui::ActionPageNewer, 0, 0, 0);
    }
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);
  handleAction(event.action, event.value, tapX, tapY);
}

void StatsActivity::handleAction(const fui::ActionId action, const int16_t value, const int tapX, const int tapY) {
  switch (action) {
    case statsui::ActionCalendar:
      view_ = View::Calendar;
      calSelected_ = 0;
      break;
    case statsui::ActionBooks:
      openBooks();
      break;
    case statsui::ActionSettings:
      view_ = View::Settings;
      break;
    case statsui::ActionPrevMonth:
      stepMonth(-1);
      break;
    case statsui::ActionNextMonth:
      if (monthIsCurrent()) return;
      stepMonth(1);
      break;
    case statsui::ActionPickDay: {
      const fui::DeviceContext device = fui::GfxRendererTarget(renderer).deviceContext();
      const int day = statsui::calendarDayAt(device, calFirstWeekday_, calDayCount_, tapX, tapY);
      calSelected_ = day == calSelected_ ? 0 : day;
      break;
    }
    case statsui::ActionOpenBook:
      if (value < 0 || value >= static_cast<int>(order_.size())) return;
      book_ = order_[value];
      view_ = View::Book;
      break;
    case statsui::ActionPageOlder:
      pageBooks(1);
      break;
    case statsui::ActionPageNewer:
      pageBooks(-1);
      break;
    case statsui::ActionCycleSetting:
      cycleSetting(value);
      break;
    default:
      return;
  }
  requestUpdate();
}

void StatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const bool books = view_ == View::Books || view_ == View::Book;
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, books ? bookFaces() : toybox::proseMenuFaces());
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Home:
      renderHome(screen);
      break;
    case View::Calendar:
      renderCalendar(screen);
      break;
    case View::Books:
      renderBooks(screen);
      break;
    case View::Book:
      renderBook(screen);
      break;
    case View::Settings:
      renderSettings(screen);
      break;
  }

  toybox::reportOverflow(interactions_, "ReadingStats");
  interactionsReady_ = true;
  renderer.displayBuffer();
}

void StatsActivity::renderHome(toybox::Screen& screen) {
  statsui::HomeModel model;
  {
    readstats::Access access;
    const readstats::Store& s = access.store;
    model.clockSet = today_ != readstats::kNoDay;
    model.goalMinutes = s.settings.goalMinutes;
    model.bestStreak = s.bestStreak();
    model.bookCount = static_cast<int>(s.booksByRecency().size());
    if (model.clockSet) {
      model.todayMs = s.msOn(today_);
      model.todayPages = s.pagesOn(today_);
      model.streak = s.streak(today_);
      model.weekMs = s.msBetween(today_ - 6, today_);
      int y = 0, m = 0, d = 0;
      readstats::civilFromDay(today_, y, m, d);
      model.monthMs = s.msBetween(readstats::dayFromCivil(y, m, 1), today_);
      for (int i = 0; i < statsui::kHistoryDays; ++i) model.historyMs[i] = s.msOn(today_ - i);
    }
  }
  statsui::buildHome(screen, model);
}

void StatsActivity::renderCalendar(toybox::Screen& screen) {
  statsui::CalendarModel model;
  model.year = calYear_;
  model.month = calMonth_;
  const int32_t first = readstats::dayFromCivil(calYear_, calMonth_, 1);
  model.firstWeekday = readstats::weekdayOf(first);
  model.dayCount = readstats::daysInMonth(calYear_, calMonth_);
  calFirstWeekday_ = model.firstWeekday;
  calDayCount_ = model.dayCount;
  if (today_ != readstats::kNoDay && today_ >= first && today_ < first + model.dayCount) {
    model.today = static_cast<int>(today_ - first) + 1;
  }
  model.selected = calSelected_;
  model.canGoNext = !monthIsCurrent();
  {
    readstats::Access access;
    const readstats::Store& s = access.store;
    model.goalMs = s.settings.goalMinutes * 60000u;
    for (int d = 0; d < model.dayCount && d < 31; ++d) {
      model.ms[d] = s.msOn(first + d);
      model.pages[d] = s.pagesOn(first + d);
    }
  }
  statsui::buildCalendar(screen, model);
}

void StatsActivity::renderBooks(toybox::Screen& screen) {
  auto rows = makeUniqueNoThrow<BookRows>();
  if (!rows) {
    LOG_ERR("RSTAT", "OOM: book rows");
    return;
  }
  const int total = static_cast<int>(order_.size());
  rows->count = std::min(statsui::kBookRows, std::max(0, total - booksFirst_));
  {
    readstats::Access access;
    const readstats::Store& s = access.store;
    for (int i = 0; i < rows->count; ++i) {
      const int index = order_[booksFirst_ + i];
      if (index < 0 || static_cast<size_t>(index) >= s.books.size()) continue;
      const readstats::Book& b = s.books[index];
      if (b.title.empty()) {
        const size_t slash = b.path.find_last_of('/');
        copyText(rows->titles[i], sizeof(rows->titles[i]),
                 slash == std::string::npos ? b.path : b.path.substr(slash + 1));
      } else {
        copyText(rows->titles[i], sizeof(rows->titles[i]), b.title);
      }
      char duration[24];
      readstats::formatDuration(duration, sizeof(duration), b.ms);
      if (b.author.empty()) {
        std::snprintf(rows->subtitles[i], sizeof(rows->subtitles[i]), "%s  ·  %.0f%%", duration, b.percent);
      } else {
        std::snprintf(rows->subtitles[i], sizeof(rows->subtitles[i]), "%s  ·  %.0f%%  ·  %s", duration, b.percent,
                      b.author.c_str());
      }
    }
  }
  for (int i = 0; i < rows->count; ++i) {
    fui::ListItem& item = rows->items[i];
    item = fui::ListItem{};
    item.label = rows->titles[i];
    item.subtitle = rows->subtitles[i];
    item.actionValue = static_cast<int16_t>(booksFirst_ + i);
  }
  std::snprintf(rows->right, sizeof(rows->right), "%d-%d OF %d", total == 0 ? 0 : booksFirst_ + 1,
                booksFirst_ + rows->count, total);

  statsui::BooksModel model;
  model.items = rows->items;
  model.count = rows->count;
  model.rightLabel = rows->right;
  model.canPageNewer = booksFirst_ > 0;
  model.canPageOlder = booksFirst_ + statsui::kBookRows < total;
  statsui::buildBooks(screen, model);
}

void StatsActivity::renderBook(toybox::Screen& screen) {
  auto detail = makeUniqueNoThrow<BookDetail>();
  if (!detail) {
    LOG_ERR("RSTAT", "OOM: book detail");
    return;
  }
  detail->count = 0;
  {
    readstats::Access access;
    const readstats::Store& s = access.store;
    if (book_ < 0 || static_cast<size_t>(book_) >= s.books.size()) return;
    const readstats::Book& b = s.books[book_];
    copyText(detail->title, sizeof(detail->title), b.title.empty() ? b.path : b.title);
    copyText(detail->author, sizeof(detail->author), b.author);

    auto add = [&detail](const char* label) -> char* {
      if (detail->count >= 10) return detail->values[9];
      detail->lines[detail->count].label = label;
      detail->lines[detail->count].value = detail->values[detail->count];
      return detail->values[detail->count++];
    };
    readstats::formatDuration(add("Time read"), 32, b.ms);
    std::snprintf(add("Pages turned"), 32, "%u", static_cast<unsigned>(b.pages));
    std::snprintf(add("Progress"), 32, "%.0f%%", b.percent);
    std::snprintf(add("Sessions"), 32, "%u", static_cast<unsigned>(b.sessions));
    if (b.sessions > 0) readstats::formatDuration(add("Average session"), 32, b.ms / b.sessions);
    const uint32_t perPage = b.paceTurns >= 5 ? b.paceMs / b.paceTurns : 0;
    if (perPage > 0) std::snprintf(add("Pace"), 32, "%u pages an hour", static_cast<unsigned>(3600000u / perPage));
    const uint32_t perPercent = readstats::msPerPercent(s, book_);
    if (perPercent > 0 && b.percent < 99.5f) {
      readstats::formatDuration(add("Left, at this pace"), 32,
                                static_cast<uint64_t>(static_cast<float>(perPercent) * (100.0f - b.percent)));
    }
    dayLabel(add("First read"), 32, b.firstDay);
    dayLabel(add("Last read"), 32, b.lastDay);
  }

  statsui::BookModel model;
  model.title = detail->title;
  model.author = detail->author;
  model.lines = detail->lines;
  model.lineCount = detail->count;
  statsui::buildBook(screen, model);
}

void StatsActivity::renderSettings(toybox::Screen& screen) {
  char goal[16];
  const char* timeLeft = "OFF";
  char idle[16];
  {
    readstats::Access access;
    const readstats::Settings& s = access.store.settings;
    if (s.goalMinutes == 0) {
      std::snprintf(goal, sizeof(goal), "OFF");
    } else {
      std::snprintf(goal, sizeof(goal), "%u MIN", static_cast<unsigned>(s.goalMinutes));
    }
    if (s.timeLeft == readstats::TimeLeft::Chapter) timeLeft = "CHAPTER";
    if (s.timeLeft == readstats::TimeLeft::Book) timeLeft = "BOOK";
    std::snprintf(idle, sizeof(idle), "%u MIN", static_cast<unsigned>(s.idleMinutes));
  }
  statsui::SettingsModel model;
  model.values[statsui::RowGoal] = goal;
  model.values[statsui::RowTimeLeft] = timeLeft;
  model.values[statsui::RowIdle] = idle;
  statsui::buildSettings(screen, model);
}
