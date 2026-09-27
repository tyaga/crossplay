#include "StatsScreens.h"

#include <cstdio>

namespace statsui {
namespace {

constexpr int16_t kBodyTop = toybox::kBodyTop;
constexpr int16_t kDoorGap = 8;
constexpr uint32_t kMinute = 60000;
// What a full cell or bar means when no goal is set.
constexpr uint32_t kDefaultGoalMs = 30 * kMinute;

constexpr const char* kMonthNames[12] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                                         "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
constexpr const char* kMonthShort[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
constexpr const char* kWeekdays[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};

// Header band, and the page margin under it. `rightLabel` is paper-coloured
// because the band is black and the theme's subtitle colour is black too.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
  }
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// The theme's styles are built for the band or for rows; text set straight on
// the paper has to say it is black, and naming the alignment marks the style
// as the caller's so the theme does not put its own back.
fui::TextStyle onPaper(fui::TextStyle style, const fui::TextAlign align) {
  style.align = align;
  style.color = fui::Color::Black;
  return style;
}

// The theme binds smallText to the body slot, so the dense cut has to be named.
fui::TextStyle dense(const fui::TextAlign align) {
  fui::TextStyle style;
  style.font = toybox::kSmallFont;
  style.align = align;
  style.color = fui::Color::Black;
  return style;
}

fui::TextStyle inverted(fui::TextStyle style, const fui::TextAlign align) {
  style.align = align;
  style.color = fui::Color::White;
  return style;
}

void addButton(toybox::Screen& screen, fui::ButtonProps props, const fui::Rect& where) {
  if (!props.enabled) props.styles = toybox::disabledButtonStyles();
  screen.button(props, where);
}

struct Column {
  int16_t left;
  int16_t width;
};

Column pageColumn(const fui::DeviceContext& device) {
  const fui::Rect safe = device.safeRect();
  return Column{static_cast<int16_t>(safe.x + toybox::kMargin), static_cast<int16_t>(safe.width - toybox::kMargin * 2)};
}

// Rows of full-width doors anchored to the bottom margin, the way every front
// door in the fork ends.
fui::Rect doorsRect(const fui::DeviceContext& device, const int count) {
  const Column c = pageColumn(device);
  const int16_t height = static_cast<int16_t>(count * toybox::kPillHeight + (count - 1) * kDoorGap);
  return fui::makeRect(c.left, static_cast<int16_t>(device.safeRect().bottom() - toybox::kMargin - height), c.width,
                       height);
}

fui::Rect doorAt(const fui::Rect& doors, const int i) {
  return fui::makeRect(doors.x, static_cast<int16_t>(doors.y + i * (toybox::kPillHeight + kDoorGap)), doors.width,
                       toybox::kPillHeight);
}

// Two half-width buttons side by side at the bottom margin.
void pagingButtons(toybox::Screen& screen, const char* leftLabel, const fui::ActionId leftAction,
                   const bool leftEnabled, const char* rightLabel, const fui::ActionId rightAction,
                   const bool rightEnabled) {
  const fui::Rect row = doorsRect(screen.device(), 1);
  const int16_t half = static_cast<int16_t>((row.width - kDoorGap) / 2);
  fui::ButtonProps left;
  left.label = leftLabel;
  left.action = leftAction;
  left.enabled = leftEnabled;
  addButton(screen, left, fui::makeRect(row.x, row.y, half, row.height));
  fui::ButtonProps right;
  right.label = rightLabel;
  right.action = rightAction;
  right.enabled = rightEnabled;
  addButton(screen, right, fui::makeRect(static_cast<int16_t>(row.right() - half), row.y, half, row.height));
}

// Minutes as the record line and the calendar speak them: "45 MIN", "3 H 05".
void minutesLabel(char* out, const size_t cap, const uint64_t ms) {
  const uint64_t minutes = ms / kMinute;
  if (ms > 0 && minutes == 0) {
    std::snprintf(out, cap, "<1 MIN");
  } else if (minutes < 60) {
    std::snprintf(out, cap, "%u MIN", static_cast<unsigned>(minutes));
  } else {
    std::snprintf(out, cap, "%u H %02u", static_cast<unsigned>(minutes / 60), static_cast<unsigned>(minutes % 60));
  }
}

// The same corner brackets the Study timeline and the Connections grid wear.
void brackets(toybox::Screen& screen, const fui::Rect& box, const int arm) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int w = toybox::kFrame;
  for (int cx = 0; cx < 2; ++cx) {
    for (int cy = 0; cy < 2; ++cy) {
      const int x = cx == 0 ? box.x : box.right() - arm;
      const int y = cy == 0 ? box.y : box.bottom() - w;
      screen.target().fill(fui::makeRect(x, y, arm, w), ink);
      const int vx = cx == 0 ? box.x : box.right() - w;
      const int vy = cy == 0 ? box.y : box.bottom() - arm;
      screen.target().fill(fui::makeRect(vx, vy, w, arm), ink);
    }
  }
}

// The ornament: the last fortnight of reading as bars, with the goal as a
// dotted line across them. A day that met the goal is solid; one that fell
// short is dithered, so the two read apart without a legend.
void fortnight(toybox::Screen& screen, const fui::Rect& box, const HomeModel& model) {
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const uint32_t goalMs = model.goalMinutes > 0 ? model.goalMinutes * kMinute : 0;

  uint32_t peak = goalMs > 0 ? goalMs + goalMs / 4 : kDefaultGoalMs;
  bool any = false;
  for (const uint32_t ms : model.historyMs) {
    if (ms > peak) peak = ms;
    if (ms > 0) any = true;
  }

  const fui::Rect inner = fui::makeRect(
      static_cast<int16_t>(box.x + toybox::kGutter * 2), static_cast<int16_t>(box.y + toybox::kGutter),
      static_cast<int16_t>(box.width - toybox::kGutter * 4), static_cast<int16_t>(box.height - toybox::kGutter * 3));
  if (!any) {
    fui::TextStyle empty = onPaper(screen.theme().bodyText, fui::TextAlign::Center);
    screen.target().text(fui::makeRect(inner.x, static_cast<int16_t>(inner.y + inner.height / 2 - 15), inner.width, 30),
                         "NOTHING READ YET", empty);
    return;
  }

  const int gap = 4;
  const int barWidth = (inner.width - gap * (kHistoryDays - 1)) / kHistoryDays;
  const int baseline = inner.bottom();
  for (int column = 0; column < kHistoryDays; ++column) {
    const int ago = kHistoryDays - 1 - column;
    const uint32_t ms = model.historyMs[ago];
    const int x = inner.x + column * (barWidth + gap);
    if (ms > 0) {
      int height = static_cast<int>(static_cast<uint64_t>(ms) * inner.height / peak);
      if (height < 3) height = 3;
      const fui::Rect bar = fui::makeRect(x, baseline - height, barWidth, height);
      const bool met = goalMs == 0 || ms >= goalMs;
      screen.target().fill(bar, met ? ink : fui::Paint::dither(fui::Color::DarkGray));
    }
    if (ago == 0) screen.target().fill(fui::makeRect(x, baseline + 2, barWidth, toybox::kRule), ink);
  }
  screen.target().fill(fui::makeRect(inner.x, baseline, inner.width, toybox::kHairline), ink);

  if (goalMs > 0) {
    const int y = baseline - static_cast<int>(static_cast<uint64_t>(goalMs) * inner.height / peak);
    for (int x = inner.x; x < inner.right(); x += 8) {
      screen.target().fill(fui::makeRect(x, y, 4, 2), ink);
    }
  }
}

}  // namespace

