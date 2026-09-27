# Third-party notices

CrossPlay is MIT (see [LICENSE](LICENSE)). It also ships other people's work,
some of it compiled into the firmware where a licence file cannot travel beside
it. This is that list.

Where a licence is reproduced in full elsewhere in the tree, this file points at
it rather than copying it twice.

## The firmware this forks

**CrossPoint Reader**, MIT, Copyright (c) Dave Allie and the CrossPoint
contributors. Most of the code in this repository is theirs; the reader, the
EPUB engine, sync, the file browser and the network stack in particular. Full
text in [LICENSE](LICENSE), upstream at
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader).

**FreeInk SDK**, vendored at `freeink-sdk/`. Its own licence travels with it.

## Compiled into the firmware image

**Lucide icons**, ISC, Copyright (c) Lucide Icons and Contributors. Full text at
`freeink-sdk/libs/assets/Icons/lucide/LICENSE`. ISC requires the copyright
notice and permission notice in all copies, and the icons are rasterised into
bitmaps at build time, so this entry is how that notice reaches a flashed
device.

**PapyriX Reader**, MIT, Copyright (c) 2026 Pavel Liashkov. The Windows-1251,
KOI8-R and Windows-1252 tables and the expat encoding handler in
`lib/Fb2/Fb2Encoding.h`, which let the FB2 converter read books in those
encodings, come from it. Upstream at
[bigbag/papyrix-reader](https://github.com/bigbag/papyrix-reader). The MIT
licence:

> Copyright (c) 2026 Pavel Liashkov
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

**Noto Sans Symbols 2**, SIL Open Font License 1.1. The Solitaire suit glyphs
are drawn from it. Full text at `src/apps_local/solitaire/art/OFL.txt`, with the
derivation documented in `src/apps_local/solitaire/art/README.md`.

**Libraries linked into the build**, each under its own licence as published:

| Library               | Author          |
| --------------------- | --------------- |
| ArduinoJson 7.4.2     | Benoit Blanchon |
| QRCode 0.0.1          | Richard Moore   |
| PNGdec 1.1.6          | Larry Bank      |
| JPEGDEC               | Larry Bank      |
| WebSockets 2.7.3      | Markus Sattler  |
| Arduino-wolfSSL 5.7.2 | wolfSSL Inc.    |

## Content on the SD card and in the browser build

**xkcd**, by Randall Munroe, [CC BY-NC 2.5](https://xkcd.com/license.html). The
device fetches comics from xkcd.com, and the browser demo at
crossplay.ma-r-s.com carries a forty-comic slice packed from there. The licence
permits non-commercial redistribution with attribution; nothing here is sold.

**Hacker News** content belongs to its posters. The browser demo freezes a
snapshot of the front page, one thread's comments and two articles so the app
can be tried offline; the device itself fetches live from the public
[Algolia API](https://hn.algolia.com/api).

**Alice's Adventures in Wonderland** by Lewis Carroll, on the browser demo's SD
card, from [Project Gutenberg](https://www.gutenberg.org/ebooks/11). Public
domain.

**KaiTi**, on the browser demo's SD card only, rasterised for the Study app's
Chinese deck. This one is not cleanly licensed for redistribution and is
knowingly outstanding; see the open item in
[docs/open-items.md](docs/open-items.md).

## Type on the website and the bridge pages

**Jersey 25** and **Instrument Serif**, both SIL Open Font License 1.1. Full
texts ship beside the fonts at `site/assets/fonts/OFL-Jersey25.txt` and
`site/assets/fonts/OFL-InstrumentSerif.txt`; the Study bridge's pages carry
Instrument Serif with `server/study-bridge/bridge/static/OFL-InstrumentSerif.txt`.

## Games

Where a game on the shelf carries the name of a published game, that name is
its owner's trademark. CrossPlay implements the games; it is not affiliated
with, endorsed by or sponsored by any of their owners. Game mechanics are not
copyrightable; the names are used to say what the thing is.

Named rather than enumerated on purpose: a list of trademarks goes stale
silently, and being incomplete is the one way this notice can do harm.

## Something missing

If your work is here and this notice is wrong, or your work is here and this
notice does not mention it, open an issue and I will fix it in the next release.
