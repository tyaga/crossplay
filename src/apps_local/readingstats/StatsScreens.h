#pragma once

// The READING screens. Freestanding builders over plain models, so
// host-tests/ui-style tests can drive them without a device; the Activity
// fills the models from the stats store and supplies the target.

#include "../ui/ToyboxScreen.h"

namespace statsui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionCalendar = 1500,
  ActionBooks = 1501,
  ActionSettings = 1502,
  ActionPrevMonth = 1503,
  ActionNextMonth = 1504,
  ActionPickDay = 1505,
  ActionOpenBook = 1506,  // value: row index in the whole list
  ActionPageOlder = 1507,
  ActionPageNewer = 1508,
  ActionCycleSetting = 1509,  // value: SettingRow
};

constexpr int kHistoryDays = 14;
// Rows on one page of the books list.
constexpr int kBookRows = 7;

// --- The front door --------------------------------------------------------

struct HomeModel {
  bool clockSet = true;
  uint32_t todayMs = 0;
  uint32_t todayPages = 0;
  uint16_t goalMinutes = 0;
  int streak = 0;
  int bestStreak = 0;
  uint64_t weekMs = 0;                    // the last seven days, today included
  uint64_t monthMs = 0;                   // this calendar month
  uint32_t historyMs[kHistoryDays] = {};  // index 0 is today
  int bookCount = 0;
};

void buildHome(toybox::Screen& screen, const HomeModel& model);

// --- The month -------------------------------------------------------------

struct CalendarModel {
  int year = 2026;
  int month = 1;
  int firstWeekday = 0;  // 0 = Monday
  int dayCount = 30;
  uint32_t ms[31] = {};
  uint32_t pages[31] = {};
  uint32_t goalMs = 0;  // what a solid cell means; 0 falls back to half an hour
  int today = 0;        // day of month, 0 when today is not in this month
  int selected = 0;     // day of month, 0 for the month summary
  bool canGoNext = false;
};

void buildCalendar(toybox::Screen& screen, const CalendarModel& model);

// The grid's rect and one cell of it, shared with the tests so what is drawn
// and what is tapped come from one function.
fui::Rect calendarGrid(const fui::DeviceContext& device);
fui::Rect calendarCell(const fui::Rect& grid, int row, int column);
// The day of the month under a tap, or 0.
int calendarDayAt(const fui::DeviceContext& device, int firstWeekday, int dayCount, int x, int y);

// --- Books -----------------------------------------------------------------

struct BooksModel {
  const fui::ListItem* items = nullptr;
  int count = 0;
  const char* rightLabel = nullptr;  // "1-8 OF 23"
  bool canPageOlder = false;
  bool canPageNewer = false;
};

// Titles and authors are the card's own and often not Latin, so these two
// screens are drawn in the UI faces, which carry Cyrillic; see StatsActivity.
void buildBooks(toybox::Screen& screen, const BooksModel& model);
fui::Rect booksBand(const fui::DeviceContext& device);

struct BookLine {
  const char* label = "";
  const char* value = "";
};

struct BookModel {
  const char* title = "";
  const char* author = "";
  const BookLine* lines = nullptr;
  int lineCount = 0;
};

void buildBook(toybox::Screen& screen, const BookModel& model);

// --- Settings --------------------------------------------------------------

enum SettingRow : int16_t { RowGoal = 0, RowTimeLeft = 1, RowIdle = 2, kSettingRows = 3 };

struct SettingsModel {
  const char* values[kSettingRows] = {"", "", ""};
};

void buildSettings(toybox::Screen& screen, const SettingsModel& model);

}  // namespace statsui
