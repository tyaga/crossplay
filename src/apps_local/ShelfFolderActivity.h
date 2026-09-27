#pragma once

#include <memory>

#include "Shelf.h"
#include "activities/UiListActivity.h"

// A shelf folder: its items as one list in the theme's own style, the way
// upstream's list screens look. Opening a row launches the item; Back returns
// Home. The row it reopens on is the shelf's to remember (shelf::resumeRowIn).
class ShelfFolderActivity final : public UiListActivity {
 public:
  ShelfFolderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int folderIndex);

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput, int folderIndex);

  void onEnter() override;

 private:
  // Enough for the largest folder; Shelf.cpp's registry is checked against it.
  static constexpr int MAX_ROWS = 24;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void navigateButtons() override;
  void onBackButton() override;
  const char* headerTitle() const override;

  const int folder;
  freeink::ui::ListItem rows[MAX_ROWS]{};
  int rowCount = 0;
};
