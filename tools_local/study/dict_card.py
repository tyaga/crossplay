#!/usr/bin/env python3
"""What goes on a flashcard made from a dictionary entry.

A StarDict entry is written to be read, not memorised: the en-ru WikDict entry
for "device" is four senses, eighteen translations and four IPA variants, BARS
follows every sense with a dozen usage examples, and the Russian Wiktionary
quotes the Bible. A card face holds a handful of words. This module keeps what
a learner recalls and drops the rest:

    word            device
    pronunciation   /dəˈvaɪs/                 the first transcription only
    part of speech  noun                      normalised across dictionaries
    meaning         аппарат, гаджет, девайс; девиз, эмблема

The meaning is built from TRANSLATIONS, not glosses. In a bilingual entry the
source-language gloss ("piece of equipment") says which sense this is, and the
target-language words under it are what you learn. Senses are joined with
"; ", items within a sense with ", ", and the whole is capped to fit a card
face. Usage examples, register labels ("разг.", "шахм."), parenthesised
qualifiers and stress marks are dropped: a stress mark is a combining
character over the reader's own language, noise to a native speaker.

Written against the six dictionaries this reader carries -- WikDict en-ru and
nl-ru, BARS en-ru, a WordNet-style nl-en, and the Russian and English
Wiktionaries for Icelandic -- which between them cover the shapes StarDict
HTML comes in: nested lists, numbered paragraphs, numbered lines with glosses
after a dash, and Wiktionary sections. Standard library only:

    python3 tools_local/study/test_dict_card.py
"""

from __future__ import annotations

import html
import re
import unicodedata
from html.parser import HTMLParser

MAX_MEANING_CHARS = 110
MAX_SENSES = 5
MAX_ITEMS_PER_SENSE = 3
# An item longer than this is a description or a usage note ("сброшюрованные
# листы чистой или разграфленной бумаги"), not a word to learn. Dropped
# whenever the entry has anything shorter.
MAX_ITEM_CHARS = 40

# Subtrees that are never meaning: headings ("Произношение", "Noun"), usage
# notes and synonym lists (<dl>), and anything classed as an example.
SKIP_TAGS = {"h1", "h2", "h3", "h4", "h5", "h6", "script", "style", "sup", "dl"}
SKIP_CLASS_PARTS = ("example", "etymology", "nyms", "headword-line")
# Italics are labels ("n.", "разг.", "(ergative)") or usage examples in every
# dictionary here, never a translation.
ITALIC_TAGS = {"i", "em"}
BLOCK_TAGS = {"div", "p", "li", "br", "ol", "ul", "section", "tr", "dd", "dt", "blockquote"}

IPA_CHARS = set("ˈˌəɪʊʌɑɒɔɛæθðʃʒŋːɹɾʏøœɐɜɵʉɨɯɤʎɲʁχɣβçʔ")

POS_LABELS = {
    "noun": "noun",
    "proper noun": "proper noun",
    "verb": "verb",
    "adjective": "adj.",
    "adverb": "adv.",
    "pronoun": "pron.",
    "preposition": "prep.",
    "conjunction": "conj.",
    "interjection": "interj.",
    "numeral": "num.",
    "number": "num.",
    "article": "art.",
    "determiner": "det.",
    "particle": "part.",
    "phrase": "phrase",
    "abbreviation": "abbr.",
    "prefix": "prefix",
    "suffix": "suffix",
    "существительное": "noun",
    "имя собственное": "proper noun",
    "глагол": "verb",
    "прилагательное": "adj.",
    "наречие": "adv.",
    "местоимение": "pron.",
    "предлог": "prep.",
    "союз": "conj.",
    "междометие": "interj.",
    "числительное": "num.",
    "частица": "part.",
    "артикль": "art.",
}
# A bare abbreviation marks the part of speech in a label position only: an
# italic at the head of an entry ("<i>n.</i>"), or after a plain-text
# transcription ("[dɪ'vaɪs] n"). "a" or "n" inside a sentence is a word.
POS_ABBREVIATIONS = {
    "n": "noun",
    "s": "noun",
    "v": "verb",
    "vt": "verb",
    "vi": "verb",
    "a": "adj.",
    "adj": "adj.",
    "adv": "adv.",
    "prep": "prep.",
    "pron": "pron.",
    "conj": "conj.",
    "int": "interj.",
    "interj": "interj.",
    "num": "num.",
}
GENDERS = (
    ("мужской род", "m"),
    ("женский род", "f"),
    ("средний род", "n"),
    ("masculine", "m"),
    ("feminine", "f"),
    ("neuter", "n"),
)

