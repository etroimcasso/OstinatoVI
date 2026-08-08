#!/usr/bin/env python3
"""Unit tests for parse_monster_items.py.

Three layers per the parser-test discipline:
  1. pure helpers (renderer formatting);
  2. synthetic inputs (macro-body anchor, steal/drop alternation, arity,
     teardown ordering, unrecognized lines);
  3. end-to-end against the real original-src monster_items.asm + const.inc
     (skipped cleanly when the rip output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_monster_items as pmi
from common import ParseError


_MACRO_PROLOGUE = (
    ".mac monster_steal rare_item, common_item\n"
    "        .byte ITEM::rare_item, ITEM::common_item\n"
    ".endmac\n"
    ".define monster_drop monster_steal\n"
)

_ONE_RECORD = (
    "        monster_steal POTION, TONIC\n"
    "        monster_drop  TONIC, EMPTY\n"
)


class FakeSymbols(object):
    """Duck-typed stand-in for pmi.Symbols over a tiny ITEM space."""

    _ITEMS = {"POTION": 0xE8, "TONIC": 0xE7, "EMPTY": 0xFF}

    def item_value(self, member, path, lineno):
        val = self._ITEMS.get(member)
        if val is None:
            raise ParseError(path, lineno, "unknown ITEM::{}".format(member))
        return val


def _parse_text(text):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w") as fh:
        fh.write(text)
    try:
        return pmi.parse_monster_items(path, FakeSymbols())
    finally:
        os.remove(path)


class SyntheticGrammarTests(unittest.TestCase):

    def _assert_parse_error(self, text, fragment):
        with self.assertRaises(ParseError) as ctx:
            _parse_text(text)
        self.assertIn(fragment, str(ctx.exception))

    def test_wellformed_pair_reaches_the_count_check(self):
        # One complete pair parses through the whole grammar; the corpus-count
        # verification is then the (expected) failure — proving the record
        # itself was accepted.
        self._assert_parse_error(_MACRO_PROLOGUE + _ONE_RECORD,
                                 "produced 1 records; expected 384")

    def test_macro_body_mismatch_raises(self):
        text = (".mac monster_steal rare_item, common_item\n"
                "        .byte ITEM::common_item, ITEM::rare_item\n"
                ".endmac\n"
                ".define monster_drop monster_steal\n" + _ONE_RECORD)
        self._assert_parse_error(text, "does not match the documented "
                                       "byte-order shape")

    def test_unexpected_macro_body_line_raises(self):
        text = (".mac monster_steal rare_item, common_item\n"
                "        lda #$00\n"
                ".endmac\n")
        self._assert_parse_error(text, "unexpected macro-body line")

    def test_steal_after_steal_raises(self):
        text = (_MACRO_PROLOGUE +
                "        monster_steal POTION, TONIC\n"
                "        monster_steal TONIC, TONIC\n")
        self._assert_parse_error(text, "still awaits its monster_drop")

    def test_drop_without_steal_raises(self):
        text = _MACRO_PROLOGUE + "        monster_drop TONIC, EMPTY\n"
        self._assert_parse_error(text, "without a preceding monster_steal")

    def test_drop_before_define_raises(self):
        text = (".mac monster_steal rare_item, common_item\n"
                "        .byte ITEM::rare_item, ITEM::common_item\n"
                ".endmac\n"
                "        monster_steal POTION, TONIC\n"
                "        monster_drop  TONIC, EMPTY\n")
        self._assert_parse_error(text, "before its .define alias")

    def test_missing_final_drop_raises(self):
        text = _MACRO_PROLOGUE + "        monster_steal POTION, TONIC\n"
        self._assert_parse_error(text, "monster_drop line is missing")

    def test_wrong_arity_raises(self):
        self._assert_parse_error(
            _MACRO_PROLOGUE + "        monster_steal POTION\n",
            "expects 2 args")
        self._assert_parse_error(
            _MACRO_PROLOGUE +
            "        monster_steal POTION, TONIC, EMPTY\n",
            "expects 2 args")

    def test_unknown_item_raises(self):
        self._assert_parse_error(
            _MACRO_PROLOGUE + "        monster_steal BOGUS, TONIC\n",
            "unknown ITEM::BOGUS")

    def test_unexpected_define_raises(self):
        self._assert_parse_error(".define monster_drop something_else\n",
                                 "unexpected .define")

    def test_record_after_teardown_raises(self):
        text = (_MACRO_PROLOGUE + _ONE_RECORD +
                ".delmac monster_steal\n" + _ONE_RECORD)
        self._assert_parse_error(text, "after the .delmac teardown")

    def test_unrecognized_line_raises(self):
        self._assert_parse_error(_MACRO_PROLOGUE + "SomeLabel:\n",
                                 "unrecognized line")


class RendererFormattingTests(unittest.TestCase):

    @staticmethod
    def _record():
        rec = pmi.Record(0, ["POTION", "TONIC"], [0xE8, 0xE7])
        rec.drop_names = ["TONIC", "EMPTY"]
        rec.drop_bytes = [0xE7, 0xFF]
        rec.name = "GUARD"
        return rec

    def test_inc_row_renders_item_enumerators(self):
        inc = pmi.render_inc([self._record()])
        self.assertIn("MonsterItemsEntry{  // [$000]", inc)
        self.assertIn(".id = MonsterId::GUARD,", inc)
        self.assertIn(".rareSteal   = ItemId::POTION,", inc)
        self.assertIn(".commonSteal = ItemId::TONIC,", inc)
        self.assertIn(".rareDrop    = ItemId::TONIC,", inc)
        self.assertIn(".commonDrop  = ItemId::EMPTY,", inc)
        self.assertNotIn("0x", inc.split("DO NOT EDIT")[1])

    def test_fixture_row_renders_raw_bytes_with_decimal_id(self):
        fixture = pmi.render_fixture([self._record()])
        self.assertIn("std::array<ExpectedMonsterItemsEntry, 1>", fixture)
        self.assertIn(".id =   0,  // $000 GUARD", fixture)
        self.assertIn(".rareSteal = 0xE8, .commonSteal = 0xE7, "
                      ".rareDrop = 0xE7, .commonDrop = 0xFF", fixture)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle",
                                   "monster_items.asm")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src monster_items.asm not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = pmi.Symbols(os.path.join(root, "include", "const.inc"))
        cls.records = pmi.parse_monster_items(
            os.path.join(root, "src", "battle", "monster_items.asm"),
            cls.symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.records), 384)
        for rec in self.records:
            self.assertEqual(len(rec.bytes), 4)
            self.assertIsNotNone(rec.name)

    def test_first_record_is_guards_items(self):
        # Hand-traced from monster_items.asm's "; 0: guard" block:
        # steal POTION, TONIC / drop TONIC, EMPTY.
        guard = self.records[0]
        self.assertEqual(guard.name, "GUARD")
        self.assertEqual(guard.steal_names, ["POTION", "TONIC"])
        self.assertEqual(guard.drop_names, ["TONIC", "EMPTY"])

    def test_last_record_is_all_empty(self):
        # Hand-traced from the "; 383: colosseum" block: all four slots EMPTY.
        last = self.records[383]
        self.assertEqual(last.names, ["EMPTY"] * 4)
        self.assertEqual(last.bytes, [0xFF] * 4)

    def test_empty_slot_byte_is_ff(self):
        self.assertEqual(
            self.symbols.item_value("EMPTY", "const.inc", 0), 0xFF)


if __name__ == "__main__":
    unittest.main()
