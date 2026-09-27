# The shelf

How to add a game or an app to this fork, and the rules that keep the
navigation from rotting. Read [LOCAL_SCOPE.md](../LOCAL_SCOPE.md) first for why
the fork exists at all.

## The shape

```
Home
  Recent Books (two covers, two title lines each)
  Library                                        (upstream's, full width)
  ---------------------------------------------
  Study        (Home item)    Apps >  browse, file transfer, reading,
                                      hacker news, xkcd, get books,
                                      wallpapers, solitaire, battleship,
                                      player
  Settings     (upstream's)   Wi-Fi  (File Transfer's join-a-network)
```

Home items open straight from Home; everything else sits in the one folder.
**At most two taps to anything.**

Upstream's Browse and File Transfer are items in the folder too. They leave
through `goHome()`, not `shelf::leave()`, so `goHome()` asks
`shelf::leaveToFolder()` first and lands them back in the folder. The Home
gesture calls `shelf::forgetOpenFolder()` before `goHome()`, because that
gesture means Home from anywhere.

## The folder screen

`ShelfFolderActivity` is a `UiListActivity`, the base upstream's own list
screens use: the theme's header with the folder's title, one row per item with
its Lucide icon, and the theme's selection, scrolling and touch. Back from it is
the base's Back, handed to `shelf::leave()`. It reopens on the row it was left
on: `shelf::rememberRowIn()` as the selection moves, `shelf::resumeRowIn()` on
entry.

**Player** is an ordinary item: the name and face this device shows another one
in Battleship, which is the one setting a device has. See
[`player/PlayerName.h`](../src/apps_local/player/PlayerName.h).

## The three rules

**1. A folder holds items, never folders.** There is nowhere in `shelf::Folder`
to put a `Folder`. The depth cap is structural, not a convention someone has to
remember, and on a panel that repaints in half a second a third tap is a real
cost.

**2. Back has two rules.** An app returns to its folder, or to Home when Home
opened it; a folder returns to Home.

**3. No app names its own destination.** It calls `shelf::leave()`.

Rule 3 is the one with scar tissue: a breadcrumb that has to be redeemed by a
different activity on entry strands you in the wrong menu the one time it is
missed. Every app here is ours, so none of them names a destination. Upstream's
screens in the folder cannot call `leave()`; `goHome()` routes them through
`shelf::leaveToFolder()` instead.

The current folder is module state in `Shelf.cpp` rather than something each app
carries. `replaceActivity` means exactly one activity exists at a time, so there
is exactly one current location: one fact, stored once. Threading a parent
through every factory would make six apps each keep their own copy of something
the device only has one of, and any app that forgot to store it could not leave.

## Adding a game or an app

**1. Copy the template.**

```bash
cp -r src/apps_local/sample src/apps_local/study
```

`sample` is sixty-six lines and is not registered, so it builds but never shows
up on the device. Do not start from a real app: the smallest is solitaire at
nineteen hundred lines.

Keep the app-name prefix on every file (`StudyActivity.h/.cpp`), matching
upstream convention: grep and crash logs stay unambiguous.

**2. Split it three ways.** This is the part that pays off later:

| Layer         | Example             | Knows about                               |
| ------------- | ------------------- | ----------------------------------------- |
| Rules / state | `SolitaireCore.h`       | nothing -- freestanding C++17             |
| Screens       | `SolitaireScreens.h`    | FreeInkUI and Toybox tokens, nothing else |
| Activity      | `SolitaireActivity.cpp` | the renderer, storage, input, the shelf   |

The first two are host-testable on macOS with no device. The third is the only
part that needs hardware, and it should be thin. See
[building-apps.md](building-apps.md) for the method in full.

**3. Register it.** One row in `src/apps_local/Shelf.cpp`, in the folder:

```cpp
constexpr shelf::Item kApps[] = {
    {"xkcd", &icon_xkcd_32, &XkcdActivity::create},
};
```

or on Home, with Home's `UIIcon` beside it in `kHomeItemIcons`:

```cpp
constexpr shelf::Item kHomeItems[] = {
    {"Study", &icon_study_32, &StudyActivity::create},
};
```

