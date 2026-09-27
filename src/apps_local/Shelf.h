#pragma once

// The shelf: everything this fork adds to the device, and the only way in or
// out of it.
//
// Home gets the Home items, launched directly, and one folder, APPS, holding
// everything else -- upstream's file browser and file transfer included. That
// is the whole hierarchy.
//
//   Home
//     Library                                          (upstream's)
//     Study         Apps >  browse, file transfer, reading, ..., player
//     Settings      Wi-Fi                               (upstream's)
//
// ---------------------------------------------------------------------------
// Three rules, and the reason each exists.
//
// 1. A folder holds apps, never another folder. Depth is capped at two by the
//    type, not by discipline: there is nowhere in `Folder` to put a folder.
//    Two taps reaches anything, forever, and on a panel that repaints in half a
//    second a third tap is a real cost.
//
// 2. Games and apps are the same kind of row. The only difference between
//    solitaire and a spaced-repetition deck is where it sits, so there is one
//    `Item` type and one launcher.
//
// 3. An app never knows where it came from. It calls leave() and lands in its
//    folder, or on Home when Home opened it; a folder calls leave() and lands
//    on Home. Upstream's screens in a folder cannot call leave(): they call
//    goHome(), and goHome() asks leaveToFolder() first.
// ---------------------------------------------------------------------------

#include <Icon.h>

#include <memory>

#include "../components/themes/BaseTheme.h"  // UIIcon

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace shelf {

// One openable thing. Raw, untranslated title: routing these through tr() would
// mean editing lib/I18n/translations/*.yaml per app, which is the per-app
// upstream churn this whole directory exists to avoid.
struct Item {
  const char* title;
  // A real icon, not a UIIcon. Upstream's palette has thirteen entries and none
  // of them is a crown or a ship; adding variants would mean editing
  // BaseTheme.h and LyraTheme.cpp per app, which is the churn this directory
  // exists to avoid. These are generated from Lucide SVGs into
  // ui/ToyboxIcons.h -- an asset the shelf resolves, so the palette is the
  // 1735 icons Lucide ships rather than a growing enum.
  const freeink::Icon* icon;
  std::unique_ptr<Activity> (*create)(GfxRenderer&, MappedInputManager&);
};

// A titled list of items. Note what is absent: a Folder cannot contain a
// Folder. That is rule 1, expressed as a type rather than a convention.
struct Folder {
  const char* title;
  // Home draws this one, and upstream's menu takes a UIIcon and nothing else.
  UIIcon icon;
  const Item* items;
  int count;
};

const Folder* folders();
int folderCount();

// Open folder `index` from Home. Out of range is a no-op and logged.
void openFolder(int index, GfxRenderer& renderer, MappedInputManager& mappedInput);

// The items Home launches directly, drawn after upstream's rows and before the
// folders.
const Item* homeItems();
int homeItemCount();
UIIcon homeItemIcon(int index);

// Open Home item `index`. leave() from it lands on Home. False if the item
// could not be created.
bool openHomeItem(int index, GfxRenderer& renderer, MappedInputManager& mappedInput);

// Which Home item Home should select on entry, or -1 when the last thing opened
// from Home was not one.
int lastHomeItemOnHome();

// Open the folder the activity named `activityName` was launched from, and say
// whether it did. False when that activity was not opened from a folder, so
// the caller goes Home. This is how upstream's own screens, which leave by
// goHome(), return to Apps.
bool leaveToFolder(const char* activityName, GfxRenderer& renderer, MappedInputManager& mappedInput);

// Drop the folder an open item came from, so the next goHome() lands on Home.
// The Home gesture calls it: that gesture means Home, from anywhere.
void forgetOpenFolder();

// Open item `item` of folder `folder`, and remember which folder it came from
// so leave() can undo it. False if the item could not be created, in which case
// nothing was replaced and the caller still owns the screen.
bool openItem(int folder, int item, GfxRenderer& renderer, MappedInputManager& mappedInput);

// Record, on the way into deep sleep, whether a shelf item is what the user is
// looking at. `currentActivityName` is the name of the activity on screen; an
// item counts as open only when that is the activity the item launched, so
// leaving a game by the Home gesture -- which never passes through leave() --
// does not leave a stale claim behind.
//
// Wake from deep sleep is a chip reset, so nothing survives it that was not
// written to the card first. This is the write.
void rememberForWake(const char* currentActivityName);

// Reopen whatever rememberForWake() recorded, and say whether it did. False
// when the device was not in a shelf item when it slept, and when the item has
// been renamed or removed by a firmware update since -- the caller goes Home,
// which is where every wake used to land.
bool resumeFromWake(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Open the item named by the CROSSPLAY_AUTOSTART environment variable, if it
// is set and matches an item title (case-insensitive). A no-op everywhere the
// variable does not exist, which is every real device: this is how the site's
// installer page boots its emulator straight into Study with the user's own
// deck, and how `CROSSPLAY_AUTOSTART=solitaire ./bin/sim` skips the shelf during
// development. Same getenv-in-firmware precedent as CROSSPLAY_SEED.
void autostartFromEnv(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Go back one level: an app returns to its folder, a folder returns to Home.
//
// The current folder is module state rather than something each app carries,
// and that is deliberate. `replaceActivity` means exactly one activity exists
// at a time, so there is exactly one current location -- one fact, stored once.
// Threading a parent through every factory would make six apps each keep their
// own copy of a thing the device only has one of, and any app that forgot to
// store it could not leave.
void leave(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Which shelf row Home should select on entry, or -1 if the shelf was not the
// last thing open. Home restores its own selection by matching the departing
// activity's name against its HomeMenuItem list, which has no idea our rows
// exist; this is how leaving a folder puts the cursor back on its row.
int lastFolderOnHome();

// The row folder `index` should reopen on. 0 if it has never been left
// anywhere; a folder that shrank since resumes on its last row.
//
// A folder is destroyed when it launches something and again when you walk out
// of it, so without this you come back from the third game with the cursor on
// the first, and you come back from a book to page one of a folder you had
// paged to the end of. Home has the same problem and the same answer; see
// lastFolderOnHome().
int resumeRowIn(int index);

// Remember that folder `index` is standing on `row`, so that is where it comes
// back. openItem() does this with the item it opened; the folder itself does it
// as its selection moves, which is what makes leaving a folder WITHOUT opening
// anything come back to where you were.
//
// Written as the selection moves rather than on the way out, because there is no
// reliable way out to hook: the idle timeout deep-sleeps wherever you are, and
// wake is a chip reset. The write is ~20 bytes beside a full e-ink repaint.
void rememberRowIn(int index, int row);

}  // namespace shelf
