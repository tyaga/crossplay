#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <Fb2Book.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "../../apps_local/Shelf.h"  // fork-local seam
#include "../network/CrossPointWebServerActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

// --- fork-local seam ---------------------------------------------------
// Library spans the width; everything after it sits two to a row, in this
// order: Study, Apps / Settings, Wi-Fi.
int HomeActivity::menuRows(MenuRow* rows) const {
  int n = 0;
  rows[n++] = {MenuRow::Kind::Library, 0};
  for (int i = 0; i < shelf::homeItemCount() && n < MAX_MENU_ROWS; ++i) rows[n++] = {MenuRow::Kind::HomeItem, i};
  for (int i = 0; i < shelf::folderCount() && n < MAX_MENU_ROWS; ++i) rows[n++] = {MenuRow::Kind::Folder, i};
  if (n < MAX_MENU_ROWS) rows[n++] = {MenuRow::Kind::Settings, 0};
  if (n < MAX_MENU_ROWS) rows[n++] = {MenuRow::Kind::Wifi, 0};
  return n;
}

// Rows from `top` to `bottom` with equal gaps between them: the first row
// right under the covers, the last on the bottom edge. The first
// `fullWidthEntries` take a row each, the rest share rows two by two.
void HomeActivity::layoutMenu(const int top, const int bottom, const int entries, const int fullWidthEntries) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int rowHeight = GUI.getMenuRowHeight(renderer);
  const int gridEntries = std::max(0, entries - fullWidthEntries);
  const int bands = std::max(1, fullWidthEntries + (gridEntries + 1) / 2);
  const int pitch = bands > 1 ? std::max(rowHeight, (bottom - top - rowHeight) / (bands - 1)) : 0;
  // drawButtonMenu insets its tile by the side padding on both edges; halving
  // that between the columns keeps the gap equal to the outer margins.
  const int half = metrics.contentSidePadding / 2;
  cellCount = 0;
  for (int e = 0; e < entries && cellCount < static_cast<int>(sizeof(cells) / sizeof(cells[0])); ++e) {
    Cell cell{};
    int band;
    if (e < fullWidthEntries) {
      band = e;
      cell.x = 0;
      cell.w = pageWidth;
    } else {
      const int g = e - fullWidthEntries;
      band = fullWidthEntries + g / 2;
      const bool left = g % 2 == 0;
      cell.x = left ? 0 : pageWidth / 2 - half;
      cell.w = pageWidth / 2 + half;
    }
    cell.y = top + band * pitch;
    cell.h = rowHeight;
    cells[cellCount++] = cell;
  }
}

// The selector index of a row, counting the recent books ahead of the menu.
int HomeActivity::menuRowOf(const MenuRow::Kind kind, const int index) const {
  MenuRow rows[MAX_MENU_ROWS];
  const int count = menuRows(rows);
  for (int i = 0; i < count; ++i) {
    if (rows[i].kind == kind && rows[i].index == index) return static_cast<int>(recentBooks.size()) + i;
  }
  return 0;
}

