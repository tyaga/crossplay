#!/usr/bin/env python3
"""What a dictionary entry becomes on a flashcard.

The entries below are real, trimmed only of senses past the ones that make the
card, one or more from each dictionary the reader carries: WikDict en-ru and
nl-ru (glosses in the source language beside translations), BARS en-ru
(numbered paragraphs, each followed by usage examples), a WordNet-style nl-en
(numbered lines, glosses after a dash), and the Russian and English
Wiktionaries for Icelandic (sections, headings, Bible verses as examples).
Standard library only. Run directly:

    python3 tools_local/study/test_dict_card.py
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import dict_card  # noqa: E402

checks = 0
failures = 0


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print(f"  FAIL {what}")


def eq(got, want, what):
    check(got == want, what)
    if got != want:
        print(f"       got:  {got!r}\n       want: {want!r}")


DEVICE = (
    '<div>/<font color="gray">dəˈvaɪs</font>/, /<font color="gray">dɪˈvaɪs</font>/<br>'
    '<div><font class="grammar" color="green">noun</font></div><ol><li>piece of equipment<ol>'
    "<li><div>аппара́т</div></li><li><div>га́джет</div></li><li><div>дева́йс</div></li>"
    "<li><div>механи́зм</div></li><li><div>прибо́р</div></li></ol></li>"
    "<li>heraldry: personal motto or emblem<ol><li><div>деви́з</div></li><li><div>эмбле́ма</div></li></ol></li>"
    "</ol></div>"
)
HOUSE = (
    '<div>/<font color="gray">ˈhaʊ̯s</font>/<br><div><font class="grammar" color="green">noun</font></div>'
    "<ol><li><ol><li>an establishment, business</li><li>astrology: one of the twelve divisions</li></ol>"
    "<div>дом</div></li><li>dynasty, familiar descendance<ol><li><div>дом</div></li>"
    "<li><div>дина́стия</div></li></ol></li></ol></div>"
)
BEAUTIFUL = (
    '<div>/<font color="gray">ˈbjuːtɪ.fəl</font>/<br><div><font class="grammar" color="green">adjective</font></div>'
    "<ol><li><div>краси́вый</div></li><li><div>прекра́сный</div></li><li><div>приго́жий</div></li></ol></div>"
)
RUN = (
    '<div>/<font color="gray">ɹʌn</font>/<br><div><font class="grammar" color="green">noun</font></div>'
    "<ol><li>quick pace<div>бег</div></li><li>route taken while running<ol><li><div>маршру́т</div></li>"
    "<li><div>путь</div></li></ol></li></ol></div>"
)
HESTUR = (
    '<section class="mw-parser-output"><section><h4><span></span>Морфологические и синтаксические свойства</h4>'
    "<p><b>hestur</b></p><p>Существительное, мужской род.</p></section><section><h4>Произношение</h4>"
    '<ul class="transcription"><li>МФА:<span>&nbsp;</span><span>[</span><span class="IPA">ˈhɛstʏr</span>'
    '<span>]</span></li></ul></section><h4><span></span>Значение</h4><ol><li><i></i> '
    '<a href="bword://лошадь">лошадь</a>, <a href="bword://конь">конь</a> </li></ol></section>'
)
HUS = (
    '<section class="mw-parser-output"><section><h4>Морфологические и синтаксические свойства</h4>'
    "<p><b>hús</b></p><p>Существительное, средний род.</p></section><h4>Значение</h4><ol><li>"
    '<a href="bword://дом">дом</a> <span class="example-fullblock "><span>◆</span>'
    '<span class="example-block">Og ef eitt <b>hús</b> tvístrast í sjálfu sér.'
    '<span class="example-translate"> — И если дом разделится сам в себе.</span></span></span></li></ol></section>'
)
BARS_STYLE = "device\n [dɪ'vaɪs] n\n 1) устройство; приспособление; механизм\n 2) девиз, эмблема\n _Ex: a clever device\n"

f = dict_card.card_fields(DEVICE, True, "device")
eq(f["pronunciation"], "/dəˈvaɪs/", "the first transcription, and only the first")
eq(f["pos"], "noun", "the part of speech from WikDict's grammar marker")
eq(f["meaning"], "аппарат, гаджет, девайс; девиз, эмблема",
   "translations grouped by sense, glosses dropped, stress marks gone, three per sense")

eq(dict_card.card_fields(HOUSE, True, "house")["meaning"], "дом; династия",
   "a gloss is dropped at any depth when the entry has translations in another script")
eq(dict_card.card_fields(BEAUTIFUL, True, "beautiful")["meaning"], "красивый, прекрасный, пригожий",
   "one-word items with no gloss are one sense, not three")
eq(dict_card.card_fields(RUN, True, "run")["meaning"], "бег; маршрут, путь",
   "a sense whose translation sits beside its gloss keeps the translation")

f = dict_card.card_fields(HESTUR, True, "hestur")
eq(f["pronunciation"], "/ˈhɛstʏr/", "Wiktionary's IPA span wins over the brackets around it")
eq(f["pos"], "noun, m", "the part of speech and the gender, from Russian prose")
eq(f["meaning"], "лошадь, конь", "the transcription list is not a sense")

f = dict_card.card_fields(HUS, True, "hús")
eq(f["meaning"], "дом", "usage examples and their translations are dropped")
eq(f["pos"], "noun, n", "neuter")

f = dict_card.card_fields(BARS_STYLE, False, "device")
eq(f["pronunciation"], "/dɪ'vaɪs/", "a plain-text transcription in brackets")
eq(f["pos"], "noun", "a bare abbreviation on the transcription line")
eq(f["meaning"], "устройство, приспособление, механизм; девиз, эмблема",
   "numbered senses from plain text, examples dropped")

BARS_TAKE = (
    "<p><b>1.</b> захват, взятие; получение</p><p><b>2.</b> <i>шахм.</i> взятие (фигуры)</p>"
    "<p>&#8195;<i>great take of fish большой улов рыбы</i></p><p><b>3.</b> <i>сл.</i> выручка, барыши</p>"
)
f = dict_card.card_fields(BARS_TAKE, True, "take")
eq(f["meaning"], "захват, взятие, получение; выручка, барыши",
   "BARS: numbered paragraphs are senses, register labels and examples go, a repeated word is not repeated")
eq(f["pos"], "", "BARS names no part of speech, and none is invented")

BARS_BOOK = (
    "<p><b>1.</b> книга</p><p><b>2.</b> сброшюрованные листы чистой или разграфленной бумаги</p>"
    "<p><b>3.</b> том</p>"
)
eq(dict_card.card_fields(BARS_BOOK, True, "book")["meaning"], "книга; том",
   "a description is not a word to learn when the entry has words")

NL_EN_MOOI = (
    '<div style="color:#666666">/moːi̯/</div><div><i>adj.</i> <span style="color:#666666">1.</span> '
    "<b>beautiful, pretty, handsome</b><br/><i style=\"color:#666666\">&ldquo;Wat een mooi meisje.&rdquo;</i><br/>"
    '<span style="color:#666666">2.</span> <b>nice, good</b><br/><span style="color:#666666">3.</span> '
    '<b>pretty, beautiful, nice</b> <span style="color:#666666">&mdash; prettig in voorkomen.</span></div>'
)
f = dict_card.card_fields(NL_EN_MOOI, True, "mooi")
eq(f["pronunciation"], "/moːi̯/", "nl-en: the transcription in its own div")
eq(f["pos"], "adj.", "nl-en: the italic label at the head of the entry")
eq(f["meaning"], "beautiful, pretty, handsome; nice, good",
   "nl-en: numbered lines, the gloss after the dash and the quoted examples dropped")

NL_RU_MOOI = (
    '<div>/<font color="gray">moːj</font>/<br> <div><font class="grammar" color="green">adjective</font></div>'
    "1. prettig in voorkomen, aangenaam om naar te kijken<div>красивый</div></div>"
    "<p>&#8212;&#8212;&#8212;</p><p><i>adj</i> красивый, симпатичный</p>"
)
eq(dict_card.card_fields(NL_RU_MOOI, True, "mooi")["meaning"], "красивый; симпатичный",
   "nl-ru: the Russian under a numbered Dutch gloss, and the dictionary merged in after the dashes")

IS_EN_HESTUR = (
    '<section class="mw-parser-output"><h4>Noun</h4><ul><li>IPA<span>:</span> <span class="IPA nowrap">'
    '[ˈhɛstʏr]</span></li></ul><p><span class="headword-line"><strong>hestur</strong> <span class="gender">'
    '<abbr title="masculine gender">m</abbr></span> (<i>genitive singular</i> <b>hests</b>)</span></p>'
    '<ol><li><a href="bword://horse">horse</a><dl><dd><span class="nyms synonym">Synonyms: hross</span>'
    "</dd></dl></li></ol></section>"
)
f = dict_card.card_fields(IS_EN_HESTUR, True, "hestur")
eq(f["pronunciation"], "/ˈhɛstʏr/", "is-en: Wiktionary's IPA")
eq(f["pos"], "noun, m", "is-en: the part of speech from the heading, the gender from its own element")
eq(f["meaning"], "horse", "is-en: the IPA list is not a sense and synonyms are not translations")

IS_RU_OG = (
    '<section class="mw-parser-output"><section><h4>Морфологические и синтаксические свойства</h4>'
    "<p><b>og</b></p><p>Союз; неизменяемое.</p><p><span>Корень: </span><b>--</b></p></section>"
    '<h4>Значение</h4><ol><li><i></i> <a href="bword://и">и</a></li></ol></section>'
)
eq(dict_card.card_fields(IS_RU_OG, True, "og")["meaning"], "и", "a one-letter translation is a translation")

long_entry = "<ol>" + "".join(f"<li><div>слово{i}</div><div>вариант{i}</div></li>" for i in range(12)) + "</ol>"
m = dict_card.card_fields(long_entry, True, "x")["meaning"]
check(len(m) <= dict_card.MAX_MEANING_CHARS, "a long entry is capped to fit a card face")
check(m.count(";") <= dict_card.MAX_SENSES - 1, "and to three senses")

eq(dict_card.strip_stress("прибо́р héllo"), "прибор héllo",
   "stress marks go, precomposed letters stay")
eq(dict_card.card_fields("", True, "x")["meaning"], "", "an empty entry makes an empty meaning, not a crash")

print(f"{'PASS' if failures == 0 else 'FAIL'} {checks} checks, {failures} failed")
sys.exit(1 if failures else 0)