// --- The front door ------------------------------------------------------

void buildHome(toybox::Screen& screen, const HomeModel& model) {
  chrome(screen, "READING");
  const Column c = pageColumn(screen.device());

  int16_t y = kBodyTop;
  const fui::Rect headline = fui::makeRect(c.left, y, c.width, 44);
  y = static_cast<int16_t>(y + 44 + 2);
  const fui::Rect state = fui::makeRect(c.left, y, c.width, 30);
  y = static_cast<int16_t>(y + 30 + toybox::kGutter);
  const fui::Rect rule = fui::makeRect(c.left, y, c.width, toybox::kRule);
  y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter);
  const fui::Rect record = fui::makeRect(c.left, y, c.width, 29);
  const fui::Rect record2 = fui::makeRect(c.left, static_cast<int16_t>(y + 29), c.width, 29);
  y = static_cast<int16_t>(y + 58 + toybox::kGutter * 2);
  const fui::Rect doors = doorsRect(screen.device(), 3);
  const fui::Rect ornament = fui::makeRect(c.left, y, c.width, static_cast<int16_t>(doors.y - toybox::kGutter * 2 - y));

  char text[64];
  if (model.clockSet) {
    minutesLabel(text, sizeof(text), model.todayMs);
  } else {
    std::snprintf(text, sizeof(text), "NO CLOCK");
  }
  screen.target().text(toybox::inkCentred(headline, toybox::kDisplayCut), text,
                       onPaper(screen.theme().titleText, fui::TextAlign::Left));
  screen.frame().hit(
      fui::makeRect(headline.x, headline.y, headline.width, static_cast<int16_t>(state.bottom() - headline.y)),
      ActionCalendar);

  if (!model.clockSet) {
    std::snprintf(text, sizeof(text), "Days start counting once the time is set");
  } else if (model.goalMinutes == 0) {
    std::snprintf(text, sizeof(text), "TODAY   %u PAGES", static_cast<unsigned>(model.todayPages));
  } else {
    const uint32_t goalMs = model.goalMinutes * kMinute;
    if (model.todayMs >= goalMs) {
      std::snprintf(text, sizeof(text), "GOAL MET   %u PAGES", static_cast<unsigned>(model.todayPages));
    } else {
      const unsigned left = static_cast<unsigned>((goalMs - model.todayMs + kMinute - 1) / kMinute);
      std::snprintf(text, sizeof(text), "%u MIN TO GOAL   %u PAGES", left, static_cast<unsigned>(model.todayPages));
    }
  }
  screen.target().text(state, text, onPaper(screen.theme().bodyText, fui::TextAlign::Left));

  screen.target().fill(rule, fui::Paint::solid(fui::Color::Black));

  char week[16];
  char month[16];
  minutesLabel(week, sizeof(week), model.weekMs);
  minutesLabel(month, sizeof(month), model.monthMs);
  std::snprintf(text, sizeof(text), "STREAK %d %s   BEST %d", model.streak, model.streak == 1 ? "DAY" : "DAYS",
                model.bestStreak);
  screen.target().text(record, text, dense(fui::TextAlign::Left));
  std::snprintf(text, sizeof(text), "LAST 7 DAYS %s   THIS MONTH %s", week, month);
  screen.target().text(record2, text, dense(fui::TextAlign::Left));

  brackets(screen, ornament, 24);
  fortnight(screen, ornament, model);

  const char* labels[3] = {"CALENDAR", "BOOKS", "SETTINGS"};
  const fui::ActionId actions[3] = {ActionCalendar, ActionBooks, ActionSettings};
  for (int i = 0; i < 3; ++i) {
    fui::ButtonProps props;
    props.label = labels[i];
    props.action = actions[i];
    props.enabled = i != 1 || model.bookCount > 0;
    addButton(screen, props, doorAt(doors, i));
  }
}

