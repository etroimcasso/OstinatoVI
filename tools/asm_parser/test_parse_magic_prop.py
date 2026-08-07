#!/usr/bin/env python3
"""Unit tests for parse_magic_prop.py.

Three layers per the parser-test discipline:
  1. pure helpers (byte decompositions, renderer formatting);
  2. synthetic inputs (length guard, undocumented-bit / residue errors);
  3. end-to-end against the real original-src magic_prop_en.dat + const.inc
     (skipped cleanly when the rip output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_magic_prop as pmp


# The TARGET bit values (const.inc .enum TARGET) — hardcoded here so the
# helper layers run without the contract clone; the e2e layer verifies the
# real const.inc agrees.
TARGET = {
    "MANUAL": 0x01, "ONE_SIDE": 0x02,
    "INIT_MASK": 0x0C, "INIT_SINGLE": 0x00, "INIT_ALL": 0x04,
    "INIT_GROUP": 0x08, "INIT_HALF": 0x0C,
    "AUTO_CONFIRM": 0x10, "MULTI_TARGET": 0x20, "ENEMY": 0x40,
    "ROULETTE": 0x80, "SELF": 0x02, "MENU": 0xFF,
}


# --- Layer 1: pure helpers ---------------------------------------------------

class TargetingDecompositionTests(unittest.TestCase):

    def test_zero_is_empty(self):
        self.assertEqual(pmp.decompose_targeting(0x00, TARGET, 0), [])

    def test_menu_sentinel_alone(self):
        self.assertEqual(pmp.decompose_targeting(0xFF, TARGET, 0), ["MENU"])

    def test_plain_bits_ascending(self):
        self.assertEqual(pmp.decompose_targeting(0x61, TARGET, 0),
                         ["MANUAL", "MULTI_TARGET", "ENEMY"])

    def test_init_subfield_first_then_remaining(self):
        # 0x7A = INIT_GROUP + ONE_SIDE + AUTO_CONFIRM + MULTI_TARGET + ENEMY.
        self.assertEqual(
            pmp.decompose_targeting(0x7A, TARGET, 0),
            ["INIT_GROUP", "ONE_SIDE", "AUTO_CONFIRM", "MULTI_TARGET",
             "ENEMY"])

    def test_init_all_variants(self):
        self.assertEqual(pmp.decompose_targeting(0x04, TARGET, 0),
                         ["INIT_ALL"])
        self.assertEqual(pmp.decompose_targeting(0x08, TARGET, 0),
                         ["INIT_GROUP"])
        self.assertEqual(pmp.decompose_targeting(0x0C, TARGET, 0),
                         ["INIT_HALF"])

    def test_duplicate_value_prefers_one_side(self):
        # $02 is declared as ONE_SIDE first, SELF as a trailing alias.
        self.assertEqual(pmp.decompose_targeting(0x02, TARGET, 0),
                         ["ONE_SIDE"])


class FlagDecompositionTests(unittest.TestCase):

    def test_known_bits(self):
        self.assertEqual(
            pmp._decompose_bits(0x28, pmp._FLAG1_BITS, "flags1", 0),
            ["ENABLE_RUNIC", "RETARGET_IF_INVALID"])
        self.assertEqual(
            pmp._decompose_bits(0x00, pmp._TRAIT_BITS, "trait", 0), [])
        self.assertEqual(
            pmp._decompose_bits(0xFF, pmp._TRAIT_BITS, "trait", 0),
            [name for _, name in pmp._TRAIT_BITS])

    def test_undocumented_misc_bit_raises(self):
        # Only bits 0-1 of the misc byte are documented; bit 2 is an error.
        with self.assertRaises(pmp.MagicPropError):
            pmp._decompose_bits(0x04, pmp._MISC_BITS, "misc", 0)


class StatusDecompositionTests(unittest.TestCase):

    STATUS_NAMES = {0: "BLIND", 2: "POISON", 16: "DANCE", 31: "FLOAT"}

    def test_ids_span_the_four_bytes(self):
        # byte0 bit0 = id 0, byte0 bit2 = id 2, byte2 bit0 = id 16,
        # byte3 bit7 = id 31.
        self.assertEqual(
            pmp.decompose_statuses([0x05, 0x00, 0x01, 0x80],
                                   self.STATUS_NAMES, 0),
            ["BLIND", "POISON", "DANCE", "FLOAT"])

    def test_all_zero_is_empty(self):
        self.assertEqual(
            pmp.decompose_statuses([0, 0, 0, 0], self.STATUS_NAMES, 0), [])

    def test_unnamed_status_id_raises(self):
        with self.assertRaises(pmp.MagicPropError):
            pmp.decompose_statuses([0x02, 0, 0, 0], {}, 0)


class SpecialEffectDecompositionTests(unittest.TestCase):

    def test_known_bytes_map_to_enumerators(self):
        self.assertEqual(pmp.decompose_special_effect(0x35, 0), "DOOM")
        self.assertEqual(pmp.decompose_special_effect(0x11, 0), "GOLEM")
        self.assertEqual(pmp.decompose_special_effect(0x25, 0),
                         "MISSES_FLOATING_TARGETS")

    def test_ff_is_the_none_sentinel(self):
        self.assertEqual(pmp.decompose_special_effect(0xFF, 0), "NONE")

    def test_unmapped_byte_raises(self):
        # $14 is a corpus gap inside the band; $41/$42 have handlers but no
        # corpus carrier (deliberately unmapped); $46/$80 sit outside the
        # attack band entirely.
        for byte in (0x14, 0x41, 0x42, 0x46, 0x80):
            with self.assertRaises(pmp.MagicPropError):
                pmp.decompose_special_effect(byte, 0)


class RendererFormattingTests(unittest.TestCase):

    def test_of_or_empty(self):
        self.assertEqual(pmp._of_or_empty("ElementSet", "Element", []),
                         "ElementSet{}")
        self.assertEqual(
            pmp._of_or_empty("ElementSet", "Element", ["FIRE", "ICE"]),
            "ElementSet::of(Element::FIRE, Element::ICE)")


# --- Layer 2: synthetic inputs -----------------------------------------------

class ReadRecordsTests(unittest.TestCase):

    def _write_dat(self, data):
        fd, path = tempfile.mkstemp(suffix=".dat")
        with os.fdopen(fd, "wb") as fh:
            fh.write(bytes(data))
        return path

    def test_wrong_length_raises(self):
        # Length is checked before any record is decomposed, so no symbol
        # table is needed.
        for length in (0, 14, pmp.EXPECTED_LEN - 1, pmp.EXPECTED_LEN + 1):
            path = self._write_dat([0] * length)
            try:
                with self.assertRaises(pmp.MagicPropError):
                    pmp.read_records(path, None)
            finally:
                os.remove(path)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle",
                                   "magic_prop_en.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src magic_prop_en.dat not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = pmp.Symbols(os.path.join(root, "include", "const.inc"))
        cls.records = pmp.read_records(
            os.path.join(root, "src", "battle", "magic_prop_en.dat"),
            cls.symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.records), 256)
        for rec in self.records:
            self.assertEqual(len(rec.raw), 14)

    def test_const_inc_target_values_match_helper_table(self):
        # The hardcoded TARGET map the helper layers use must agree with the
        # real const.inc — this is the drift check between the two layers.
        for name, value in TARGET.items():
            self.assertEqual(self.symbols.target[name], value,
                             "TARGET::{}".format(name))

    def test_fire_boundary_record(self):
        # $00 FIRE: 61 01 00 28 00 04 15 00 96 FF 00*4 (hand-traced).
        fire = self.records[0x00]
        self.assertEqual(fire.name, "FIRE")
        self.assertEqual(fire.raw, [0x61, 0x01, 0x00, 0x28, 0x00, 0x04, 0x15,
                                    0x00, 0x96, 0xFF, 0x00, 0x00, 0x00, 0x00])
        self.assertEqual(fire.targeting, ["MANUAL", "MULTI_TARGET", "ENEMY"])
        self.assertEqual(fire.elements, ["FIRE"])
        self.assertEqual(fire.flags1, ["ENABLE_RUNIC", "RETARGET_IF_INVALID"])
        self.assertEqual(fire.mp_cost, 4)
        self.assertEqual(fire.power, 21)
        self.assertEqual(fire.hit_rate, 150)
        self.assertEqual(fire.special_effect, 0xFF)
        self.assertEqual(fire.statuses, [])

    def test_last_record_is_none_sentinel_row(self):
        # $FF NONE: all-zero except specialEffect $FF (hand-traced).
        last = self.records[0xFF]
        self.assertEqual(last.name, "NONE")
        self.assertEqual(last.raw[9], 0xFF)
        self.assertEqual([b for i, b in enumerate(last.raw) if i != 9],
                         [0x00] * 13)

    def test_poison_status_decomposition(self):
        # $03 POISON: trait INVERT_ON_UNDEAD, status byte 1 = 0x04 -> POISON.
        poison = self.records[0x03]
        self.assertEqual(poison.traits, ["INVERT_ON_UNDEAD"])
        self.assertEqual(poison.statuses, ["POISON"])

    def test_special_effect_corpus_shape(self):
        # 66 of 256 records carry a non-$FF special-effect byte, spanning 52
        # distinct values — the full EN corpus of the dispatch band.
        non_none = [r.special_effect for r in self.records
                    if r.special_effect != 0xFF]
        self.assertEqual(len(non_none), 66)
        self.assertEqual(len(set(non_none)), 52)

    def test_special_effect_rows_render_named(self):
        # Every emitted specialEffect line renders through the
        # AttackSpecialEffect surface — no raw-hex byte remains.
        inc = pmp.render_inc(self.records)
        self.assertNotRegex(inc, r"\.specialEffect = 0x")
        self.assertIn(".specialEffect = AttackSpecialEffect::NONE,", inc)
        self.assertIn(".specialEffect = AttackSpecialEffect::DOOM,", inc)


if __name__ == "__main__":
    unittest.main()
