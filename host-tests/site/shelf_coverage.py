"""Every game and app on the shelf is named on the site and in the README.

The shelf is the product and the page is how anyone finds out what is on it.
An app missing from the page is not visible in a build, in a render or in a
read-through, because what is absent looks like nothing at all.

The list is read out of Shelf.cpp, which is what actually decides what a device
shows, so a new app fails this until somebody writes it up. Prints one line per
gap and nothing when there are none; host-tests/site/run.sh counts the lines.

    python3 host-tests/site/shelf_coverage.py <repo-root>
"""

import pathlib
import re
import sys

if len(sys.argv) < 2:
    sys.exit("usage: shelf_coverage.py <repo-root>")

root = pathlib.Path(sys.argv[1])
shelf = (root / "src/apps_local/Shelf.cpp").read_text()


def titles(table):
    body = re.search(
        r"constexpr shelf::Item " + table + r"\[\] = \{(.*?)\n\};", shelf, re.S
    )
    if not body:
        print(
            f"Shelf.cpp has no {table} table, so this check cannot see the shelf at all"
        )
        return []
    return re.findall(r'\{"([^"]+)"', body.group(1))


def flat(text):
    """Casing, punctuation and &amp; are the page's business, not the shelf's."""
    text = text.replace("&amp;", "&").replace("&#38;", "&")
    return " " + re.sub(r"[^a-z0-9]+", " ", text.lower()) + " "


pages = {
    "the site": flat((root / "site/index.html").read_text()),
    "the README": flat((root / "README.md").read_text()),
}

for table, kind in (("kHomeItems", "Home item"), ("kApps", "shelf item")):
    for title in titles(table):
        needle = " " + flat(title).strip() + " "
        for where, text in pages.items():
            if needle not in text:
                print(
                    f"the {kind} {title} is on the shelf and named nowhere in {where}"
                )


# ---------------------------------------------------------------------------
# And the PLAY NEARBY list, which is the same failure one level down: a game
# that gained a radio and was never added to the sentence. The truth is
# LinkPlay.h's GameId enum -- an id is what a device actually offers to play --
# so the sentence is checked against it.
#
# The release page is not checked here: docs/release-body.md names no game and
# states no number, and the check further down asserts that it stays that way.
# ---------------------------------------------------------------------------

link = (root / "src/apps_local/link/LinkPlay.h").read_text()
body = re.search(r"enum class GameId[^{]*\{(.*?)\n\};", link, re.S)
if not body:
    print("LinkPlay.h has no GameId enum, so the PLAY NEARBY list cannot be checked")
    ids = []
else:
    ids = [n for n in re.findall(r"^\s*([A-Za-z]+)\s*=", body.group(1), re.M) if n != "Test"]

# ConnectFour -> "Connect Four". flat() then makes the spacing and casing moot.
spaced = [re.sub(r"(?<!^)(?=[A-Z])", " ", name) for name in ids]

prose = {
    "the README": (root / "README.md").read_text(),
}
for where, text in prose.items():
    near = " ".join(par for par in re.split(r"\n\s*\n", text) if "PLAY NEARBY" in par)
    if not near:
        print(f"{where} does not mention PLAY NEARBY at all")
        continue
    near = flat(near)
    for name in spaced:
        if " " + flat(name).strip() + " " not in near:
            print(f"{name} plays over PLAY NEARBY and {where} leaves it out of the list")


# ---------------------------------------------------------------------------
# Number words, for spotting a count written into standing text.
# ---------------------------------------------------------------------------

WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
    "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16,
    "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20,
}


# The release page must state no total of its own. It is rewritten by
# scripts_local/release_notes.py on every release, from the merged pull
# requests, and that generator knows nothing about Shelf.cpp -- so a number
# written into its standing text is a fact about one file maintained by hand in
# another, on the one page a stranger reads first. The README is where the
# enumerated list lives, and it is checked against the source above.
body_preamble = (root / "docs/release-body.md").read_text().split("### ", 1)[0]
counted = re.search(
    r"\b(\d+|" + "|".join(WORDS) + r")\s+(?:of them\b|of the games\b|games\b|apps\b)",
    body_preamble,
    re.I,
)
if counted:
    print(
        f"docs/release-body.md counts the shelf in its standing text "
        f"({counted.group(0)!r}); that is a fact about Shelf.cpp on a page "
        f"nothing regenerates from it, and it went stale there once already"
    )