// --- The month -----------------------------------------------------------

namespace {
constexpr int16_t kWeekdayRow = 30;
constexpr int16_t kCellHeight = 58;
constexpr int kGridRows = 6;
}  // namespace

fui::Rect calendarGrid(const fui::DeviceContext& device) {
  const Column c = pageColumn(device);
  const int16_t cell = static_cast<int16_t>(c.width / 7);
  return fui::makeRect(static_cast<int16_t>(c.left + (c.width - cell * 7) / 2),
                       static_cast<int16_t>(kBodyTop + kWeekdayRow), static_cast<int16_t>(cell * 7),
                       static_cast<int16_t>(kCellHeight * kGridRows));
}

fui::Rect calendarCell(const fui::Rect& grid, const int row, const int column) {
  const int16_t w = static_cast<int16_t>(grid.width / 7);
  return fui::makeRect(static_cast<int16_t>(grid.x + column * w), static_cast<int16_t>(grid.y + row * kCellHeight), w,
                       kCellHeight);
}

int calendarDayAt(const fui::DeviceContext& device, const int firstWeekday, const int dayCount, const int x,
                  const int y) {
  const fui::Rect grid = calendarGrid(device);
  if (x < grid.x || y < grid.y || x >= grid.right() || y >= grid.bottom()) return 0;
  const int column = (x - grid.x) / (grid.width / 7);
  const int row = (y - grid.y) / kCellHeight;
  const int day = row * 7 + column - firstWeekday + 1;
  return day >= 1 && day <= dayCount ? day : 0;
}

