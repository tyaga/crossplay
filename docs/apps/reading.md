# READING

Reading statistics: time read per day and per book, pages turned, a streak
against a daily goal, and an estimate of the time left in the chapter or the
book, shown in the reader's status bar.

The app in `src/apps_local/readingstats/` only shows the numbers. They are
recorded by three hooks in upstream code, whether or not the app is ever opened.

## How time is counted

Every reader paints its position through `BaseTheme::drawStatusBar`, so that is
where a page view is recorded (`readstats::onReaderStatusBar`). A repaint of the
same page is not a new view.

- When the page changes, the view that just ended is credited with the time it
  was on screen, up to the **idle limit** (5 minutes by default). A device left
  open on a page adds the limit, not the hours it sat there.
- A **page** is a single forward turn after at least 2 seconds on the page. A
  TOC jump, a ten-page skip or a step back adds time but no page.
- Leaving the reader for a menu, the dictionary or Home keeps the session open
  for one idle limit. Coming back to the same book continues it, and the time
  away counts as reading. Otherwise the last page is credited up to the moment
  the reader was left.
- An automatic sleep credits the last page with an average page's time rather
  than with the whole wait before sleep. A sleep from the power button credits
  it up to the press.
- A **session** is a run of views with no gap over the idle limit, and it counts
  once it reaches a minute.

Days are local calendar days from the RTC. Time read while the clock is unset
still counts towards the book, but towards no day.

## Time left

The pace is the average time of views that ended in a single forward turn and
stayed under the idle limit.

- **Chapter**: pages left in the chapter times the time per page. It needs five
  such turns in this book, or twenty across all books.
- **Book**: percent left times the time per percent. It needs a full percent read
  in this book.

For TXT and XTC books the "chapter" is the whole book.

## Settings

Daily goal (off to 120 minutes), time left (off, chapter, book), and idle limit
(2 to 15 minutes). They are stored in the stats file.

## The file

`/.crosspoint/reading-stats.bin`, outside the per-book cache folders, so
clearing the reading cache keeps it. It is rewritten through a `.tmp` and a
rename:

- while a book is open, at most every two minutes;
- as soon as the reader is left;
- before an attended sleep.

A file that does not parse is moved to `reading-stats.bin.bad` rather than
overwritten.

A book is matched by path, then by title and author. A book moved to another
folder, for example by "move finished books", keeps its history.

Layout, little-endian:

| Field | Type |
| --- | --- |
| magic `RSTS`, version `1` | 4 + 1 bytes |
| goal minutes, idle minutes, time-left mode | u16, u8, u8 |
| recency sequence | u32 |
| day count, then per day: day number (days since 1970-01-01), ms, pages | u32; i32, u32, u32 |
| book count, then per book: path, title, author (u16 length + UTF-8) | u32; strings |
| ms, pages, sessions, first day, last day, recency, percent | u64, u32, u32, i32, i32, u32, f32 |
| pace ms, pace turns, pace percent | u32, u32, f32 |

The logic is freestanding (`StatsCore`) and tested in `host-tests/readingstats/`.
