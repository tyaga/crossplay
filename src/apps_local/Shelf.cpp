#include "Shelf.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../activities/ActivityManager.h"
#include "ShelfFolderActivity.h"
#include "ShelfState.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "battleship/BattleshipActivity.h"
#include "hackernews/HackerNewsActivity.h"
#include "player/PlayerActivity.h"
#include "readingstats/StatsActivity.h"
#include "solitaire/SolitaireActivity.h"
#include "study/StudyActivity.h"
#include "ui/ToyboxIcons.h"
#include "wallpapers/WallpapersActivity.h"
#include "xkcd/XkcdActivity.h"

namespace {

// Upstream's file browser and file transfer, as shelf items. They leave
// through goHome(), which hands them back to the folder; see
// shelf::leaveToFolder().
std::unique_ptr<Activity> createFileBrowser(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, "/");
}

std::unique_ptr<Activity> createFileTransfer(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInput);
}

// Icons come from tools_local/toybox/icons.txt via Lucide. Title Case, like
// the theme's other lists.
constexpr shelf::Item kApps[] = {
    {"Browse", &icon_browse_32, &createFileBrowser},
    {"File Transfer", &icon_filetransfer_32, &createFileTransfer},
    {"Reading", &icon_readingstats_32, &StatsActivity::create},
    {"Hacker News", &icon_hackernews_32, &HackerNewsActivity::create},
    {"xkcd", &icon_xkcd_32, &XkcdActivity::create},
    {"Get Books", &icon_getbooks_32, &OpdsBookBrowserActivity::create},
    {"Wallpapers", &icon_wallpapers_32, &WallpapersActivity::create},
    {"Solitaire", &icon_solitaire_32, &SolitaireActivity::create},
    {"Battleship", &icon_battleship_32, &BattleshipActivity::create},
    // The name and face Battleship shows the other device.
    {"Player", &icon_player_32, &PlayerActivity::create},
};

// Apps Home launches directly, without a folder in between.
constexpr shelf::Item kHomeItems[] = {
    {"Study", &icon_study_32, &StudyActivity::create},
};

constexpr int kHomeItemCount = static_cast<int>(sizeof(kHomeItems) / sizeof(kHomeItems[0]));
// Home draws upstream's icon palette, not the item's own icon.
constexpr UIIcon kHomeItemIcons[] = {UIIcon::Study};
static_assert(sizeof(kHomeItemIcons) / sizeof(kHomeItemIcons[0]) == kHomeItemCount,
              "every Home item needs a UIIcon for Home's list");

// The folders Home shows beside its own entries.
constexpr shelf::Folder kFolders[] = {
    {"Apps", UIIcon::Apps, kApps, static_cast<int>(sizeof(kApps) / sizeof(shelf::Item))},
};

constexpr int kFolderCount = static_cast<int>(sizeof(kFolders) / sizeof(kFolders[0]));

// An item with no icon draws a blank gutter and nothing says so. Caught at
// compile time rather than in a test, because a test can be forgotten and this
// cannot: a new row without an icon does not build.
constexpr bool everyItemHasAnIcon() {
  for (const auto& folder : kFolders) {
    for (int i = 0; i < folder.count; ++i) {
      if (folder.items[i].icon == nullptr) return false;
    }
  }
  for (const auto& item : kHomeItems) {
    if (item.icon == nullptr) return false;
  }
  return true;
}
static_assert(everyItemHasAnIcon(), "every shelf item needs an icon; see tools_local/toybox/icons.txt");

// Beside the reader's own state and the player's name, so clearing
// `.crosspoint/` clears this too and there is one place to look. Inside the
// guard because the host build has no storage and an unused constant is a
// -Werror failure there.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
constexpr char kStatePath[] = "/.crosspoint/shelf.cfg";
#endif

// Where leave() sends an app. Set when an item is opened, read when it leaves.
// -1 means "nothing is open below Home", which is what a folder itself sees.
int openFolderIndex = -1;

// The Home item last opened, for Home's cursor on the way back. RAM only: wake
// resumes the item itself, and a cold boot starts at the top anyway.
int lastHomeItem = -1;