void buildCalendar(toybox::Screen& screen, const CalendarModel& model) {
  char title[32];
  std::snprintf(title, sizeof(title), "%s %d", kMonthShort[(model.month + 11) % 12], model.year);
  chrome(screen, title);

  const fui::Rect grid = calendarGrid(screen.device());
  for (int column = 0; column < 7; ++column) {
    const fui::Rect cell = calendarCell(grid, 0, column);
    screen.target().text(fui::makeRect(cell.x, kBodyTop, cell.width, kWeekdayRow), kWeekdays[column],
                         dense(fui::TextAlign::Center));
  }

  const uint32_t full = model.goalMs > 0 ? model.goalMs : kDefaultGoalMs;
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  for (int day = 1; day <= model.dayCount && day <= 31; ++day) {
    const int index = model.firstWeekday + day - 1;
    const fui::Rect cell = calendarCell(grid, index / 7, index % 7);
    const fui::Rect face = fui::makeRect(static_cast<int16_t>(cell.x + 3), static_cast<int16_t>(cell.y + 3),
                                         static_cast<int16_t>(cell.width - 6), static_cast<int16_t>(cell.height - 6));
    const uint32_t ms = model.ms[day - 1];
    bool solid = false;
    if (ms >= full) {
      screen.target().fill(face, ink);
      solid = true;
    } else if (ms >= full / 2) {
      screen.target().fill(face, fui::Paint::dither(fui::Color::DarkGray));
    } else if (ms >= kMinute) {
      screen.target().fill(face, fui::Paint::dither(fui::Color::LightGray));
    } else {
      screen.target().stroke(face, ink, toybox::kHairline);
    }

    char number[12];
    std::snprintf(number, sizeof(number), "%d", day);
    // A dithered ground is half ink, so the number sits on a paper chip there.
    const fui::Rect label = toybox::inkCentred(face, toybox::kUiCut);
    if (!solid && ms >= kMinute) {
      const int16_t chipW = 36;
      const int16_t chipH = 33;
      screen.target().fill(fui::makeRect(static_cast<int16_t>(face.x + (face.width - chipW) / 2),
                                         static_cast<int16_t>(face.y + (face.height - chipH) / 2), chipW, chipH),
                           fui::Paint::solid(fui::Color::White), 4);
    }
    screen.target().text(label, number,
                         solid ? inverted(screen.theme().bodyText, fui::TextAlign::Center)
                               : onPaper(screen.theme().bodyText, fui::TextAlign::Center));

    if (day == model.today) {
      const fui::Rect tick = fui::makeRect(static_cast<int16_t>(face.x + 6), static_cast<int16_t>(face.bottom() - 7),
                                           static_cast<int16_t>(face.width - 12), toybox::kRule);
      screen.target().fill(tick, solid ? fui::Paint::solid(fui::Color::White) : ink);
    }
    if (day == model.selected) {
      screen.target().stroke(cell, ink, toybox::kRule);
      const fui::Rect inner =
          fui::makeRect(static_cast<int16_t>(face.x + 3), static_cast<int16_t>(face.y + 3),
                        static_cast<int16_t>(face.width - 6), static_cast<int16_t>(face.height - 6));
      screen.target().stroke(inner, solid ? fui::Paint::solid(fui::Color::White) : ink, 2);
    }
  }
  // One target for the whole month: 31 cells would overflow the interaction
  // table, so the Activity resolves the day from the tap with calendarDayAt().
  screen.frame().hit(grid, ActionPickDay);

  const Column c = pageColumn(screen.device());
  const int16_t summaryY = static_cast<int16_t>(grid.bottom() + toybox::kGutter);
  const fui::Rect line1 = fui::makeRect(c.left, summaryY, c.width, 42);
  const fui::Rect line2 = fui::makeRect(c.left, static_cast<int16_t>(summaryY + 40), c.width, 30);

  char head[48];
  char detail[64];
  if (model.selected > 0) {
    char minutes[16];
    minutesLabel(minutes, sizeof(minutes), model.ms[model.selected - 1]);
    std::snprintf(head, sizeof(head), "%s %d   %s", kMonthShort[(model.month + 11) % 12], model.selected, minutes);
    std::snprintf(detail, sizeof(detail), "%u PAGES", static_cast<unsigned>(model.pages[model.selected - 1]));
  } else {
    uint64_t total = 0;
    uint32_t pages = 0;
    int days = 0;
    int met = 0;
    for (int d = 0; d < model.dayCount && d < 31; ++d) {
      total += model.ms[d];
      pages += model.pages[d];
      if (model.ms[d] >= kMinute) ++days;
      if (model.goalMs > 0 && model.ms[d] >= model.goalMs) ++met;
    }
    char minutes[16];
    minutesLabel(minutes, sizeof(minutes), total);
    std::snprintf(head, sizeof(head), "%s   %s", kMonthNames[(model.month + 11) % 12], minutes);
    if (model.goalMs > 0) {
      std::snprintf(detail, sizeof(detail), "%d DAYS READ   %d AT GOAL   %u PAGES", days, met,
                    static_cast<unsigned>(pages));
    } else {
      std::snprintf(detail, sizeof(detail), "%d DAYS READ   %u PAGES", days, static_cast<unsigned>(pages));
    }
  }
  screen.target().text(line1, head, onPaper(screen.theme().bodyText, fui::TextAlign::Left));
  screen.target().text(line2, detail, dense(fui::TextAlign::Left));

  pagingButtons(screen, "EARLIER", ActionPrevMonth, true, "LATER", ActionNextMonth, model.canGoNext);
}

