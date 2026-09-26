"""Words saved from the reader's dictionary, between the POST and the push.

The device sends each saved word as the file it keeps on the card
(src/util/WordCaptureCore.h: a header, a blank line, the raw entry). They are
kept here, one JSON line each, from the moment the POST is acknowledged -- the
device deletes its copies on that acknowledgement -- until a sync cycle has
added them to the mirror AND pushed that to AnkiWeb. A cycle that fails, or a
full download that replaces the mirror, leaves them here to be added again;
adding is idempotent (words_to_anki skips a word already in its deck).
"""

import json
import os
import pathlib
import re

import words_to_anki

# "<lang>/<stem>.txt", as the device names them under /study/.inbox.
FILE_RE = re.compile(r"^[a-z]{2,3}/[^/\\\x00-\x1f]{1,80}\.txt$")


class WordsInbox:
    def __init__(self, path: pathlib.Path):
        self.path = path
        self._taken = 0

    def add(self, entries):
        """entries: [(file, text)]. Durable before return."""
        if not entries:
            return
        with self.path.open("a", encoding="utf-8") as f:
            for file, text in entries:
                f.write(json.dumps({"file": file, "text": text}, ensure_ascii=False) + "\n")
            f.flush()
            os.fsync(f.fileno())

    def pending(self):
        """Saved words, parsed, in the order they arrived. Remembers how many
        it returned, so drop_taken() removes those and not a batch that a
        POST appended while the cycle ran."""
        if not self.path.exists():
            self._taken = 0
            return []
        out = []
        lines = self.path.read_text("utf-8").splitlines()
        self._taken = len(lines)
        for line in lines:
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            lang = rec["file"].split("/", 1)[0]
            out.append(words_to_anki.parse(rec["text"], lang, rec["file"]))
        return out

    def drop_taken(self):
        """Remove the words the last pending() returned, keeping later ones."""
        if not self.path.exists() or self._taken == 0:
            return
        rest = self.path.read_text("utf-8").splitlines()[self._taken :]
        if not rest:
            self.path.unlink(missing_ok=True)
        else:
            tmp = self.path.with_suffix(".tmp")
            tmp.write_text("".join(line + "\n" for line in rest), "utf-8")
            os.replace(tmp, self.path)
        self._taken = 0


def languages(files):
    return sorted({f.split("/", 1)[0] for f in files})
