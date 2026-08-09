#!/usr/bin/env python3
"""Unit tests for parse_formations.py.

Three layers per the parser-test discipline:
  1. pure helpers (composition naming, FormationId derivation incl. repeats /
     UNUSED / dedup suffixes, slot + formation-ref rendering);
  2. synthetic inputs (per-table length + range + corpus-zero-bit guards);
  3. end-to-end against the real original-src battle_monsters.dat +
     battle_prop.dat + cond_battle.dat + const.inc (skipped cleanly when the
     rip output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_formations as pf
from common import ParseError


# --- record builders for synthetic inputs ------------------------------------

def make_formation(slots, positions=None, present=(), vram=0, bg1=0,
                   b14_hi=0):
    """A 15-byte formation record. slots: 6 ids (pf.EMPTY_SLOT for empty).
    positions: 6 packed bytes. present: iterable of present slot indices."""
    positions = positions or [0] * 6
    low = [s & 0xFF for s in slots]
    hi = 0
    for i, s in enumerate(slots):
        hi |= ((s >> 8) & 1) << i
    hi |= (b14_hi & 0x3) << 6
    present_mask = 0
    for i in present:
        present_mask |= 1 << i
    byte0 = ((vram & 0x0F) << 4) | (bg1 & 0x0F)
    byte1 = ((bg1 >> 4) << 6) | (present_mask & 0x3F)
    return bytes([byte0, byte1] + low + positions + [hi])


def make_aux(byte0=0xF0, flags=0, char_ai=0, audio=0):
    return bytes([byte0, flags, char_ai, audio])


MONSTERS = {0: "LOBO", 1: "HORNET", 2: "CRAWLY", 3: "BLEARY"}


# --- Layer 1: pure helpers ---------------------------------------------------

class BaseNameTests(unittest.TestCase):

    def test_single_monster(self):
        rec = make_formation([0] + [pf.EMPTY_SLOT] * 5)
        self.assertEqual(pf._base_name(rec, MONSTERS), "LOBO")

    def test_distinct_monsters_join_in_slot_order(self):
        rec = make_formation([1, 2, 3, pf.EMPTY_SLOT, pf.EMPTY_SLOT,
                              pf.EMPTY_SLOT])
        self.assertEqual(pf._base_name(rec, MONSTERS), "HORNET_CRAWLY_BLEARY")

    def test_consecutive_repeat_collapses_with_count(self):
        rec = make_formation([0, 0, 2, pf.EMPTY_SLOT, pf.EMPTY_SLOT,
                              pf.EMPTY_SLOT])
        self.assertEqual(pf._base_name(rec, MONSTERS), "LOBO_X2_CRAWLY")

    def test_non_consecutive_repeat_uses_first_seen_order_and_total_count(self):
        # HORNET, BLEARY, BLEARY, BLEARY, HORNET -> HORNET_X2_BLEARY_X3.
        rec = make_formation([1, 3, 3, 3, 1, pf.EMPTY_SLOT])
        self.assertEqual(pf._base_name(rec, MONSTERS),
                         "HORNET_X2_BLEARY_X3")

    def test_zero_monster_returns_none(self):
        rec = make_formation([pf.EMPTY_SLOT] * 6)
        self.assertIsNone(pf._base_name(rec, MONSTERS))


class DeriveFormationIdsTests(unittest.TestCase):

    def test_unused_names_zero_monster_formations(self):
        records = [make_formation([pf.EMPTY_SLOT] * 6),
                   make_formation([0] + [pf.EMPTY_SLOT] * 5)]
        names = pf.derive_formation_ids(records, MONSTERS)
        self.assertEqual(names, ["UNUSED_0", "LOBO"])

    def test_duplicate_names_get_index_ordered_suffixes(self):
        lobo = make_formation([0] + [pf.EMPTY_SLOT] * 5)
        records = [lobo, lobo, lobo]
        names = pf.derive_formation_ids(records, MONSTERS)
        self.assertEqual(names, ["LOBO", "LOBO_2", "LOBO_3"])

    def test_all_names_unique(self):
        records = [make_formation([i % 4] + [pf.EMPTY_SLOT] * 5)
                   for i in range(10)]
        names = pf.derive_formation_ids(records, MONSTERS)
        self.assertEqual(len(set(names)), len(names))


class RenderingTests(unittest.TestCase):

    def test_filled_slot_names_monster(self):
        rec = make_formation([2] + [pf.EMPTY_SLOT] * 5,
                             positions=[0x69, 0, 0, 0, 0, 0], present=(0,))
        lit = pf._slot_literal(rec, 0, MONSTERS)
        self.assertEqual(
            lit, "{ .monster = MonsterId::CRAWLY, .x = 6, .y = 9, "
                 ".present = true }")

    def test_empty_slot_renders_braces(self):
        rec = make_formation([pf.EMPTY_SLOT] * 6)
        self.assertEqual(pf._slot_literal(rec, 3, MONSTERS), "{}")

    def test_empty_slot_with_position_renders_faithfully(self):
        rec = make_formation([pf.EMPTY_SLOT] * 6,
                             positions=[0, 0, 0x35, 0, 0, 0])
        self.assertEqual(pf._slot_literal(rec, 2, MONSTERS),
                         "{ .x = 3, .y = 5 }")

    def test_formation_ref_names_formation(self):
        self.assertEqual(pf._ref(0, ["LOBO"]),
                         "FormationRef::of(FormationId::LOBO)")

    def test_formation_ref_randomize_flag(self):
        self.assertEqual(
            pf._ref(0x8000, ["LOBO"]),
            "FormationRef::of(FormationId::LOBO, /*randomizePlus3=*/true)")

    def test_formation_id_header_has_enum_and_values(self):
        header = pf.render_formation_id_h(["LOBO", "HORNET"])
        self.assertIn("enum class FormationId : std::uint16_t {", header)
        self.assertIn("LOBO   = 0,", header)
        self.assertIn("HORNET = 1,", header)


# --- Layer 2: synthetic structural guards ------------------------------------

def _write_dat(data):
    fd, path = tempfile.mkstemp(suffix=".dat")
    with os.fdopen(fd, "wb") as fh:
        fh.write(data)
    return path


class ReadFormationsGuards(unittest.TestCase):

    def _read(self, data):
        path = _write_dat(data)
        try:
            return pf.read_formations(path)
        finally:
            os.remove(path)

    def test_wrong_length_raises(self):
        with self.assertRaises(ParseError):
            self._read(bytes(15 * 575))

    def test_bg1_mask_nonzero_raises(self):
        good = make_formation([pf.EMPTY_SLOT] * 6)
        bad = make_formation([pf.EMPTY_SLOT] * 6, bg1=0x01)
        with self.assertRaises(ParseError) as ctx:
            self._read(bad + good * 575)
        self.assertIn("bg1 mask", str(ctx.exception))

    def test_byte14_high_bits_raise(self):
        bad = make_formation([pf.EMPTY_SLOT] * 6, b14_hi=0x1)
        good = make_formation([pf.EMPTY_SLOT] * 6)
        with self.assertRaises(ParseError):
            self._read(bad + good * 575)

    def test_vram_map_over_12_raises(self):
        bad = make_formation([pf.EMPTY_SLOT] * 6, vram=13)
        good = make_formation([pf.EMPTY_SLOT] * 6)
        with self.assertRaises(ParseError) as ctx:
            self._read(bad + good * 575)
        self.assertIn("VRAM map", str(ctx.exception))

    def test_bad_slot_id_raises(self):
        bad = make_formation([400] + [pf.EMPTY_SLOT] * 5)
        good = make_formation([pf.EMPTY_SLOT] * 6)
        with self.assertRaises(ParseError) as ctx:
            self._read(bad + good * 575)
        self.assertIn("empty sentinel", str(ctx.exception))


class ReadAuxGuards(unittest.TestCase):

    def _read(self, data):
        path = _write_dat(data)
        try:
            return pf.read_aux(path)
        finally:
            os.remove(path)

    def test_unused_byte1_bits_raise(self):
        bad = make_aux(flags=0x01)  # bit 0 unused
        good = make_aux()
        with self.assertRaises(ParseError) as ctx:
            self._read(bad + good * 575)
        self.assertIn("unused bits", str(ctx.exception))

    def test_character_ai_without_enable_bit_raises(self):
        bad = make_aux(flags=0x00, char_ai=5)
        good = make_aux()
        with self.assertRaises(ParseError) as ctx:
            self._read(bad + good * 575)
        self.assertIn("character-AI", str(ctx.exception))

    def test_character_ai_with_enable_bit_is_allowed(self):
        ok = make_aux(flags=0x80, char_ai=5)
        records = pf.read_aux(_path_ok(ok))
        self.assertEqual(records[0][2], 5)


class ReadCondGuards(unittest.TestCase):

    def _read(self, data):
        path = _write_dat(data)
        try:
            return pf.read_cond(path)
        finally:
            os.remove(path)

    def test_wrong_length_raises(self):
        with self.assertRaises(ParseError):
            self._read(bytes(60))

    def test_out_of_range_formation_raises(self):
        data = bytearray(64)
        data[0:2] = (600).to_bytes(2, "little")
        with self.assertRaises(ParseError) as ctx:
            self._read(bytes(data))
        self.assertIn(">=", str(ctx.exception))

    def test_randomize_flag_word_is_accepted(self):
        data = bytearray(64)
        data[0:2] = (0x8000 | 42).to_bytes(2, "little")
        entries = self._read(bytes(data))
        self.assertEqual(entries[0][0], 0x8000 | 42)


def _path_ok(record):
    path = _write_dat(record * 576)
    return path


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle",
                                   "battle_monsters.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src battle_monsters.dat not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        src = os.path.join(root, "src", "battle")
        here = os.path.dirname(os.path.abspath(__file__))
        cls.symbols = pf.Symbols(
            os.path.join(root, "include", "const.inc"),
            os.path.join(here, "..", "..", "include", "ostinato",
                         "monster_id.h"))
        cls.formations = pf.read_formations(
            os.path.join(src, "battle_monsters.dat"))
        cls.aux = pf.read_aux(os.path.join(src, "battle_prop.dat"))
        cls.cond = pf.read_cond(os.path.join(src, "cond_battle.dat"))
        cls.names = pf.derive_formation_ids(cls.formations,
                                            cls.symbols.monster_names)

    def test_corpus_shape(self):
        self.assertEqual(len(self.formations), 576)
        self.assertEqual(len(self.aux), 576)
        self.assertEqual(len(self.cond), 16)
        self.assertEqual(len(self.symbols.monster_names), 384)

    def test_formation_ids_unique_and_complete(self):
        self.assertEqual(len(self.names), 576)
        self.assertEqual(len(set(self.names)), 576)

    def test_spot_check_named_formations(self):
        self.assertEqual(self.names[0], "LOBO")
        self.assertEqual(self.names[471], "SHORT_ARM_LONG_ARM_FACE")
        self.assertEqual(self.names[514], "FINAL_KEFKA")

    def test_unused_formation_count(self):
        unused = [n for n in self.names if n.startswith("UNUSED_")]
        self.assertEqual(len(unused), 14)

    def test_cond_entry_zero_is_undead_behemoth(self):
        trig, repl = self.cond[0]
        self.assertEqual(self.names[trig & 0x7FFF], "SRBEHEMOTH")
        self.assertEqual(self.names[repl & 0x7FFF], "SRBEHEMOTH_UNDEAD")

    def test_unknown_bit40_only_on_two_rows(self):
        carriers = [f for f, r in enumerate(self.aux) if r[3] & 0x40]
        self.assertEqual(carriers, [384, 385])

    def test_monster_id_cross_check_passes(self):
        # Symbols() raising on construction would already fail setUpClass; this
        # asserts the resolved token set matches the shipped enum for a sample.
        self.assertEqual(self.symbols.monster_names[0], "GUARD")


if __name__ == "__main__":
    unittest.main()