// The remembered position, mirroring /.crosspoint/shelf.cfg:
//
// - `lastFolder`: which shelf row Home should land on when you come back out.
//   CrossPoint restores Home's selection by matching the departing activity's
//   name against its own HomeMenuItem list, which cannot know about ours, so
//   without this you leave a folder and the cursor is sitting on the top row.
// - `resumeRow`: per folder, the row it reopens on. The row of the item last
//   opened from it, or -- when you paged and then walked out without opening
//   anything -- the first row of the page you were looking at. It is the page
//   you were ON, not the page holding the game you last played; those are the
//   same thing until you browse and leave, and browsing and leaving is the case
//   that was wrong.
// - `openTitle`: the item that was open when the device went to sleep, which is
//   what wake reopens instead of dropping you on Home.
//
// All of it survives the device going to sleep, which none of it did before.
// `main.cpp` deep-sleeps on the idle timeout and says of it that wake is
// effectively a chip reset, so every time Mario put the device down and came
// back the shelf had forgotten which game he was playing.
//
// Written next to the reader's own state rather than into CrossPointState,
// which is upstream's file: a fork-local fact belongs in a fork-local file, and
// player.cfg already established the pattern.
shelf::State state;
bool stateLoaded = false;

// The Activity that the open item launched, by name. `openFolderIndex` alone
// cannot answer "is that item still what is on screen": the Home gesture leaves
// an app without going through leave(), so a sleep from Home would otherwise
// record the game you left ten minutes ago as still open. A pushed sub-screen
// (the frontlight panel) also fails this check, and falls back to Home the way
// every wake used to.
std::string openActivityName;

// A title that does not fit the state file cannot be resumed, and would fail
// silently at the write. Caught at compile time instead, next to the icon and
// mark checks, so a long-titled new game does not build.
constexpr bool everyTitleFitsTheStateFile() {
  for (const auto& folder : kFolders) {
    for (int i = 0; i < folder.count; ++i) {
      if (shelf::constexprLength(folder.items[i].title) > shelf::MAX_ITEM_TITLE) return false;
    }
  }
  for (const auto& item : kHomeItems) {
    if (shelf::constexprLength(item.title) > shelf::MAX_ITEM_TITLE) return false;
  }
  return true;
}
static_assert(everyTitleFitsTheStateFile(), "shelf item titles must fit shelf::MAX_ITEM_TITLE; see ShelfState.h");
static_assert(kFolderCount <= shelf::MAX_FOLDERS, "raise shelf::MAX_FOLDERS in ShelfState.h");

// Row limits as the registry stands now, for parseState's clamp.
const int* itemLimits() {
  static int limits[shelf::MAX_FOLDERS] = {};
  for (int i = 0; i < kFolderCount; ++i) limits[i] = kFolders[i].count - 1;
  return limits;
}

// The folder and row of the item with this title, case-insensitively. The one
// place a title is turned back into a row, shared by wake and by the
// autostart environment variable. A Home item answers with folder -1.
bool findItemByTitle(const char* title, int& folder, int& item) {
  for (int i = 0; i < kHomeItemCount; ++i) {
    if (strcasecmp(kHomeItems[i].title, title) == 0) {
      folder = -1;
      item = i;
      return true;
    }
  }
  for (int f = 0; f < kFolderCount; ++f) {
    for (int i = 0; i < kFolders[f].count; ++i) {
      if (strcasecmp(kFolders[f].items[i].title, title) == 0) {
        folder = f;
        item = i;
        return true;
      }
    }
  }
  return false;
}

// The parse and the format live in ShelfState.cpp, where a host test can reach
// them without a card: the file has to survive a truncated write, a file
// written before wake could resume, and a game renamed since it was written.
void loadState() {
  stateLoaded = true;
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (!Storage.exists(kStatePath)) return;
  char buffer[96] = {};
  if (Storage.readFileToBuffer(kStatePath, buffer, sizeof(buffer)) == 0) return;
  // Fails quietly: the worst a corrupt file can cost is starting at the top,
  // and there is nothing for anyone to do about it.
  shelf::parseState(buffer, kFolderCount, itemLimits(), state);
#endif
}

