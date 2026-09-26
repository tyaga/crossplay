#!/usr/bin/env python3
"""Saved words become notes in a deck per language, once each.

Builds a throwaway collection and a throwaway card, never touches a real one.
Needs Anki's library, so it runs in the tooling venv:

    .venv-study/bin/python tools_local/study/test_words_to_anki.py
"""

import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from anki.collection import Collection  # noqa: E402

import words_to_anki as w  # noqa: E402

checks = 0
failures = 0


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print(f"  FAIL {what}")


ENTRY_NL = (
    '<div><font class="grammar">noun</font></div><ol><li>building for living in'
    "<ol><li><div>house</div></li><li><div>home</div></li></ol></li></ol>"
)
ENTRY_EN = '<div>/<font>dəˈvaɪs</font>/<br><div><font class="grammar">noun</font></div><ol><li><div>прибо́р</div></li></ol>'


def save(card, lang, stem, header, entry):
    d = card / w.INBOX / lang
    d.mkdir(parents=True, exist_ok=True)
    head = "".join(f"{k}: {v}\n" for k, v in header.items())
    (d / f"{stem}.txt").write_text(head + "\n" + entry, "utf-8")


with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    card = tmp / "card"
    save(card, "nl", "huis", {"word": "huis", "looked-up": "huizen", "dictionary": "nl-en", "language": "nl",
                              "format": "html", "context": "De huizen staan aan het water.",
                              "source": "Dik Trom"}, ENTRY_NL)
    save(card, "en", "device", {"word": "device", "dictionary": "en-ru", "language": "en", "format": "html"},
         ENTRY_EN)
    save(card, "en", "device-again", {"word": "Device", "dictionary": "en-ru-bars", "language": "en",
                                      "format": "html"}, ENTRY_EN)
    (card / w.INBOX / "en" / "half.txt.part").write_text("word: half\n", "utf-8")

    saved = w.read_card(card)
    check(len(saved) == 3, "every finished file is read, and a .part left by a power cut is not")
    check({s.language for s in saved} == {"nl", "en"}, "the language comes from the file")

    col = Collection(str(tmp / "collection.anki2"))

    added, skipped = w.import_words(col, saved, dry_run=True)
    check(len(added) == 2 and len(skipped) == 1, "a dry run reports what it would do")
    check(col.models.by_name(w.NOTETYPE) is None and col.note_count() == 0, "and writes nothing")

    added, skipped = w.import_words(col, saved)
    check(len(added) == 2, "two words added")
    check(len(skipped) == 1 and skipped[0][1] == "already in the deck",
          "the same word saved twice, in different case, is one note")

    names = {d.name for d in col.decks.all_names_and_ids()}
    check({"Dictionary::Dutch", "Dictionary::English"} <= names, "a deck per language under Dictionary")

    model = col.models.by_name(w.NOTETYPE)
    check([f["name"] for f in model["flds"]] == list(w.FIELDS), "the note type carries the fields in order")

    nid = col.find_notes('"deck:Dictionary::Dutch"')[0]
    note = col.get_note(nid)
    check(note["Word"] == "huis", "the headword is the front")
    check(note["Meaning"] == "house, home", "the meaning is the card-sized one")
    check(note["Sentence"] == "De huizen staan aan het water.", "the sentence it was read in")
    check(note["Source"] == "Dik Trom", "and the book")
    check("lang::nl" in note.tags and "crossplay" in note.tags, "tagged for finding them later")

    en = col.get_note(col.find_notes('"deck:Dictionary::English"')[0])
    check(en["Meaning"] == "прибор", "stress marks are dropped from the meaning")
    check(en["Transcription"] == "/dəˈvaɪs/", "the transcription is kept for Anki")

    added, skipped = w.import_words(col, w.read_card(card))
    check(not added and len(skipped) == 3, "importing the same inbox again adds nothing")

    added, _ = w.import_words(col, saved, prefix="Reader")
    check(len(added) == 2, "another prefix is another set of decks")
    col.close()

    w.remove_imported(saved)
    check(not list((card / w.INBOX).glob("*/*.txt")), "imported files are removed from the card")
    check((card / w.INBOX / "en" / "half.txt.part").exists(), "and nothing else is")

    check(w.deck_name("Dictionary", "xx") == "Dictionary::XX", "an unknown code still names a deck")
    parsed = w.parse("word: a: b\nformat: text\n\nbody\n\nmore", "is", "x")
    check(parsed.header["word"] == "a: b", "a colon inside a value survives")
    check(parsed.entry == "body\n\nmore", "the entry is everything after the first blank line")

print(f"{'PASS' if failures == 0 else 'FAIL'} {checks} checks, {failures} failed")
sys.exit(1 if failures else 0)
