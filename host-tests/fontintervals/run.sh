#!/bin/bash
# Does every prose face on the SD card carry the combining marks it needs?
#
# A .cpfont ships exactly the codepoints its interval presets name, and a glyph
# the face lacks draws as U+FFFD. The built-in faces carry Combining
# Diacritical Marks (fontconvert.py, 0x0300-0x036F); the SD presets did not, so
# selecting any SD family turned every stressed vowel in a bilingual dictionary
# entry into a diamond -- "аппара́т" as "аппара◆т". Visible in the reader too,
# wherever text is decomposed rather than precomposed.
#
# The invariant, rather than a list of families: a face that draws Latin
# Extended-A or Cyrillic draws text that can carry a combining mark, so it must
# carry the block. Faces built for a single non-Latin script (Arabic UI faces
# take ascii + latin1 only) are outside it and are not checked.
#
#   host-tests/fontintervals/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

PY=""
for candidate in "$ROOT/../local/venv-fonts/bin/python" "$ROOT/.venv-study/bin/python" python3; do
  if "$candidate" -c 'import yaml' >/dev/null 2>&1; then
    PY="$candidate"
    break
  fi
done
if [ -z "$PY" ]; then
  if [ -n "${CI:-}" ]; then
    echo "FAIL fontintervals  no interpreter with pyyaml and this is CI: the suite must run here"
    exit 1
  fi
  echo "SKIP fontintervals  no interpreter with pyyaml; pip install -r lib/EpdFont/scripts/requirements.txt (in CI it is a failure)"
  exit 0
fi

"$PY" - "$ROOT" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
sys.path.insert(0, str(root / "lib/EpdFont/scripts"))

import yaml  # noqa: E402

import fontconvert_sdcard  # noqa: E402

COMBINING = (0x0300, 0x036F)
# Codepoints that decide whether a family is in scope, and the mark itself.
LATIN_EXT_A = 0x0100
CYRILLIC_A = 0x0410
ACUTE = 0x0301

checks = 0
failed = 0


def covers(intervals, cp):
    return any(start <= cp <= end for start, end in intervals)


config = yaml.safe_load((root / "lib/EpdFont/scripts/sd-fonts.yaml").read_text())

for family in config["families"]:
    name = family["name"]
    intervals = fontconvert_sdcard.resolve_intervals(family["intervals"])
    if not (covers(intervals, LATIN_EXT_A) or covers(intervals, CYRILLIC_A)):
        continue
    checks += 1
    missing = [cp for cp in range(COMBINING[0], COMBINING[1] + 1) if not covers(intervals, cp)]
    if missing:
        failed += 1
        print(f"  FAIL {name} draws Latin or Cyrillic prose but not U+0301")
        print(f"       intervals: {family['intervals']}")
        print(f"       {len(missing)} of the Combining Diacritical Marks block is absent,")
        print("       so a stressed vowel in a dictionary entry draws U+FFFD")

# The presets are the fix's actual home; a family list that happens to pass
# while the preset regressed would mean the next family added is broken again.
for preset in ("latin-ext", "reading"):
    checks += 1
    intervals = fontconvert_sdcard.resolve_intervals(preset)
    if not covers(intervals, ACUTE):
        failed += 1
        print(f"  FAIL preset '{preset}' no longer covers U+0301")

print(f"{checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
PY