That is the whole registration for a folder item. No `ActivityManager` method, no `UIIcon` enum
variant, no i18n key, no `AppId` bit.

**Icons come from Lucide, not from `UIIcon`.** Add a line to
`tools_local/toybox/icons.txt` and run `./tools_local/toybox/gen_toybox_icons.sh`:

```
xkcd = gallery-vertical-end
```

That regenerates `src/apps_local/ui/ToyboxIcons.h`, which is committed because
generating needs librsvg and a checkout should build without it. Upstream's
palette is thirteen icons and growing it costs two upstream files per app;
Lucide ships 1735 and costs a line in a manifest.

A row with no icon does not compile -- `Shelf.cpp` static_asserts it, because a
blank icon gutter is silent otherwise. Pick for silhouette rather than
literalness: the label already says the name, so the icon's job is to be
distinct at a glance in a 62px row.

**A folder holds up to `ShelfFolderActivity::MAX_ROWS` items**, the size of
the row array it builds once on entry; a longer registry logs and shows the
first ones.

**4. Leave through the shelf.**

```cpp
if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
  shelf::leave(renderer, mappedInput);
  return;
}
```

**5. Write its tests before you believe it.** `host-tests/<app>/run.sh`, plus a
block in `host-tests/ui/test_ui.cpp` for the screens. Then break the code on
purpose and check the tests notice: asserting "the name is not drawn" is not
the same as asserting "the control is not there".

## What is not host-tested, and why

`Shelf.cpp` is not host-tested: it exists to swap activities, so it pulls in
`ActivityManager` and cannot be built freestanding. Its job is four facts --
which folder is open, which folder Home should select, which row each folder
should reopen on, and which item was open when the device went to sleep -- and
those are verified in the simulator instead. The file format underneath them,
`ShelfState`, is freestanding and has `host-tests/shelfstate`.

The last three of those outlive a reboot, in `/.crosspoint/shelf.cfg` beside
`player.cfg`. They are plain `.bss` otherwise, and `main.cpp` deep-sleeps on the
idle timeout with wake being effectively a chip reset. Verified by driving it
twice: one run opens the third item and leaves, and a second run from a cold
boot must land Home on Apps and the folder's cursor on that same item. If you
touch that bookkeeping, drive it:

```bash
# Opened an app and came back: the row you opened.
./scripts_local/sim-shot.sh '4000:HOME;8000:TAP:360,620;13000:TAP:150,258;18000:BACK;22000:QUIT' \
                            '21000:qa-artifacts/returned.bmp'
```

## Wake comes back into the app, not to Home

Deep sleep is a chip reset, so before it there is one write and after it one
read. `shelf::rememberForWake()` runs on the way into sleep and records the open
item's **title** on a second line of `shelf.cfg`; `shelf::resumeFromWake()` runs
from `setup()`'s routing block and reopens it, ahead of the reader's own resume.
Without it every sleep taken in a game woke up on Home, which is what made the
"Quick Resume" sleep screen a lie outside the reader: it leaves your game on the
panel with a small moon in the corner, and the wake then threw that game away.

Three things that look optional and are not:

- **The title, not the row index.** The card outlives firmware updates and row
  indices move whenever a game is added. An index would resume into a different
  game, silently. `findItemByTitle()` is the one place a title becomes a row,
  and a title that no longer exists falls back to Home.
- **The activity name, not `openFolderIndex`.** An item counts as open only when
  the activity it launched is the one on screen. The Home gesture leaves an app
  without passing through `leave()`, so `openFolderIndex` alone would claim a
  game you left ten minutes ago is still open and wake into it.
- **Only on a verified sleep wake.** The gate is `BootResume::SplashlessWake`,
  which needs both the one-shot `showBootScreen` flag the sleep wrote and a
  power-button wakeup reason. A cold boot must not resume off a stale card.

The simulator models the whole cycle -- a button press while asleep re-execs the
process reporting a power wake -- so drive it end to end rather than in halves:

```bash
CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE='7000:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE='4000:qa-artifacts/after-wake.bmp' \
./scripts_local/sim-shot.sh '4000:HOME;8000:TAP:360,620;13000:TAP:150,567;17000:SLEEP;20000:POWER' ''
```

