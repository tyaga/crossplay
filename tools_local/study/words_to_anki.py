#!/usr/bin/env python3
"""Turn the words saved from the reader's dictionary into Anki notes.

On the reader, Confirm (or a tap on the headword) on a dictionary entry saves
the word to the card as /study/.inbox/<lang>/<word>.txt: a short header and
the raw entry (src/util/WordCaptureCore.h). This script reads those files,
builds a card-sized note from each with dict_card.py, and adds it to a deck
per language -- "Dictionary::Dutch", "Dictionary::English" -- so the words
arrive in Anki, and from there on AnkiWeb and back on the reader, split the
way you study them.

    words_to_anki.py COLLECTION --card /Volumes/NO\\ NAME
    words_to_anki.py COLLECTION --device http://192.168.1.11
    words_to_anki.py COLLECTION --card fs_ --dry-run

A word already in its deck is skipped, so a card saved twice, or imported
from two copies of the inbox, is one note. Imported files are removed from
the card, so the next run starts empty; --keep leaves them.

Writes the collection directly, which needs Anki closed: two writers is how a
collection gets corrupted. study.py words checks that before calling this.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
import subprocess
import sys
import tempfile
import urllib.parse

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import dict_card  # noqa: E402

NOTETYPE = "CrossPlay Word"
# Names the Study converter maps without being told (anki_to_deck.py
# FIELD_NAME_PATTERNS): Word -> headword, Part of speech -> partOfSpeech,
# Meaning -> meaning, Sentence -> sentence. Transcription and Source are for
# Anki alone, and Transcription is deliberately a name the converter does not
# recognise: the reader draws a card's reading in its built-in face, which has
# no IPA, so "/dəˈvaɪs/" arrived as "/d??va?s/". Order is the editor's order.
FIELDS = ("Word", "Transcription", "Part of speech", "Meaning", "Sentence", "Source")
DEFAULT_PREFIX = "Dictionary"
INBOX = "study/.inbox"

LANGUAGES = {
    "cs": "Czech",
    "da": "Danish",
    "de": "German",
    "el": "Greek",
    "en": "English",
    "es": "Spanish",
    "fi": "Finnish",
    "fr": "French",
    "is": "Icelandic",
    "it": "Italian",
    "ja": "Japanese",
    "ko": "Korean",
    "nb": "Norwegian",
    "nl": "Dutch",
    "no": "Norwegian",
    "pl": "Polish",
    "pt": "Portuguese",
    "ru": "Russian",
    "sv": "Swedish",
    "tr": "Turkish",
    "uk": "Ukrainian",
    "zh": "Chinese",
}

FRONT = """<div class="word">{{Word}}</div>"""
BACK = """{{FrontSide}}
<hr id="answer">
<div class="meta">{{Transcription}}{{#Part of speech}} &middot; {{Part of speech}}{{/Part of speech}}</div>
<div class="meaning">{{Meaning}}</div>
{{#Sentence}}<div class="sentence">{{Sentence}}</div>{{/Sentence}}
{{#Source}}<div class="source">{{Source}}</div>{{/Source}}"""
CSS = """.card { font-family: serif; font-size: 22px; text-align: center; }
.word { font-size: 34px; }
.meta { color: #777; font-size: 18px; }
.meaning { margin-top: 10px; }
.sentence { margin-top: 14px; font-style: italic; font-size: 18px; }
.source { margin-top: 6px; color: #999; font-size: 14px; }"""


@dataclasses.dataclass
class Saved:
    """One word from the inbox, and where it came from."""

    language: str
    header: dict
    entry: str
    origin: str  # a path on disk, or a path on the device

    @property
    def word(self):
        return self.header.get("word", "").strip()


def deck_name(prefix, language):
    return f"{prefix}::{LANGUAGES.get(language, language.upper())}"


def parse(text, language, origin):
    """A saved-word file: 'key: value' lines, a blank line, the entry."""
    head, _, entry = text.partition("\n\n")
    header = {}
    for line in head.splitlines():
        key, sep, value = line.partition(": ")
        if sep:
            header[key.strip()] = value.strip()
    return Saved(header.get("language") or language, header, entry, origin)


def note_fields(saved):
    card = dict_card.card_fields(saved.entry, saved.header.get("format") == "html", saved.word)
    source = saved.header.get("source", "")
    return {
        "Word": saved.word,
        "Transcription": card["pronunciation"],
        "Part of speech": card["pos"],
        "Meaning": card["meaning"],
        "Sentence": saved.header.get("context", ""),
        "Source": source,
    }


# --- reading the inbox --------------------------------------------------------


def read_card(card):
    """Saved words under a mounted card or any directory holding study/."""
    root = pathlib.Path(card) / INBOX
    found = []
    if not root.is_dir():
        return found
    for lang_dir in sorted(p for p in root.iterdir() if p.is_dir() and not p.name.startswith(".")):
        for path in sorted(lang_dir.glob("*.txt")):
            found.append(parse(path.read_text("utf-8", "replace"), lang_dir.name, str(path)))
    return found


def _curl(*args, timeout="60"):
    return subprocess.run(["curl", "-s", "-m", timeout, *args], capture_output=True, text=True).stdout


def _device_ls(base, path):
    out = _curl(f"{base}/api/files?" + urllib.parse.urlencode({"path": path}))
    if not out.strip().startswith("["):
        return None
    return json.loads(out)


def read_device(base):
    """Saved words fetched from a reader over Wi-Fi, through its file API."""
    base = base.rstrip("/")
    top = _device_ls(base, "/" + INBOX)
    if top is None:
        if not _curl(f"{base}/api/status", timeout="8").strip().startswith("{"):
            sys.exit(f"The reader at {base} does not answer. Is File Transfer on?")
        return []
    found = []
    for lang in sorted(e["name"] for e in top if e["isDirectory"] and not e["name"].startswith(".")):
        for e in _device_ls(base, f"/{INBOX}/{lang}") or []:
            if e["isDirectory"] or not e["name"].endswith(".txt"):
                continue
            remote = f"/{INBOX}/{lang}/{e['name']}"
            with tempfile.NamedTemporaryFile(suffix=".txt") as tmp:
                _curl("-o", tmp.name, f"{base}/download?" + urllib.parse.urlencode({"path": remote}))
                text = pathlib.Path(tmp.name).read_text("utf-8", "replace")
            found.append(parse(text, lang, remote))
    return found


def remove_imported(saved, device=None):
    for s in saved:
        if device:
            _curl("-X", "POST", "--data-urlencode", f"path={s.origin}", f"{device.rstrip('/')}/delete")
        else:
            pathlib.Path(s.origin).unlink(missing_ok=True)


# --- writing Anki --------------------------------------------------------------


def ensure_notetype(col):
    models = col.models
    model = models.by_name(NOTETYPE)
    if model:
        have = {f["name"] for f in model["flds"]}
        missing = [f for f in FIELDS if f not in have]
        for name in missing:
            models.add_field(model, models.new_field(name))
        if missing:
            models.update_dict(model)
        return models.by_name(NOTETYPE)
    model = models.new(NOTETYPE)
    for name in FIELDS:
        models.add_field(model, models.new_field(name))
    template = models.new_template("Recognition")
    template["qfmt"] = FRONT
    template["afmt"] = BACK
    models.add_template(model, template)
    model["css"] = CSS
    models.add_dict(model)
    return models.by_name(NOTETYPE)


def existing_words(col, model, deck_id):
    """Lowercased Word of every note of our type with a card in this deck."""
    nids = col.db.list(
        "select distinct n.id from notes n join cards c on c.nid = n.id "
        "where n.mid = ? and (c.did = ? or c.odid = ?)",
        model["id"],
        deck_id,
        deck_id,
    )
    out = set()
    for nid in nids:
        out.add(col.get_note(nid)["Word"].strip().lower())
    return out


def import_words(col, saved, prefix=DEFAULT_PREFIX, dry_run=False):
    """Add each saved word to its language's deck. Returns (added, skipped)."""
    model = ensure_notetype(col) if not dry_run else col.models.by_name(NOTETYPE)
    added, skipped = [], []
    seen = {}
    for s in saved:
        if not s.word:
            skipped.append((s, "no headword"))
            continue
        deck = deck_name(prefix, s.language)
        fields = note_fields(s)
        if dry_run:
            key = (deck, s.word.lower())
            if key in seen:
                skipped.append((s, "already in the deck"))
                continue
            seen[key] = True
            added.append((s, deck, fields))
            continue
        deck_id = col.decks.id(deck)
        if deck_id not in seen:
            seen[deck_id] = existing_words(col, model, deck_id)
        if s.word.lower() in seen[deck_id]:
            skipped.append((s, "already in the deck"))
            continue
        note = col.new_note(model)
        for name, value in fields.items():
            note[name] = value
        note.tags = ["crossplay", f"lang::{s.language}"]
        col.add_note(note, deck_id)
        seen[deck_id].add(s.word.lower())
        added.append((s, deck, fields))
    return added, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("collection", help="path to collection.anki2")
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--card", help="the reader's card, mounted, or any directory holding study/")
    source.add_argument("--device", help="the reader over Wi-Fi, e.g. http://192.168.1.11")
    ap.add_argument("--deck-prefix", default=DEFAULT_PREFIX, help="parent deck (default: Dictionary)")
    ap.add_argument("--dry-run", action="store_true", help="print the notes, write nothing")
    ap.add_argument("--keep", action="store_true", help="leave the imported words on the card")
    args = ap.parse_args()

    saved = read_device(args.device) if args.device else read_card(args.card)
    if not saved:
        print("No saved words on the reader.")
        return 0

    from anki.collection import Collection

    col = Collection(str(pathlib.Path(args.collection).resolve()))
    try:
        added, skipped = import_words(col, saved, args.deck_prefix, args.dry_run)
    finally:
        col.close()

    for s, deck, fields in added:
        extra = " · ".join(x for x in (fields["Transcription"], fields["Part of speech"]) if x)
        print(f"  {'would add' if args.dry_run else 'added'}  {deck:24} {fields['Word']:18} {fields['Meaning']}")
        if extra:
            print(f"  {'':34}{'':18} {extra}")
    for s, why in skipped:
        print(f"  skipped {s.word or s.origin}: {why}")
    verb = "would add" if args.dry_run else "added"
    print(f"\n{len(added)} {verb}, {len(skipped)} skipped")

    if not args.dry_run and not args.keep:
        remove_imported([s for s, _, _ in added] + [s for s, why in skipped if why == "already in the deck"],
                        args.device)
    return 0


if __name__ == "__main__":
    sys.exit(main())