void saveState() {
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  char line[96];
  const size_t used = shelf::formatState(state, kFolderCount, line, sizeof(line));
  if (used == 0) {
    LOG_ERR("SHELF", "State line did not fit %d bytes", static_cast<int>(sizeof(line)));
    return;
  }
  Storage.writeFile(kStatePath, String(line));
#endif
}

// Every path that reads or writes the remembered position goes through this
// first. Lazily rather than at boot because the shelf has no init hook, and
// unconditionally rather than only on the read paths because openFolder passes
// the current resumeRow back in: without the load, the first navigation of a
// session would write the defaults over the saved file and the persistence
// would silently do nothing.
void ensureLoaded() {
  if (!stateLoaded) loadState();
}

// Only when something actually changed. Opening a folder happens on every Back,
// and SPIFFS sectors have a finite erase count, so an unconditional write here
// would be a write per navigation for no gain.
void saveIfChanged(const int folder, const int row) {
  ensureLoaded();
  if (state.lastFolder == folder && (folder < 0 || state.resumeRow[folder] == row)) return;
  state.lastFolder = folder;
  if (folder >= 0) state.resumeRow[folder] = row;
  saveState();
}

// The item wake should reopen, or none. Kept separate from saveIfChanged
// because the two facts change on different events: the position changes as you
// navigate, this changes when an item opens, closes, or is left behind.
void setOpenTitle(const char* title) {
  ensureLoaded();
  const char* wanted = title == nullptr ? "" : title;
  if (strcmp(state.openTitle, wanted) == 0) return;
  snprintf(state.openTitle, sizeof(state.openTitle), "%s", wanted);
  saveState();
}

// Replaces the running activity, or logs and stays put. Every launch in this
// file funnels through here so an OOM cannot leave the shelf thinking it opened
// something it did not.
bool replaceWith(std::unique_ptr<Activity> activity, const char* what) {
  if (!activity) {
    LOG_ERR("SHELF", "OOM opening %s", what);
    return false;
  }
  activityManager.replaceActivity(std::move(activity));
  // Read back from the activity itself rather than written down beside the
  // registry: a table mapping titles to activity names would be a second copy
  // of a fact the activity already carries, and would rot the first time one is
  // renamed. Asked after the request, because Activity::name is not ours to
  // read directly.
  openActivityName = activityManager.currentActivityName();
  return true;
}

}  // namespace