The trace must show the game entered again on the second process, with no Boot
and no Home between. Do not settle for checking the two halves separately: the
gate is the _combination_ of the flag and the wakeup reason, and setting each by
hand proves neither.

What this does **not** do is restore a half-played board. Reopening the app is
the shelf's job; remembering the position is the app's, through the same save
its `onExit` already writes. Solitaire comes back mid-game.

## Two icon paths, opposite conventions

Worth knowing before you add an icon anywhere new, because it costs an
afternoon otherwise.

- `fui::bitmapFromIcon` + `DrawTarget::bitmap`, which everything in
  `apps_local` uses, takes **upright** bitmaps. The renderer maps logical
  coordinates onto the panel.
- `GfxRenderer::drawIcon`, which upstream's `drawButtonMenu` uses, rotates its
  input by -90. Every bitmap in `src/components/icons/` is therefore stored
  rotated the other way so it comes out upright.

So the Home icons the shelf adds (Apps, Study) exist twice: upright in
`ui/ToyboxIcons.h`, which is what `rotate_icons.py` reads, and pre-rotated in
`src/components/icons/shelfIcons.h`, which is what the theme draws.
`tools_local/toybox/gen_toybox_icons.sh` writes both; never hand-edit either.
The upright pair is the rotation's source rather than something a screen draws.

The joystick shipped lying on its side, then upside down, before this was
understood. The direction was settled by rotating a freshly generated `folder`
and comparing it against upstream's own stored `FolderIcon` -- not by
byte-equality, since the Lucide source has moved since theirs was generated, but
by looking at the shape.

## Adding a second folder, or another Home item

A folder is one row in `kFolders`; a Home item is one row in `kHomeItems` and
one in `kHomeItemIcons`. Either needs its icon in two places: a `UIIcon` value
appended in `BaseTheme.h` with a case in `LyraTheme.cpp` (Home draws it and
accepts nothing else), and a line in `tools_local/toybox/icons.txt`, from which
`gen_toybox_icons.sh` writes the pre-rotated copy into
`src/components/icons/shelfIcons.h` for Home's row. Add the alias to the
script's rotate list too.

Resist a second folder until there is something that genuinely belongs outside
Apps. One folder is a structure; four is a filing system, and a filing
system is what you build when you have not decided what the device is for.

## The Home seam

`src/activities/home/HomeActivity.cpp` draws its own menu rather than
upstream's list: `menuRows()` returns Library, the Home items, the folders,
Settings and Wi-Fi, in that order, and the render, the dispatch and the
selection restore all walk that one list. Recent-book covers sit before it,
which is why `menuRowOf()` adds `recentBooks.size()` to a row's position.

`layoutMenu()` places them: the first entry (and RoundedRaff's Continue
Reading, when the theme puts it in the menu) across the full width, the rest two
to a row, rows spaced evenly from right under the cover tile to the bottom
edge, a rule under the full-width rows. Each cell is drawn by the theme's own
`drawButtonMenu()` with one row, so every theme keeps its tile and selection
style; the touch test walks the same cell rectangles.

`onEnter()` restores the selection in this order: the departing upstream screen
that `goHome()` names (Library; Settings; File Transfer selects Wi-Fi; Browse
and Get Books select Apps), then `shelf::lastFolderOnHome()`, then
`shelf::lastHomeItemOnHome()`. `goHome()` matches the departing activity's
_name_ against `HomeMenuItem`, which cannot know shelf rows exist; without the
shelf's answers you leave a folder and the cursor sits on the first cover.

Wi-Fi opens `CrossPointWebServerActivity` with `joinNetworkOnly`, which skips
File Transfer's mode choice and goes straight to joining a network; leaving the
network list leaves the screen.

`src/activities/ActivityManager.cpp` carries the other two seams:
`goHome()` asks `shelf::leaveToFolder()` before building Home, and the Home
gesture calls `shelf::forgetOpenFolder()` first.

## Titles

The registry stores Title Case titles (`"Apps"`, `"Study"`, `"Get Books"`).
Home and the folder screen both draw them as they are, because both are the
theme's lists and have to look like the rest of it.
