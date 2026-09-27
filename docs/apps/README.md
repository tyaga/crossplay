# What is in docs/apps/

A doc lands here when something about an app would otherwise be rediscovered
the hard way; not every app has one, and some have several.

**Read the status first.** Several of these are plans, and a plan describing
something that shipped reads like unfinished work if nothing says otherwise.
Where a file is a record rather than a description, it says so in its own first
paragraph.

## What an app is, and how to use it

The fork's only user-facing app docs. `USER_GUIDE.md` at the repository root is
upstream's and covers the reader, not these.

| | |
| --- | --- |
| [`study.md`](study.md) | Anki decks on the reader: the deck, the scheduler, and what a review does. |
| [`fridge.md`](fridge.md) | Live: a note, a drawing or a photo left on a sleeping device from a phone. |

## Formats on the card

The on-disk shapes. Read these before changing a writer.

| | |
| --- | --- |
| [`study-deck-format.md`](study-deck-format.md) | |
| [`study-anki-compatibility.md`](study-anki-compatibility.md) | What converts, what is reduced, what stays behind. |
| [`xkcd-pack-format.md`](xkcd-pack-format.md) | |

## Decision records for things that shipped

Written before the code, kept for the reasoning. **All of these landed**, and
all are in the present tense of the day they were drafted, so read "not built
yet" here as "not built yet then".

| | |
| --- | --- |
| [`hackernews-saved-port.md`](hackernews-saved-port.md) | The saved-articles shelf. |
| [`study-installer-plan.md`](study-installer-plan.md) | The browser page that converts a deck. |
| [`study-sync-bridge-plan.md`](study-sync-bridge-plan.md) | Every device syncs, nobody runs a server. |
| [`study-syncflow-ui.md`](study-syncflow-ui.md) | The sync flow rework. |
| [`wallpapers-phone-flow.md`](wallpapers-phone-flow.md) | From a picture on a phone to the sleep screen. |
| [`wallpapers-shuffle.md`](wallpapers-shuffle.md) | Choosing a set and letting it take turns. |
| [`xkcd-viewing-plan.md`](xkcd-viewing-plan.md) | Reworking how comics are shown. |

## Generated, not written

[`study-quick-reference.pdf`](study-quick-reference.pdf) is the one file here
that is not prose: a printable page for the Study app, produced rather than
edited.

## Things on the shelf with no doc of their own

Solitaire, Battleship, Hacker News, xkcd, Wallpapers and Get Books. Some have
auxiliary records here (a format, a plan, a flow); none has a file saying what
the app is. To check the list, read `Shelf.cpp`'s `kAppsAndGames` and
`kHomeItems` against "What an app is" above.