NUMBERING_RE = re.compile(r"^\s*(?:\d+|[a-zа-я])[.)]\s*", re.IGNORECASE)
SLASHED_RE = re.compile(r"/([^/\n]{1,48})/")
BRACKETED_RE = re.compile(r"\[([^\]\n]{1,48})\]")
PAREN_RE = re.compile(r"\([^()]*\)")
GLOSS_DASH_RE = re.compile(r"\s[—–]\s")
SEPARATOR_RE = re.compile(r"^[\s—–\-_=*]{3,}$")
WHITESPACE_RE = re.compile(r"\s+")


class _Node:
    __slots__ = ("tag", "classes", "children", "parent")

    def __init__(self, tag, classes, parent):
        self.tag = tag
        self.classes = classes
        self.children = []
        self.parent = parent


class _TreeBuilder(HTMLParser):
    """Enough of a DOM for this job: tags, class names, text, nesting.

    StarDict HTML is tag soup -- unclosed <li>, stray </div> -- so a close tag
    pops back to the nearest matching open element and an unmatched one is
    ignored, which is close enough to a browser for text extraction.
    """

    VOID = {"br", "img", "hr", "meta", "input", "wbr"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = _Node("root", (), None)
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        node = _Node(tag, tuple((dict(attrs).get("class") or "").split()), self.cur)
        self.cur.children.append(node)
        if tag not in self.VOID:
            self.cur = node

    def handle_startendtag(self, tag, attrs):
        self.cur.children.append(_Node(tag, tuple((dict(attrs).get("class") or "").split()), self.cur))

    def handle_endtag(self, tag):
        node = self.cur
        while node is not self.root and node.tag != tag:
            node = node.parent
        if node is not self.root:
            self.cur = node.parent

    def handle_data(self, data):
        self.cur.children.append(data)


def _has_class(node, part):
    return any(part in c.lower() for c in node.classes)


def _skipped(node):
    return (
        node.tag in SKIP_TAGS
        or node.tag in ITALIC_TAGS
        or any(_has_class(node, part) for part in SKIP_CLASS_PARTS)
    )


def _text(node, stop_at_lists=False, skip=True):
    """Visible text of a subtree, block elements breaking lines."""
    out = []

    def walk(n):
        for child in n.children:
            if isinstance(child, str):
                out.append(child)
                continue
            if skip and _skipped(child):
                continue
            if stop_at_lists and child.tag in ("ol", "ul"):
                continue
            if child.tag in BLOCK_TAGS:
                out.append("\n")
            walk(child)
            if child.tag in BLOCK_TAGS:
                out.append("\n")

    walk(node)
    return "".join(out)


def _find_all(node, predicate, skip=True):
    found = []

    def walk(n):
        for child in n.children:
            if isinstance(child, str) or (skip and _skipped(child)):
                continue
            if predicate(child):
                found.append(child)
            walk(child)

    walk(node)
    return found


def _clean(text):
    return WHITESPACE_RE.sub(" ", text).strip(" \t\n,;:.—–-")


def strip_stress(text):
    """Drop combining marks, keeping precomposed letters (á, ö) intact."""
    return "".join(c for c in text if not unicodedata.combining(c))


# --- pronunciation, part of speech ------------------------------------------


def _looks_like_ipa(chunk):
    return any(c in IPA_CHARS for c in chunk) or "'" in chunk


def find_pronunciation(text, ipa_text=None):
    if ipa_text:
        chunk = _clean(ipa_text).strip("[]/ ")
        if chunk:
            return f"/{chunk}/"
    for regex in (SLASHED_RE, BRACKETED_RE):
        for match in regex.finditer(text):
            chunk = match.group(1).strip()
            if chunk and _looks_like_ipa(chunk):
                return f"/{chunk.strip('[]/ ')}/"
    return ""


def pos_label(text):
    """The part-of-speech label a phrase names, or ""."""
    low = text.lower()
    for phrase in sorted(POS_LABELS, key=len, reverse=True):
        if re.search(rf"(?<![\w]){re.escape(phrase)}(?![\w])", low):
            return POS_LABELS[phrase]
    return POS_ABBREVIATIONS.get(low.strip().rstrip("."), "")


def gender_mark(text):
    low = text.lower()
    for phrase, mark in GENDERS:
        if phrase in low:
            return mark
    return ""


def normalise_pos(text):
    """A part-of-speech label and gender from one phrase, or "" for none."""
    label = pos_label(text) if len(text) < 80 else ""
    if not label:
        return ""
    mark = gender_mark(text)
    return f"{label}, {mark}" if mark else label


def _html_pos(root, full):
    label = ""
    for n in _find_all(root, lambda n: _has_class(n, "grammar"), skip=False):
        label = pos_label(_text(n, skip=False))
        if label:
            break
    if not label:
        for n in _find_all(root, lambda n: n.tag in ("h3", "h4", "h5"), skip=False):
            label = pos_label(_text(n, skip=False))
            if label:
                break
    if not label:
        # The first italic of the entry, if it is a label ("<i>v.</i>").
        italics = _find_all(root, lambda n: n.tag in ITALIC_TAGS, skip=False)
        if italics:
            first = _clean(_text(italics[0], skip=False))
            if len(first) <= 12:
                label = POS_ABBREVIATIONS.get(first.lower().rstrip("."), "") or (
                    POS_LABELS.get(first.lower(), "")
                )
    mark = ""
    if not label:
        # Prose, as the Russian Wiktionary writes it: "Существительное, мужской
        # род." The gender is read from that line and nowhere else -- a gloss
        # elsewhere can say "feminine" about something that is not the word.
        for line in full.split("\n"):
            if len(line) < 80 and not POS_ABBREVIATIONS.get(line.strip().lower()):
                label = pos_label(line)
                if label:
                    mark = gender_mark(line)
                    break
    if not label:
        return ""
    genders = _find_all(root, lambda n: _has_class(n, "gender"), skip=False)
    if genders:
        g = _clean(_text(genders[0], skip=False)).lower()
        mark = g if g in ("m", "f", "n") else gender_mark(g)
    return f"{label}, {mark}" if mark else label


# --- senses ---------------------------------------------------------------------


def _split_items(text):
    items = []
    for part in re.split(r"[;,]\s*|\n+", PAREN_RE.sub(" ", text)):
        part = _clean(NUMBERING_RE.sub("", part))
        if part and not part.isdigit():
            items.append(part)
    return items


def _in_list(node, root):
    p = node.parent
    while p is not None and p is not root:
        if p.tag == "li":
            return True
        p = p.parent
    return False


def _is_pronunciation_item(li):
    """A list item that is a transcription, as Wiktionary lays them out."""
    p = li
    while p is not None:
        if _has_class(p, "transcription"):
            return True
        p = p.parent
    if _find_all(li, lambda n: any(c.upper() == "IPA" for c in n.classes), skip=False):
        return True
    head = _clean(_text(li))[:12].lower()
    return head.startswith(("ipa", "мфа"))


def _child_divs(li):
    return [d for d in li.children if not isinstance(d, str) and d.tag == "div" and not _skipped(d)]


def _own_text(li):
    """Text directly in this item, outside nested lists and child divs."""
    parts = []
    for child in li.children:
        if isinstance(child, str):
            parts.append(child)
        elif not _skipped(child) and child.tag not in ("ol", "ul", "div"):
            parts.append(_text(child))
    return _clean("".join(parts))


def _list_senses(root):
    """Senses from an entry's list structure, or [] when it has none."""
    senses = []
    top = [
        li
        for li in _find_all(root, lambda n: n.tag == "li")
        if not _in_list(li, root) and not _is_pronunciation_item(li)
    ]
    for li in top:
        nested = _find_all(li, lambda n: n.tag == "li")
        divs = _child_divs(li)
        if nested:
            items = []
            for leaf in nested:
                if not _find_all(leaf, lambda n: n.tag == "li"):
                    items.extend(_split_items(_text(leaf)))
            for d in divs:
                items.extend(_split_items(_text(d)))
            senses.append((items, True))
        elif divs:
            items = []
            for d in divs:
                items.extend(_split_items(_text(d)))
            senses.append((items, bool(_own_text(li))))
        else:
            senses.append((_split_items(_text(li, stop_at_lists=True)), True))

    # WikDict writes a sense with no gloss as one <li><div>word</div></li> per
    # translation: "beautiful" is five such items, and they are one sense.
    merged = []
    for items, glossed in senses:
        if not items:
            continue
        if not glossed and len(items) == 1 and merged and not merged[-1][1]:
            merged[-1][0].extend(items)
        else:
            merged.append((list(items), glossed))
    return [items for items, _ in merged]


def _is_meta_line(line, headword):
    low = line.lower().strip()
    if not low or low == headword.lower():
        return True
    if normalise_pos(line) and len(line) < 40:
        return True
    rest = _clean(SLASHED_RE.sub("", BRACKETED_RE.sub("", line)))
    return bool(find_pronunciation(line)) and len(rest) < 3


def _text_senses(text, headword):
    """Senses from running text.

    Numbered lines are senses, and the unnumbered lines under one belong to it:
    WikDict nl-ru puts the Russian word on the line after the Dutch gloss it
    translates. Without numbering every line is a sense. A line of dashes is a
    second dictionary merged in, and starts afresh.
    """
    lines = [l.strip() for l in text.split("\n")]
    lines = [l for l in lines if l]
    numbered = any(NUMBERING_RE.match(l) for l in lines)
    senses = []
    current = None
    after_separator = False
    for line in lines:
        if SEPARATOR_RE.match(line):
            current = None
            after_separator = True
            continue
        if line.startswith(("_", "~", "Ex:", "◆")) or _is_meta_line(line, headword):
            continue
        line = GLOSS_DASH_RE.split(line, maxsplit=1)[0]
        if numbered and not after_separator and not NUMBERING_RE.match(line):
            if current is not None:
                current.extend(_split_items(line))
            continue
        current = [i for i in _split_items(line) if not _is_meta_line(i, headword)]
        senses.append(current)
    return [s for s in senses if s]


def _script(text):
    for c in text:
        o = ord(c)
        if 0x0400 <= o <= 0x052F:
            return "cyrillic"
        if 0x0370 <= o <= 0x03FF:
            return "greek"
        if o > 0x2E80:
            return "cjk"
    return "latin" if any(c.isalpha() for c in text) else ""


def _translations_only(senses, headword):
    """In a bilingual entry, drop glosses written in the headword's language.

    WikDict puts source-language glosses ("an establishment, business") beside
    target-language translations ("дом") at every level of nesting, so the
    structure cannot tell them apart; the script can. When nothing is in a
    second script -- nl-en, is-en -- everything stays.
    """
    source = _script(headword)
    if not source or not any(_script(i) not in ("", source) for s in senses for i in s):
        return senses
    kept = [[i for i in s if _script(i) not in ("", source)] for s in senses]
    return [s for s in kept if s]


def _compose(senses, headword):
    senses = _translations_only(senses, headword)
    if any(len(i) <= MAX_ITEM_CHARS for sense in senses for i in sense):
        senses = [[i for i in sense if len(i) <= MAX_ITEM_CHARS] for sense in senses]
        senses = [sense for sense in senses if sense]
    seen = set()
    groups = []
    used = 0
    for sense in senses[:MAX_SENSES]:
        group = []
        for item in sense:
            item = strip_stress(item)
            key = item.lower()
            if key in seen or key == headword.lower():
                continue
            if len(group) >= MAX_ITEMS_PER_SENSE:
                break
            cost = len(item) + 2
            if used + cost > MAX_MEANING_CHARS and (groups or group):
                break
            seen.add(key)
            group.append(item)
            used += cost
        if group:
            groups.append(", ".join(group))
        if used >= MAX_MEANING_CHARS:
            break
    meaning = "; ".join(groups)
    if len(meaning) > MAX_MEANING_CHARS:
        meaning = meaning[: MAX_MEANING_CHARS - 1].rstrip(" ,;") + "…"
    return meaning


def card_fields(raw, is_html, headword):
    """Pronunciation, part of speech and a card-sized meaning for one entry."""
    raw = raw.replace("\0", "\n")
    if is_html:
        builder = _TreeBuilder()
        builder.feed(raw)
        builder.close()
        root = builder.root
        full = html.unescape(_text(root, skip=False))
        ipa = _find_all(root, lambda n: any(c.upper() == "IPA" for c in n.classes), skip=False)
        pronunciation = find_pronunciation(full, _text(ipa[0], skip=False) if ipa else None)
        pos = _html_pos(root, full)
        senses = _list_senses(root) or _text_senses(html.unescape(_text(root)), headword)
    else:
        text = html.unescape(raw)
        pronunciation = find_pronunciation(text)
        pos = ""
        for line in text.split("\n")[:4]:
            pos = normalise_pos(line)
            if not pos:
                tail = BRACKETED_RE.sub("", line).strip().lower().rstrip(".")
                pos = POS_ABBREVIATIONS.get(tail, "")
            if pos:
                break
        senses = _text_senses(text, headword)
    return {
        "pronunciation": pronunciation,
        "pos": pos,
        "meaning": _compose(senses, headword),
    }