// --- Books ---------------------------------------------------------------

fui::Rect booksBand(const fui::DeviceContext& device) {
  const Column c = pageColumn(device);
  const fui::Rect buttons = doorsRect(device, 1);
  return fui::makeRect(c.left, kBodyTop, c.width, static_cast<int16_t>(buttons.y - toybox::kGutter - kBodyTop));
}

void buildBooks(toybox::Screen& screen, const BooksModel& model) {
  chrome(screen, "BOOKS", model.rightLabel);

  const fui::Rect band = booksBand(screen.device());
  const fui::Rect panel = screen.device().screen();

  fui::ListProps props;
  props.items = model.items;
  props.count = static_cast<uint16_t>(model.count < 0 ? 0 : model.count);
  props.selectedIndex = -1;
  props.action = ActionOpenBook;
  fui::TextStyle label = onPaper(screen.theme().bodyText, fui::TextAlign::Left);
  props.labelText = label;
  props.valueText = onPaper(screen.theme().smallText, fui::TextAlign::Right);
  screen.setContentMarginAbsolute(fui::Insets{band.y, static_cast<int16_t>(panel.width - band.right()),
                                              static_cast<int16_t>(panel.height - band.bottom()), band.x});
  screen.list(props, band.height);

  pagingButtons(screen, "NEWER", ActionPageNewer, model.canPageNewer, "OLDER", ActionPageOlder, model.canPageOlder);
}