int HomeActivity::getMenuItemCount() const {
  MenuRow rows[MAX_MENU_ROWS];
  return menuRows(rows) + static_cast<int>(recentBooks.size());
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        // fork-local seam: an FB2 book's cover comes from the EPUB made from it.
        if (FsHelpers::hasEpubExtension(book.path) || fb2::isFb2Path(book.path)) {
          Epub epub(fb2::isFb2Path(book.path) ? fb2::epubPathFor(book.path) : book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // fork-local seam: goHome() names the departing upstream screen; the shelf
  // remembers its own rows, which HomeMenuItem cannot name.
  switch (initialMenuItem) {
    case HomeMenuItem::LIBRARY:
      selectorIndex = menuRowOf(MenuRow::Kind::Library, 0);
      break;
    case HomeMenuItem::FILE_TRANSFER:
      selectorIndex = menuRowOf(MenuRow::Kind::Wifi, 0);
      break;
    case HomeMenuItem::SETTINGS_MENU:
      selectorIndex = menuRowOf(MenuRow::Kind::Settings, 0);
      break;
    case HomeMenuItem::FILE_BROWSER:
    case HomeMenuItem::OPDS_BROWSER:
      selectorIndex = menuRowOf(MenuRow::Kind::Folder, 0);
      break;
    default:
      if (const int folder = shelf::lastFolderOnHome(); folder >= 0) {
        selectorIndex = menuRowOf(MenuRow::Kind::Folder, folder);
      } else if (const int item = shelf::lastHomeItemOnHome(); item >= 0) {
        selectorIndex = menuRowOf(MenuRow::Kind::HomeItem, item);
      } else {
        selectorIndex = 0;
      }
      break;
  }

  // fork-local seam: boot straight into a named app when the environment asks
  // for one (the site's installer preview, CROSSPLAY_AUTOSTART=solitaire ./bin/sim).
  // Fires once per process; on hardware getenv finds nothing and this is free.
  // Safe from onEnter because replaceActivity defers to the end of the loop.
  shelf::autostartFromEnv(renderer, mappedInput);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    MenuRow rows[MAX_MENU_ROWS];
    if (menuIndex < 0 || menuIndex >= menuRows(rows)) return;
    switch (rows[menuIndex].kind) {
      case MenuRow::Kind::Library:
        onLibraryOpen();
        break;
      case MenuRow::Kind::Wifi:
        onWifiOpen();
        break;
      case MenuRow::Kind::HomeItem:
        shelf::openHomeItem(rows[menuIndex].index, renderer, mappedInput);
        break;
      case MenuRow::Kind::Folder:
        shelf::openFolder(rows[menuIndex].index, renderer, mappedInput);
        break;
      case MenuRow::Kind::Settings:
        onSettingsOpen();
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  // Hit areas follow the DRAWN geometry: the cover tile and the menu cells
  // render() recorded.
  const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
  const int recentCount = std::min(static_cast<int>(recentBooks.size()), coverColumnCount);
  const int coverColumnWidth = (renderer.getScreenWidth() - 2 * metrics.contentSidePadding) / coverColumnCount;
  const int coverBottom = coverRectH > 0 ? coverRectY + coverRectH : metrics.homeTopPadding;
  int touchedBook = -1;
  const auto coverTouch =
      metrics.homeContinueReadingInMenu
          ? MappedInputManager::RowTouch::None
          : mappedInput.colTouch(touchedBook, metrics.contentSidePadding, coverColumnWidth, recentCount,
                                 metrics.homeTopPadding, coverBottom, coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedBook) {
        selectorIndex = touchedBook;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedBook;
      activateSelection();
    }
    return;
  }

  for (int c = 0; c < cellCount; ++c) {
    const Cell& cell = cells[c];
    int row = -1;
    const auto touch = mappedInput.rowTouch(row, cell.y, cell.h, 1, cell.x + metrics.contentSidePadding,
                                            cell.x + cell.w - metrics.contentSidePadding, cell.h);
    if (touch == MappedInputManager::RowTouch::None) continue;
    if (touch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != cell.selector) {
        selectorIndex = cell.selector;
        requestUpdate();
      }
    } else {
      selectorIndex = cell.selector;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band (and a
  // centered title, e.g. RoundedRaff's book title) sinks into the tile.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // fork-local seam: the entries come from menuRows(), which the dispatch in
  // loop() walks too. Shelf titles are raw rather than tr(): routing them
  // through i18n would mean editing lib/I18n/translations/*.yaml per app.
  MenuRow menuRowList[MAX_MENU_ROWS];
  const int rowCount = menuRows(menuRowList);
  const char* labels[MAX_MENU_ROWS + 1] = {};
  UIIcon icons[MAX_MENU_ROWS + 1] = {};
  int selectors[MAX_MENU_ROWS + 1] = {};
  int entries = 0;
  const bool continueRow = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  if (continueRow) {
    labels[entries] = tr(STR_CONTINUE_READING);
    icons[entries] = Book;
    selectors[entries++] = 0;
  }
  for (int i = 0; i < rowCount; ++i) {
    switch (menuRowList[i].kind) {
      case MenuRow::Kind::Library:
        labels[entries] = tr(STR_LIBRARY);
        icons[entries] = Library;
        break;
      case MenuRow::Kind::HomeItem:
        labels[entries] = shelf::homeItems()[menuRowList[i].index].title;
        icons[entries] = shelf::homeItemIcon(menuRowList[i].index);
        break;
      case MenuRow::Kind::Folder:
        labels[entries] = shelf::folders()[menuRowList[i].index].title;
        icons[entries] = shelf::folders()[menuRowList[i].index].icon;
        break;
      case MenuRow::Kind::Settings:
        labels[entries] = tr(STR_SETTINGS_TITLE);
        icons[entries] = Settings;
        break;
      case MenuRow::Kind::Wifi:
        labels[entries] = tr(STR_HOME_WIFI);
        icons[entries] = Wifi;
        break;
    }
    selectors[entries++] = static_cast<int>(recentBooks.size()) + i;
  }

  // The cover tile sits right under the header and the menu takes everything
  // below it, so no band of the screen is left empty.
  const int coverTileHeight = metrics.homeCoverTileHeight;
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = coverTileHeight;
  if (coverTileHeight > 0) {
    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, coverTileHeight}, recentBooks,
                            selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));
  }

  const int fullWidth = continueRow ? 2 : 1;
  const int menuTop = metrics.homeTopPadding + coverTileHeight + metrics.homeMenuTopOffset / 2;
  layoutMenu(menuTop, pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing, entries, fullWidth);
  for (int e = 0; e < cellCount; ++e) {
    cells[e].selector = selectors[e];
    const char* label = labels[e];
    const UIIcon icon = icons[e];
    GUI.drawButtonMenu(
        renderer, Rect{cells[e].x, cells[e].y, cells[e].w, cells[e].h}, 1, selectorIndex == selectors[e] ? 0 : -1,
        [label](int) { return std::string(label); }, [icon](int) { return icon; }, 0);
  }
  // A rule under the full-width entries, halfway down the gap to the grid.
  if (cellCount > fullWidth) {
    const int above = cells[fullWidth - 1].y + cells[fullWidth - 1].h;
    const int lineY = (above + cells[fullWidth].y) / 2;
    renderer.drawLine(metrics.contentSidePadding, lineY, pageWidth - metrics.contentSidePadding, lineY, true);
  }

  const auto hints = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                           tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

// fork-local seam: File Transfer's own first choice, joining a Wi-Fi network,
// without the choice.
void HomeActivity::onWifiOpen() {
  auto activity = makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInput, /*joinNetworkOnly=*/true);
  if (!activity) {
    LOG_ERR("HOME", "OOM: Wi-Fi activity");
    return;
  }
  activityManager.replaceActivity(std::move(activity));
}
