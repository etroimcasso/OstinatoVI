#!/usr/bin/env python3
"""Unit tests for parse_monster_prop.py.

Three layers per the parser-test discipline:
  1. pure helpers (byte decompositions, renderer formatting);
  2. synthetic inputs (length guard, out-of-range / unnamed-id errors);
  3. end-to-end against the real original-src monster_prop.dat + const.inc
     (skipped cleanly when the rip output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_monster_prop as pmp


# --- Layer 1: pure helpers ---------------------------------------------------

class MetamorphDecompositionTests(unittest.TestCase):

    def test_zero_byte_is_pack_zero_rate_zero(self):
        self.assertEqual(pmp.decompose_metamorph(0x00, 0), (0, 0))

    def test_low_five_bits_are_the_pack(self):
        self.assertEqual(pmp.decompose_metamorph(0x1F, 0), (31, 0))

    def test_high_three_bits_are_the_rate(self):
        self.assertEqual(pmp.decompose_metamorph(0xE0, 0), (0, 7))

    def test_mixed_byte_splits_both_ways(self):
        # $6C = rate 3 (011), pack 12 (01100).
        self.assertEqual(pmp.decompose_metamorph(0x6C, 0), (12, 3))


class SpecialAttackDecompositionTests(unittest.TestCase):

    def test_plain_class_byte(self):
        self.assertEqual(pmp.decompose_special_attack(0x20, 0),
                         (0x20, False, False))

    def test_bit7_is_cant_dodge(self):
        self.assertEqual(pmp.decompose_special_attack(0x85, 0),
                         (0x05, True, False))

    def test_bit6_is_no_damage(self):
        self.assertEqual(pmp.decompose_special_attack(0x45, 0),
                         (0x05, False, True))

    def test_both_modifier_bits(self):
        self.assertEqual(pmp.decompose_special_attack(0xFF, 0),
                         (0x3F, True, True))


class FlagDecompositionTests(unittest.TestCase):

    def test_trait_bits(self):
        self.assertEqual(
            pmp._decompose_bits(0x10, pmp._TRAIT_BITS, "trait", 0),
            ["HUMAN"])
        self.assertEqual(
            pmp._decompose_bits(0x91, pmp._TRAIT_BITS, "trait", 0),
            ["DIES_AT_ZERO_MP", "HUMAN", "UNDEAD"])
        self.assertEqual(
            pmp._decompose_bits(0x00, pmp._TRAIT_BITS, "trait", 0), [])

    def test_unused_trait_bits_have_names(self):
        # Bits 1/3/5 are unused in the layout but keep UNUSED_n enumerators.
        self.assertEqual(
            pmp._decompose_bits(0x2A, pmp._TRAIT_BITS, "trait", 0),
            ["UNUSED_1", "UNUSED_3", "UNUSED_5"])

    def test_battle_bits(self):
        self.assertEqual(
            pmp._decompose_bits(0xFF, pmp._BATTLE_BITS, "battle-flag", 0),
            [name for _, name in pmp._BATTLE_BITS])
        self.assertEqual(
            pmp._decompose_bits(0x82, pmp._BATTLE_BITS, "battle-flag", 0),
            ["FIRST_STRIKE", "CANT_CONTROL"])


class StatusDecompositionTests(unittest.TestCase):

    STATUS_NAMES = {0: "BLIND", 2: "POISON", 7: "DEAD", 16: "DANCE",
                    23: "REFLECT", 31: "FLOAT"}

    def test_blocked_ids_span_three_bytes(self):
        # byte0 bit0 = id 0, byte1 bit? none, byte2 bit7 = id 23.
        self.assertEqual(
            pmp.decompose_statuses([0x01, 0x00, 0x80], self.STATUS_NAMES,
                                   0, 23, "blocked", 0),
            ["BLIND", "REFLECT"])

    def test_innate_ids_span_four_bytes(self):
        self.assertEqual(
            pmp.decompose_statuses([0x04, 0x00, 0x01, 0x80],
                                   self.STATUS_NAMES, 0, 31, "innate", 0),
            ["POISON", "DANCE", "FLOAT"])

    def test_all_zero_is_empty(self):
        self.assertEqual(
            pmp.decompose_statuses([0, 0, 0], self.STATUS_NAMES, 0, 23,
                                   "blocked", 0), [])

    def test_id_beyond_max_raises(self):
        # A fourth blocked byte cannot exist; ids over the max hard-error.
        with self.assertRaises(pmp.MonsterPropError):
            pmp.decompose_statuses([0, 0, 0x80], self.STATUS_NAMES, 0, 22,
                                   "blocked", 0)

    def test_unnamed_status_id_raises(self):
        with self.assertRaises(pmp.MonsterPropError):
            pmp.decompose_statuses([0x02, 0, 0], {}, 0, 23, "blocked", 0)


class ElementDecompositionTests(unittest.TestCase):

    ELEMENTS = {0x01: "FIRE", 0x08: "POISON", 0x80: "WATER"}

    def test_bits_map_to_names_ascending(self):
        self.assertEqual(
            pmp.decompose_elements(0x89, self.ELEMENTS, "weak", 0),
            ["FIRE", "POISON", "WATER"])

    def test_zero_is_empty(self):
        self.assertEqual(pmp.decompose_elements(0x00, self.ELEMENTS,
                                                "weak", 0), [])

    def test_unnamed_bit_raises(self):
        with self.assertRaises(pmp.MonsterPropError):
            pmp.decompose_elements(0x02, self.ELEMENTS, "weak", 0)


class RendererFormattingTests(unittest.TestCase):

    def test_of_or_empty(self):
        self.assertEqual(pmp._of_or_empty("ElementSet", "Element", []),
                         "ElementSet{}")
        self.assertEqual(
            pmp._of_or_empty("MonsterTraitFlags", "MonsterTraitFlag",
                             ["HUMAN", "UNDEAD"]),
            "MonsterTraitFlags::of(MonsterTraitFlag::HUMAN, "
            "MonsterTraitFlag::UNDEAD)")

    def test_special_attack_render_forms(self):
        class Rec(object):
            pass
        rec = Rec()
        rec.special_builder = "MonsterSpecialAttack::damageBoost(0)"
        rec.special_cant_dodge = False
        rec.special_no_damage = False
        self.assertEqual(pmp._render_special_attack(rec),
                         "MonsterSpecialAttack::damageBoost(0)")
        rec.special_cant_dodge = True
        self.assertEqual(
            pmp._render_special_attack(rec),
            "MonsterSpecialAttack::damageBoost(0).withCantDodge()")
        rec.special_no_damage = True
        self.assertEqual(
            pmp._render_special_attack(rec),
            "MonsterSpecialAttack::damageBoost(0).withCantDodge()"
            ".withNoDamage()")


class SpecialAttackBuilderTests(unittest.TestCase):
    """The per-band builder selection follows the dispatch's decode order
    (battle_main.asm:8225-8235)."""

    STATUS_NAMES = {0x08: "CONDEMNED", 0x0F: "SLEEP"}

    def test_status_band_uses_the_status_name(self):
        self.assertEqual(
            pmp.special_attack_builder(0x08, self.STATUS_NAMES, 0),
            "MonsterSpecialAttack::inflictStatus(StatusId::CONDEMNED)")

    def test_status_band_unnamed_id_raises(self):
        with self.assertRaises(pmp.MonsterPropError):
            pmp.special_attack_builder(0x01, self.STATUS_NAMES, 0)

    def test_damage_boost_band(self):
        self.assertEqual(pmp.special_attack_builder(0x20, {}, 0),
                         "MonsterSpecialAttack::damageBoost(0)")
        self.assertEqual(pmp.special_attack_builder(0x2D, {}, 0),
                         "MonsterSpecialAttack::damageBoost(13)")

    def test_drain_bytes(self):
        self.assertEqual(pmp.special_attack_builder(0x30, {}, 0),
                         "MonsterSpecialAttack::drainHp()")
        self.assertEqual(pmp.special_attack_builder(0x31, {}, 0),
                         "MonsterSpecialAttack::drainMp()")

    def test_remove_reflect_band(self):
        self.assertEqual(pmp.special_attack_builder(0x32, {}, 0),
                         "MonsterSpecialAttack::removeReflect()")
        self.assertEqual(
            pmp.special_attack_builder(0x3F, {}, 0),
            "MonsterSpecialAttack::removeReflect(/*deadResidualBits=*/13)")


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
        for length in (0, 32, pmp.EXPECTED_LEN - 1, pmp.EXPECTED_LEN + 1):
            path = self._write_dat([0] * length)
            try:
                with self.assertRaises(pmp.MonsterPropError):
                    pmp.read_records(path, None)
            finally:
                os.remove(path)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle",
                                   "monster_prop.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src monster_prop.dat not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = pmp.Symbols(os.path.join(root, "include", "const.inc"))
        cls.records = pmp.read_records(
            os.path.join(root, "src", "battle", "monster_prop.dat"),
            cls.symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.records), 384)
        for rec in self.records:
            self.assertEqual(len(rec.raw), 32)

    def test_guard_boundary_record(self):
        # $000 GUARD, hand-traced from the .dat's first 32 bytes:
        # 1E 10 64 00 00 64 8C 06 | 28 00 0F 00 30 00 30 00 |
        # 05 00 10 00 | 00 00 00 | 00 00 08 | 00 | 00 00 00 00 | 20.
        guard = self.records[0x000]
        self.assertEqual(guard.name, "GUARD")
        self.assertEqual(guard.raw, [
            0x1E, 0x10, 0x64, 0x00, 0x00, 0x64, 0x8C, 0x06,
            0x28, 0x00, 0x0F, 0x00, 0x30, 0x00, 0x30, 0x00,
            0x05, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20])
        self.assertEqual(guard.speed, 30)
        self.assertEqual(guard.attack_power, 16)
        self.assertEqual(guard.hit_rate, 100)
        self.assertEqual(guard.defense, 100)
        self.assertEqual(guard.magic_defense, 140)
        self.assertEqual(guard.magic_power, 6)
        self.assertEqual(guard.hp, 40)
        self.assertEqual(guard.mp, 15)
        self.assertEqual(guard.experience, 48)
        self.assertEqual(guard.gold, 48)
        self.assertEqual(guard.level, 5)
        self.assertEqual((guard.metamorph_pack, guard.metamorph_rate), (0, 0))
        self.assertEqual(guard.traits, ["HUMAN"])
        self.assertEqual(guard.battle_flags, [])
        self.assertEqual(guard.blocked_statuses, [])
        self.assertEqual(guard.weak_elements, ["POISON"])
        self.assertEqual(guard.attack_graphic_name, "DIRK")
        self.assertEqual(guard.innate_statuses, [])
        self.assertEqual(
            (guard.special_effect_class, guard.special_cant_dodge,
             guard.special_no_damage),
            (0x20, False, False))

    def test_last_record_raw_bytes(self):
        # $17F, hand-traced from the .dat's final 32 bytes.
        last = self.records[0x17F]
        self.assertEqual(last.raw, [
            0x1E, 0x0D, 0x64, 0x00, 0x00, 0x66, 0x99, 0x0A,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0xE0, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
            0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x20])
        # metamorph $E0 = rate row 7 (the 0/256 probability), pack 0.
        self.assertEqual((last.metamorph_pack, last.metamorph_rate), (0, 7))
        # battle-flag byte $FF carries all eight named bits.
        self.assertEqual(last.battle_flags,
                         [name for _, name in pmp._BATTLE_BITS])

    def test_monster_name_space_is_full(self):
        # Every index 0..383 resolved a MONSTER name (placeholder slots
        # included) — Record construction would have raised otherwise.
        self.assertEqual(len({rec.name for rec in self.records}), 384)

    def test_rows_render_through_named_surfaces(self):
        inc = pmp.render_inc(self.records)
        # Flag/status/element fields never render as raw bytes.
        self.assertNotRegex(inc, r"\.traitFlags      = 0x")
        self.assertNotRegex(inc, r"\.battleFlags     = 0x")
        self.assertNotRegex(inc, r"\.attackGraphic   = 0x")
        self.assertNotRegex(inc, r"\.specialAttack   = 0x")
        self.assertNotRegex(inc, r"MonsterSpecialAttack::of\(")
        self.assertIn(".traitFlags      = MonsterTraitFlags::of("
                      "MonsterTraitFlag::HUMAN),", inc)
        self.assertIn(".attackGraphic   = ItemId::DIRK,", inc)
        self.assertIn(".specialAttack   = MonsterSpecialAttack::"
                      "damageBoost(0),", inc)
        self.assertIn(".metamorph       = MetamorphInfo::of({ .packIndex = 0,"
                      " .rate = MetamorphRate::ODDS_255_256 }),", inc)

    def test_fixture_rows_are_raw_bytes(self):
        fixture = pmp.render_fixture(self.records)
        self.assertIn("{ .id =   0,  // $000 GUARD", fixture)
        self.assertIn(".speed = 0x1E, .attackPower = 0x10, .hitRate = 0x64,",
                      fixture)
        self.assertIn("std::array<ExpectedMonsterEntry, 384>", fixture)


if __name__ == "__main__":
    unittest.main()
