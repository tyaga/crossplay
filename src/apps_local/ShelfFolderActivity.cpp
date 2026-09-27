#include "ShelfFolderActivity.h"

#include <FreeInkUIIcon.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

ShelfFolderActivity::ShelfFolderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const int folderIndex)
    : UiListActivity("ShelfFolder", renderer, mappedInput), folder(folderIndex) {
  const shelf::Folder& self = shelf::folders()[folder];
  rowCount = std::min(self.count, MAX_ROWS);
  if (self.count > MAX_ROWS) LOG_ERR("SHELF", "%s has %d items; showing %d", self.title, self.count, MAX_ROWS);
  for (int i = 0; i < rowCount; i++) {
    fui::ListItem item;
    item.label = self.items[i].title;
    item.icon = fui::bitmapFromIcon(*self.items[i].icon);
    item.actionValue = static_cast<int16_t>(i);
    rows[i] = item;
  }
}

std::unique_ptr<Activity> ShelfFolderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                      const int folderIndex) {
  if (folderIndex < 0 || folderIndex >= shelf::folderCount()) {
    LOG_ERR("SHELF", "Bad folder index: %d", folderIndex);
    return nullptr;
  }
  return makeUniqueNoThrow<ShelfFolderActivity>(renderer, mappedInput, folderIndex);
}

void ShelfFolderActivity::onEnter() {
  nav.selected = std::clamp(shelf::resumeRowIn(folder), 0, std::max(0, rowCount - 1));
  nav.followOnBuild = true;
  UiListActivity::onEnter();
}

int ShelfFolderActivity::listCount() const { return rowCount; }

const char* ShelfFolderActivity::headerTitle() const { return shelf::folders()[folder].title; }

void ShelfFolderActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rows;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void ShelfFolderActivity::navigateButtons() {
  const int before = nav.selected;
  UiListActivity::navigateButtons();
  // Remembered as it moves, so walking out without opening anything comes back
  // to the same row; the device can sleep here, and wake is a chip reset.
  if (nav.selected != before && nav.selected >= 0 && nav.selected < rowCount)
    shelf::rememberRowIn(folder, nav.selected);
}

void ShelfFolderActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount) return;
  // The next screen has its own surfaces; a lingering tap flash would gray an
  // unrelated element there.
  app.clearTapFlash();
  nav.selected = index;
  shelf::openItem(folder, index, renderer, mappedInput);
}

void ShelfFolderActivity::onBackButton() { shelf::leave(renderer, mappedInput); }
