#pragma once

// READING: what the reader has recorded, as today, a month, and a shelf of
// books, plus the three settings the recording and the status bar use.
//
// The recording itself is not here: it runs whether or not this app is open,
// from the hooks in ReadingStats.h. This screen only reads the store and edits
// its settings.

#include <memory>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "StatsScreens.h"

class StatsActivity final : public Activity {
 public:
  StatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStats", renderer, mappedInput) {}
  ~StatsActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Home, Calendar, Books, Book, Settings };

  void handleAction(freeink::ui::ActionId action, int16_t value, int tapX, int tapY);
  void stepMonth(int delta);
  bool monthIsCurrent() const;
  void openBooks();
  void pageBooks(int delta);
  void cycleSetting(int row);

  void renderHome(toybox::Screen& screen);
  void renderCalendar(toybox::Screen& screen);
  void renderBooks(toybox::Screen& screen);
  void renderBook(toybox::Screen& screen);
  void renderSettings(toybox::Screen& screen);

  View view_ = View::Home;

  int32_t today_ = -1;
  int calYear_ = 2026;
  int calMonth_ = 1;
  int calSelected_ = 0;
  int calFirstWeekday_ = 0;
  int calDayCount_ = 30;

  std::vector<int> order_;  // book indices, most recent first
  int booksFirst_ = 0;
  int book_ = -1;

  toybox::Interactions interactions_{};
  bool interactionsReady_ = false;
};
