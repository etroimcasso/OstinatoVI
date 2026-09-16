#!/usr/bin/env python3
"""Unit tests for parse_char_ai.py.

Three layers, matching the parser test discipline: the expression evaluator
against hand-made terms, synthetic assembly fragments exercising the grammar's
edges and its error paths, and an end-to-end pass over the real disassembly
(skipped when it is not present) that pins the corpus's own counts and quirks.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_char_ai as pca  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "btlgfx", "char_ai.asm")):
        return candidate
    return None


class _FakeEnum(object):
    def __init__(self, pairs):
        self._pairs = list(pairs)

    @property
    def members(self):
        return [type("M", (), {"name": n, "value": v})() for n, v in self._pairs]

    def value_of(self, name):
        for n, v in self._pairs:
            if n == name:
                return v
        return None


class _FakeSymbols(object):
    """Stands in for the four real include files."""

    def __init__(self):
        self.enums = {
            "CHAR_AI": _FakeEnum([("NONE", 0), ("FIGHT", 1)]),
            "BATTLE_BG": _FakeEnum([("FIELD", 0), ("DEFAULT", 0xFF)]),
            "SONG": _FakeEnum([("NONE", 0xFF), ("BOSS", 3)]),
            "CHAR_PROP": _FakeEnum([("TERRA", 0), ("SHADOW", 3)]),
            "CHAR_GFX": _FakeEnum([("TERRA", 0), ("SHADOW", 3)]),
            "MONSTER": _FakeEnum([("GUARD", 0x0102)]),
        }
        self.globals = {
            "CHAR_AI_FLAG_NONE": 0,
            "CHAR_AI_FLAG_HIDE_NAMES": 0x01,
            "CHAR_AI_FLAG_HIDE_PARTY": 0x80,
            "CHAR_AI_FLAG_NOT_IN_PARTY": 0x80,
            "CHAR_AI_FLAG_ENEMY_CHAR": 0x40,
        }

    enum_value = pca.Symbols.enum_value
    member_named = pca.Symbols.member_named
    names_by_index = pca.Symbols.names_by_index


class ExpressionTest(unittest.TestCase):
    """One `.byte` term at a time."""

    def setUp(self):
        self.sym = _FakeSymbols()

    def ev(self, text):
        return pca.evaluate(text, self.sym, "x.asm", 1)

    def test_literals(self):
        self.assertEqual(self.ev("$ff"), 0xFF)
        self.assertEqual(self.ev("40"), 40)
        self.assertEqual(self.ev("0"), 0)

    def test_bare_constant(self):
        self.assertEqual(self.ev("CHAR_AI_FLAG_HIDE_PARTY"), 0x80)

    def test_scoped_enum(self):
        self.assertEqual(self.ev("BATTLE_BG::DEFAULT"), 0xFF)
        self.assertEqual(self.ev("SONG::BOSS"), 3)

    def test_or_of_terms(self):
        self.assertEqual(
            self.ev("CHAR_PROP::SHADOW|CHAR_AI_FLAG_ENEMY_CHAR"), 0x43)
        self.assertEqual(
            self.ev("CHAR_AI_FLAG_HIDE_NAMES|CHAR_AI_FLAG_HIDE_PARTY"), 0x81)

    def test_low_byte_of_complement(self):
        # <~0 is how the corpus writes "every monster is a valid target".
        self.assertEqual(self.ev("<~0"), 0xFF)

    def test_low_byte_of_a_word_enum(self):
        self.assertEqual(self.ev("<MONSTER::GUARD"), 0x02)

    def test_high_byte(self):
        self.assertEqual(self.ev(">MONSTER::GUARD"), 0x01)

    def test_unknown_symbol_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.ev("NOT_A_THING")
        self.assertIn("grammar not covered", str(ctx.exception))

    def test_unknown_member_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.ev("SONG::NOT_A_SONG")
        self.assertIn("unknown SONG", str(ctx.exception))

    def test_unknown_scope_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.ev("WEATHER::RAIN")
        self.assertIn("not an enum this table resolves against",
                      str(ctx.exception))

    def test_a_word_sized_result_without_a_byte_operator_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.ev("MONSTER::GUARD")
        self.assertIn("outside a byte", str(ctx.exception))

    def test_operator_without_operand_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.ev("<")
        self.assertIn("no operand", str(ctx.exception))


def _record(header, slots):
    out = list(header)
    for slot in slots:
        out.extend(slot)
    return out


class SlotShapeTest(unittest.TestCase):
    """Which byte patterns count as an unused slot."""

    def test_both_encodings_are_accepted(self):
        self.assertIn(pca.EMPTY_SLOT_SENTINEL, pca.EMPTY_SLOTS)
        self.assertIn(pca.EMPTY_SLOT_ZEROED, pca.EMPTY_SLOTS)

    def test_each_encoding_picks_its_own_builder(self):
        self.assertEqual(pca.Slot(pca.EMPTY_SLOT_SENTINEL).builder, "unused")
        self.assertEqual(pca.Slot(pca.EMPTY_SLOT_ZEROED).builder, "unusedZeroed")

    def test_a_populated_slot_is_not_empty(self):
        self.assertFalse(pca.Slot([0x43, 0x03, 0x7E, 40, 48]).empty)

    def test_a_third_sentinel_shape_is_rejected(self):
        # A slot that opens with $FF but fills differently is a grammar
        # deviation, not a new kind of empty.
        record = pca.Record(0, _record([0, 0xFF, 0xFF, 0xFF],
                                       [[0xFF, 0x11, 0x22, 0xFF, 0xFF]] * 4))
        record.name = "NONE"
        symbols = _FakeSymbols()
        symbols.enums["CHAR_AI"] = _FakeEnum([("NONE", 0)])
        with self.assertRaises(ParseError) as ctx:
            pca._verify([record], symbols, "x.asm")
        self.assertIn("opens with the $FF sentinel", str(ctx.exception))


class RomAssertTest(unittest.TestCase):
    """The cartridge comparison reports the first disagreeing byte."""

    def setUp(self):
        raw = _record([0, 0xFF, 0xFF, 0xFF], [list(pca.EMPTY_SLOT_SENTINEL)] * 4)
        self.records = [pca.Record(0, raw)]
        self.records[0].name = "NONE"

    def _rom_with(self, payload):
        base = common.hirom_file_offset(pca.CHAR_AI_SNES)
        rom = bytearray(base + len(payload))
        rom[base:base + len(payload)] = payload
        return bytes(rom)

    def test_identical_bytes_pass(self):
        payload = bytes(self.records[0].bytes)
        pca.assert_rom(self._rom_with(payload), self.records, "x.asm")

    def test_a_differing_byte_is_reported(self):
        payload = bytearray(self.records[0].bytes)
        payload[3] ^= 0xFF
        with self.assertRaises(ParseError) as ctx:
            pca.assert_rom(self._rom_with(payload), self.records, "x.asm")
        self.assertIn("ROM MISMATCH", str(ctx.exception))

    def test_a_short_cartridge_is_reported(self):
        with self.assertRaises(ParseError) as ctx:
            pca.assert_rom(b"\x00" * 4, self.records, "x.asm")
        self.assertIn("ROM MISMATCH", str(ctx.exception))


@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):
    """The real table: its shape, its counts and its two empty-slot forms."""

    @classmethod
    def setUpClass(cls):
        root = _source_root()
        cls.symbols = pca.Symbols(root)
        cls.records = pca.parse_char_ai(
            os.path.join(root, "src", "btlgfx", "char_ai.asm"), cls.symbols)

    def test_the_corpus_is_twenty_four_records_of_twenty_four_bytes(self):
        self.assertEqual(len(self.records), 24)
        for record in self.records:
            self.assertEqual(len(record.bytes), pca.RECORD_BYTES)
            self.assertEqual(len(record.slots), pca.SLOT_COUNT)

    def test_every_record_is_named_by_its_position(self):
        for index, record in enumerate(self.records):
            self.assertEqual(record.index, index)
            self.assertIsNotNone(record.name)
        self.assertEqual(self.records[0].name, "NONE")
        self.assertEqual(self.records[1].name, "SHADOW_COLOSSEUM")

    def test_the_populated_slot_count_is_pinned(self):
        populated = sum(1 for r in self.records for s in r.slots if not s.empty)
        self.assertEqual(populated, 30)

    def test_both_unused_encodings_occur(self):
        shapes = {}
        for record in self.records:
            for slot in record.slots:
                if slot.empty:
                    shapes[tuple(slot.bytes)] = shapes.get(
                        tuple(slot.bytes), 0) + 1
        self.assertEqual(shapes.get(pca.EMPTY_SLOT_SENTINEL), 46)
        self.assertEqual(shapes.get(pca.EMPTY_SLOT_ZEROED), 20)

    def test_shadow_colosseum_is_hand_traced(self):
        record = self.records[1]
        self.assertEqual(record.bytes[:4], [0x00, 0xFF, 0xFF, 0x07])
        self.assertEqual(record.slots[0].bytes, [0x43, 0x03, 0x7E, 40, 48])
        for slot in record.slots[1:]:
            self.assertTrue(slot.empty)


@unittest.skipIf(_source_root() is None, "disassembly not present")
class CartridgeTest(unittest.TestCase):
    """The assembled table against the vanilla cartridge, end to end."""

    def test_the_whole_table_matches_the_cartridge(self):
        root = _source_root()
        symbols = pca.Symbols(root)
        asm = os.path.join(root, "src", "btlgfx", "char_ai.asm")
        records = pca.parse_char_ai(asm, symbols)
        pca.assert_rom(common.load_vanilla_rom(root), records, asm)


if __name__ == "__main__":
    unittest.main()