namespace shelf {

const Folder* folders() { return kFolders; }

int folderCount() { return kFolderCount; }

void openFolder(const int index, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (index < 0 || index >= kFolderCount) {
    LOG_ERR("SHELF", "Bad folder index: %d", index);
    return;
  }
  // Opening a folder means nothing below it is open any more. Clearing here
  // rather than in leave() keeps the fact true even when a folder is reached by
  // some route that did not go through leave().
  openFolderIndex = -1;
  lastHomeItem = -1;
  ensureLoaded();
  saveIfChanged(index, state.resumeRow[index]);
  setOpenTitle(nullptr);
  replaceWith(ShelfFolderActivity::create(renderer, mappedInput, index), kFolders[index].title);
}

bool openItem(const int folder, const int item, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (folder < 0 || folder >= kFolderCount) {
    LOG_ERR("SHELF", "Bad folder index: %d", folder);
    return false;
  }
  const Folder& parent = kFolders[folder];
  if (item < 0 || item >= parent.count) {
    LOG_ERR("SHELF", "Bad item index %d in %s", item, parent.title);
    return false;
  }

  // Recorded before the launch, not after: replaceActivity destroys the caller,
  // so there is no "after" to run in.
  openFolderIndex = folder;
  saveIfChanged(folder, item);
  setOpenTitle(parent.items[item].title);
  if (!replaceWith(parent.items[item].create(renderer, mappedInput), parent.items[item].title)) {
    openFolderIndex = -1;
    setOpenTitle(nullptr);
    return false;
  }
  return true;
}

const Item* homeItems() { return kHomeItems; }

int homeItemCount() { return kHomeItemCount; }

UIIcon homeItemIcon(const int index) { return kHomeItemIcons[index]; }

bool openHomeItem(const int index, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (index < 0 || index >= kHomeItemCount) {
    LOG_ERR("SHELF", "Bad home item index: %d", index);
    return false;
  }
  openFolderIndex = -1;
  lastHomeItem = index;
  saveIfChanged(-1, 0);
  setOpenTitle(kHomeItems[index].title);
  if (!replaceWith(kHomeItems[index].create(renderer, mappedInput), kHomeItems[index].title)) {
    setOpenTitle(nullptr);
    return false;
  }
  return true;
}

int lastHomeItemOnHome() { return lastHomeItem; }

bool leaveToFolder(const char* activityName, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (openFolderIndex < 0 || activityName == nullptr || openActivityName != activityName) return false;
  openFolder(openFolderIndex, renderer, mappedInput);
  return true;
}

void forgetOpenFolder() { openFolderIndex = -1; }

void autostartFromEnv(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  // Once per process: leaving the app afterwards must land on the shelf like
  // any other exit, not bounce straight back in.
  static bool consumed = false;
  if (consumed) {
    return;
  }
  const char* wanted = std::getenv("CROSSPLAY_AUTOSTART");
  if (wanted == nullptr || *wanted == '\0') {
    return;
  }
  consumed = true;
  int folder = -1;
  int item = -1;
  if (!findItemByTitle(wanted, folder, item)) {
    LOG_ERR("SHELF", "Autostart: no item titled '%s'", wanted);
    return;
  }
  if (folder < 0) {
    LOG_INF("SHELF", "Autostart into %s", kHomeItems[item].title);
    openHomeItem(item, renderer, mappedInput);
    return;
  }
  LOG_INF("SHELF", "Autostart into %s", kFolders[folder].items[item].title);
  openItem(folder, item, renderer, mappedInput);
}

void rememberForWake(const char* currentActivityName) {
  ensureLoaded();
  if (state.openTitle[0] == '\0') return;
  // An item is only still open if the activity it launched is the one on
  // screen. Leaving a game by the Home gesture never passes through leave(),
  // so without this a sleep taken on Home would resume into the game you left.
  const char* onScreen = currentActivityName == nullptr ? "" : currentActivityName;
  if (openActivityName.empty() || openActivityName != onScreen) {
    LOG_DBG("SHELF", "Sleeping on %s, not %s: nothing to resume", onScreen, state.openTitle);
    setOpenTitle(nullptr);
  }
}

bool resumeFromWake(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  ensureLoaded();
  if (state.openTitle[0] == '\0') return false;

  int folder = -1;
  int item = -1;
  if (!findItemByTitle(state.openTitle, folder, item)) {
    // The card outlives firmware updates, so the item may have been renamed or
    // removed since it was written. Home, and forget it.
    LOG_INF("SHELF", "Wake: nothing titled '%s' any more", state.openTitle);
    setOpenTitle(nullptr);
    return false;
  }

  if (folder < 0) {
    LOG_INF("SHELF", "Wake: resuming %s", kHomeItems[item].title);
    return openHomeItem(item, renderer, mappedInput);
  }
  LOG_INF("SHELF", "Wake: resuming %s", kFolders[folder].items[item].title);
  return openItem(folder, item, renderer, mappedInput);
}

void leave(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (openFolderIndex >= 0) {
    openFolder(openFolderIndex, renderer, mappedInput);
    return;
  }
  activityManager.goHome();
}

int lastFolderOnHome() {
  ensureLoaded();
  return state.lastFolder;
}

int resumeRowIn(const int index) {
  ensureLoaded();
  if (index < 0 || index >= kFolderCount) return 0;
  const int row = state.resumeRow[index];
  return row < kFolders[index].count ? row : kFolders[index].count - 1;
}

void rememberRowIn(const int index, const int row) {
  if (index < 0 || index >= kFolderCount || row < 0 || row >= kFolders[index].count) {
    LOG_ERR("SHELF", "Bad row %d in folder %d", row, index);
    return;
  }
  saveIfChanged(index, row);
}

}  // namespace shelf