void buildBook(toybox::Screen& screen, const BookModel& model) {
  chrome(screen, "BOOK");
  const Column c = pageColumn(screen.device());

  int16_t y = kBodyTop;
  fui::TextStyle titleStyle = onPaper(screen.theme().bodyText, fui::TextAlign::Left);
  titleStyle.maxLines = 3;
  titleStyle.bold = true;
  const int16_t titleH = screen.target().measureText(titleStyle.font, "Ag", titleStyle).height;
  // Measured by wrapping, so a short title does not leave three lines of air.
  int lines = 1;
  {
    const int16_t whole = screen.target().measureText(titleStyle.font, model.title, titleStyle).width;
    lines = whole > c.width ? (whole > c.width * 2 ? 3 : 2) : 1;
  }
  const fui::Rect title = fui::makeRect(c.left, y, c.width, static_cast<int16_t>(titleH * lines + 4));
  screen.target().text(title, model.title, titleStyle);
  y = static_cast<int16_t>(title.bottom() + 4);

  if (model.author != nullptr && model.author[0] != '\0') {
    fui::TextStyle authorStyle = onPaper(screen.theme().smallText, fui::TextAlign::Left);
    authorStyle.maxLines = 2;
    const int16_t authorH = screen.target().measureText(authorStyle.font, "Ag", authorStyle).height;
    const int16_t whole = screen.target().measureText(authorStyle.font, model.author, authorStyle).width;
    const int authorLines = whole > c.width ? 2 : 1;
    const fui::Rect author = fui::makeRect(c.left, y, c.width, static_cast<int16_t>(authorH * authorLines + 4));
    screen.target().text(author, model.author, authorStyle);
    y = static_cast<int16_t>(author.bottom());
  }

  y = static_cast<int16_t>(y + toybox::kGutter);
  screen.target().fill(fui::makeRect(c.left, y, c.width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
  y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter);

  constexpr int16_t kLineHeight = 44;
  const int16_t bottom = static_cast<int16_t>(screen.device().safeRect().bottom() - toybox::kMargin);
  for (int i = 0; i < model.lineCount && y + kLineHeight <= bottom; ++i) {
    const fui::Rect row = fui::makeRect(c.left, y, c.width, kLineHeight);
    screen.target().text(row, model.lines[i].label, onPaper(screen.theme().smallText, fui::TextAlign::Left));
    screen.target().text(row, model.lines[i].value, onPaper(screen.theme().bodyText, fui::TextAlign::Right));
    screen.target().fill(fui::makeRect(c.left, static_cast<int16_t>(row.bottom() - 1), c.width, toybox::kHairline),
                         fui::Paint::dither(fui::Color::DarkGray));
    y = static_cast<int16_t>(y + kLineHeight);
  }
}

// --- Settings --------------------------------------------------------------

void buildSettings(toybox::Screen& screen, const SettingsModel& model) {
  chrome(screen, "SETTINGS");
  const Column c = pageColumn(screen.device());

  static constexpr const char* kLabels[kSettingRows] = {"DAILY GOAL", "TIME LEFT", "IDLE LIMIT"};
  fui::ListItem items[kSettingRows];
  for (int i = 0; i < kSettingRows; ++i) {
    items[i].label = kLabels[i];
    items[i].value = model.values[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  const int16_t listH = static_cast<int16_t>(kSettingRows * (toybox::kRowHeight + toybox::kGutter));
  const fui::Rect panel = screen.device().screen();
  fui::ListProps props;
  props.items = items;
  props.count = kSettingRows;
  props.selectedIndex = -1;
  props.action = ActionCycleSetting;
  props.labelText = onPaper(screen.theme().bodyText, fui::TextAlign::Left);
  props.valueText = onPaper(screen.theme().bodyText, fui::TextAlign::Right);
  screen.setContentMarginAbsolute(fui::Insets{kBodyTop, static_cast<int16_t>(panel.width - c.left - c.width),
                                              static_cast<int16_t>(panel.height - kBodyTop - listH), c.left});
  screen.list(props, listH);

  fui::TextStyle note = dense(fui::TextAlign::Left);
  note.maxLines = 8;
  screen.target().text(
      fui::makeRect(c.left, static_cast<int16_t>(kBodyTop + listH + toybox::kGutter), c.width, 240),
      "Tap a row to change it. The time left shows in the reader's status bar once a few pages are read, "
      "and a page left open longer than the idle limit counts as the limit.",
      note);
}

}  // namespace statsui
