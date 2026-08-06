#!/usr/bin/env python3
"""Unit tests for parse_item_prop.py.

Three layers per the parser-test discipline:
  1. pure helpers (role-shaped byte decompositions, renderer formatting);
  2. synthetic inputs (length guard, undocumented-bit / residue errors);
  3. end-to-end against the real original-src item_prop_en.dat + const.inc
     (skipped cleanly when the rip output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_item_prop as pip


# Minimal symbol values (const.inc enums) — hardcoded here so the helper
# layers run without the contract clone; the e2e layer verifies the real
# const.inc agrees.
ITEM_TYPES = {0: "TOOL", 1: "WEAPON", 2: "ARMOR", 3: "SHIELD", 4: "HELMET",
              5: "RELIC", 6: "CONSUMABLE"}
ITEM_USAGE = ((0x10, "THROW"), (0x20, "BATTLE"), (0x40, "MENU"))
WEAPON_FLAGS = ((0x02, "SWDTECH"), (0x20, "BACK_ROW"), (0x40, "TWO_HAND"),
                (0x80, "RUNIC"))
CHARS = {0: "TERRA", 1: "LOCKE", 2: "CYAN", 3: "SHADOW", 4: "EDGAR",
         5: "SABIN", 6: "CELES", 7: "STRAGO", 8: "RELM", 9: "SETZER",
         10: "MOG", 11: "GAU", 12: "GOGO", 13: "UMARO"}


class _FakeSymbols(object):
    """Just the attributes the helper decompositions consume."""

    type_names = ITEM_TYPES
    usage_bits = ITEM_USAGE
    weapon_flag_bits = WEAPON_FLAGS
    char_names = CHARS
    attack_names = {0x00: "FIRE", 0x05: "FIRE_2", 0x09: "FIRE_3"}
    status_names = {0: "BLIND", 7: "DEAD", 8: "CONDEMNED", 16: "DANCE"}


SYM = _FakeSymbols()


# --- Layer 1: pure helpers ---------------------------------------------------

class TypeUsageDecompositionTests(unittest.TestCase):

    def test_type_and_usage_flags(self):
        self.assertEqual(pip.decompose_type_usage(0x11, SYM, 0),
                         ("WEAPON", ["THROW"]))
        self.assertEqual(pip.decompose_type_usage(0x66, SYM, 0),
                         ("CONSUMABLE", ["BATTLE", "MENU"]))
        self.assertEqual(pip.decompose_type_usage(0x05, SYM, 0),
                         ("RELIC", []))

    def test_unused_bit_raises(self):
        # Bits 3 and 7 are unused across the corpus.
        for byte in (0x09, 0x81):
            with self.assertRaises(pip.ItemPropError):
                pip.decompose_type_usage(byte, SYM, 0)

    def test_unknown_type_raises(self):
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_type_usage(0x07, SYM, 0)


class EquipMaskDecompositionTests(unittest.TestCase):

    def test_slots_then_special_bits(self):
        self.assertEqual(
            pip.decompose_equip_mask(0x8410, SYM, 0),
            [("CharacterId", "EDGAR"), ("CharacterId", "MOG"),
             ("EquipSpecial", "HEAVY")])
        self.assertEqual(pip.decompose_equip_mask(0x4000, SYM, 0),
                         [("EquipSpecial", "IMP")])
        self.assertEqual(pip.decompose_equip_mask(0x0000, SYM, 0), [])

    def test_unnamed_slot_raises(self):
        class Empty(_FakeSymbols):
            char_names = {}
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_equip_mask(0x0001, Empty(), 0)


class StatPairDecompositionTests(unittest.TestCase):

    def test_signed_nibbles(self):
        self.assertEqual(pip.decompose_stat_pair(0x22, "t", 0), (2, 2))
        self.assertEqual(pip.decompose_stat_pair(0x3F, "t", 0), (-7, 3))
        self.assertEqual(pip.decompose_stat_pair(0x00, "t", 0), (0, 0))
        self.assertEqual(pip.decompose_stat_pair(0x9A, "t", 0), (-2, -1))

    def test_negative_zero_nibble_raises(self):
        for byte in (0x08, 0x80):
            with self.assertRaises(pip.ItemPropError):
                pip.decompose_stat_pair(byte, "t", 0)


class StatusSliceDecompositionTests(unittest.TestCase):

    def test_slice_offsets(self):
        self.assertEqual(pip.decompose_status_slice(0x81, 0, SYM, 0),
                         ["BLIND", "DEAD"])
        self.assertEqual(pip.decompose_status_slice(0x01, 1, SYM, 0),
                         ["CONDEMNED"])
        self.assertEqual(pip.decompose_status_slice(0x01, 2, SYM, 0),
                         ["DANCE"])
        self.assertEqual(pip.decompose_status_slice(0x00, 0, SYM, 0), [])

    def test_unnamed_status_raises(self):
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_status_slice(0x02, 0, SYM, 0)


class ItemFlagsDecompositionTests(unittest.TestCase):

    def test_weapon_role(self):
        self.assertEqual(pip.decompose_item_flags(0xC2, 1, SYM, 0),
                         ("weapon", ["SWDTECH", "TWO_HAND", "RUNIC"]))
        self.assertEqual(pip.decompose_item_flags(0x00, 1, SYM, 0),
                         ("empty",))

    def test_weapon_residue_raises(self):
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_item_flags(0x0A, 1, SYM, 0)

    def test_dead_bits(self):
        self.assertEqual(pip.decompose_item_flags(0x01, 5, SYM, 0),
                         ("dead", "kDeadItemFlagBit0"))
        self.assertEqual(pip.decompose_item_flags(0x40, 6, SYM, 0),
                         ("dead", "kDeadItemFlagBit6"))

    def test_item_use_role(self):
        self.assertEqual(
            pip.decompose_item_flags(0x0A, 6, SYM, 0),
            ("item_use", ["INVERT_ON_UNDEAD", "RESTORES_HP"]))
        self.assertEqual(
            pip.decompose_item_flags(0xB8, 6, SYM, 0),
            ("item_use", ["RESTORES_HP", "RESTORES_MP", "REMOVES_STATUS",
                          "FRACTIONAL_DAMAGE"]))

    def test_item_use_undocumented_bit_raises(self):
        # Bit 2 is skipped untested by the item-use asl chain.
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_item_flags(0x04, 6, SYM, 0)


class SpellCastDecompositionTests(unittest.TestCase):

    def test_zero_is_none(self):
        self.assertIsNone(pip.decompose_spell_cast(0x00, SYM, 0))

    def test_modes_and_spell(self):
        self.assertEqual(pip.decompose_spell_cast(0xC5, SYM, 0),
                         ("FIRE_2", ["RANDOM_ON_ATTACK", "CAST_ON_ITEM_USE"]))
        self.assertEqual(pip.decompose_spell_cast(0x89, SYM, 0),
                         ("FIRE_3", ["CAST_ON_ITEM_USE"]))
        self.assertEqual(pip.decompose_spell_cast(0x40, SYM, 0),
                         ("FIRE", ["RANDOM_ON_ATTACK"]))

    def test_modeless_byte_raises(self):
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_spell_cast(0x05, SYM, 0)

    def test_unnamed_spell_raises(self):
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_spell_cast(0x7F, SYM, 0)


class SpecialEffectDecompositionTests(unittest.TestCase):

    def test_none(self):
        self.assertEqual(pip.decompose_special_effect(0x00, 1, 0), ("none",))
        self.assertEqual(pip.decompose_special_effect(0x00, 6, 0), ("none",))

    def test_consumable_role(self):
        self.assertEqual(pip.decompose_special_effect(0xFF, 6, 0),
                         ("disabled",))
        self.assertEqual(pip.decompose_special_effect(0x01, 6, 0),
                         ("item_use", "MAGICITE"))
        self.assertEqual(pip.decompose_special_effect(0x06, 6, 0),
                         ("item_use", "DRIED_MEAT"))
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_special_effect(0x07, 6, 0)

    def test_equipment_role(self):
        self.assertEqual(pip.decompose_special_effect(0x50, 1, 0),
                         ("weapon", "DRAINER"))
        self.assertEqual(
            pip.decompose_special_effect(0x05, 1, 0),
            ("equipment", "NONE", "SWORD", ["PHYSICAL"]))
        self.assertEqual(
            pip.decompose_special_effect(0x0E, 3, 0),
            ("equipment", "NONE", "SHIELD", ["PHYSICAL", "MAGIC"]))
        self.assertEqual(
            pip.decompose_special_effect(0x07, 5, 0),
            ("equipment", "NONE", "ZEPHYR_CAPE", ["PHYSICAL"]))

    def test_unnamed_weapon_effect_raises(self):
        with self.assertRaises(pip.ItemPropError):
            pip.decompose_special_effect(0xF0, 1, 0)


class RendererFormattingTests(unittest.TestCase):

    def test_of_or_empty(self):
        self.assertEqual(pip._of_or_empty("ElementSet", "Element", [], "  "),
                         "ElementSet{}")
        self.assertEqual(
            pip._of_or_empty("ElementSet", "Element", ["FIRE"], "  "),
            "ElementSet::of(Element::FIRE)")

    def test_wrap_call_short_stays_single_line(self):
        self.assertEqual(pip._wrap_call("F::of", ["A::B"], " " * 12),
                         "F::of(A::B)")

    def test_wrap_call_long_wraps(self):
        tokens = ["CharacterId::{}".format(n) for n in CHARS.values()]
        rendered = pip._wrap_call("EquipPermissions::of", tokens, " " * 12)
        self.assertIn("\n", rendered)
        for line in rendered.splitlines():
            self.assertLessEqual(len(" " * 12 + line), pip._LINE_WIDTH + 12)
        # Every token survives the wrap.
        for token in tokens:
            self.assertIn(token, rendered)


# --- Layer 2: synthetic inputs -----------------------------------------------

class ReadRecordsTests(unittest.TestCase):

    def test_wrong_length_raises(self):
        # Length is checked before any record is decomposed, so no symbol
        # table is needed.
        for length in (0, 30, pip.EXPECTED_LEN - 1, pip.EXPECTED_LEN + 1):
            fd, path = tempfile.mkstemp(suffix=".dat")
            with os.fdopen(fd, "wb") as fh:
                fh.write(bytes(length))
            try:
                with self.assertRaises(pip.ItemPropError):
                    pip.read_records(path, None)
            finally:
                os.remove(path)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "menu", "item_prop_en.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src item_prop_en.dat not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = pip.Symbols(os.path.join(root, "include", "const.inc"))
        cls.records = pip.read_records(
            os.path.join(root, "src", "menu", "item_prop_en.dat"),
            cls.symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.records), 256)
        for rec in self.records:
            self.assertEqual(len(rec.raw), 30)

    def test_const_inc_values_match_helper_tables(self):
        # The hardcoded maps the helper layers use must agree with the real
        # const.inc — this is the drift check between the two layers.
        for value, name in ITEM_TYPES.items():
            self.assertEqual(self.symbols.type_names[value], name)
        self.assertEqual(tuple(self.symbols.usage_bits), ITEM_USAGE)
        self.assertEqual(tuple(self.symbols.weapon_flag_bits), WEAPON_FLAGS)
        for slot, name in CHARS.items():
            self.assertEqual(self.symbols.char_names[slot], name)

    def test_dirk_boundary_record(self):
        # $00 DIRK: type $11, equip $97DB, power 26, hit 180, weapon flags
        # $C0, price 150 (hand-traced).
        dirk = self.records[0x00]
        self.assertEqual(dirk.name, "DIRK")
        self.assertEqual(dirk.raw[0], 0x11)
        self.assertEqual(dirk.type_name, "WEAPON")
        self.assertEqual(dirk.usage, ["THROW"])
        self.assertEqual(dirk.item_flags, ("weapon", ["TWO_HAND", "RUNIC"]))
        self.assertEqual(dirk.power, 26)
        self.assertEqual(dirk.hit_rate_or_defense, 180)
        self.assertEqual(dirk.price, 150)
        self.assertIsNone(dirk.spell_cast)

    def test_excalibur_full_decomposition(self):
        # $18 EXCALIBUR: holy element, +2/+2 and +1/+1 boosts, special $05
        # (blocks physical, sword graphic), price 2 (hand-traced).
        excalibur = self.records[0x18]
        self.assertEqual(excalibur.name, "EXCALIBUR")
        self.assertEqual(excalibur.element, ["HOLY"])
        self.assertEqual(excalibur.vigor_speed, (2, 2))
        self.assertEqual(excalibur.stamina_magic, (1, 1))
        self.assertEqual(excalibur.special,
                         ("equipment", "NONE", "SWORD", ["PHYSICAL"]))
        self.assertEqual(excalibur.price, 2)
        self.assertEqual(
            excalibur.equip,
            [("CharacterId", "TERRA"), ("CharacterId", "LOCKE"),
             ("CharacterId", "EDGAR"), ("CharacterId", "CELES"),
             ("EquipSpecial", "HEAVY")])

    def test_role_overloaded_rows(self):
        # $E7 POTION carries the named item-use flags and the disabled
        # item-use effect; $F9 MAGICITE carries the dead +19 bit 6 and the
        # MAGICITE item-use effect; the imp-gear bit sits on $68
        # TORTOISESHLD (hand-traced indices).
        by_name = {rec.name: rec for rec in self.records}
        potion = by_name["POTION"]
        self.assertEqual(potion.item_flags,
                         ("item_use", ["INVERT_ON_UNDEAD", "RESTORES_HP"]))
        self.assertEqual(potion.special, ("disabled",))
        magicite = by_name["MAGICITE"]
        self.assertEqual(magicite.item_flags, ("dead", "kDeadItemFlagBit6"))
        self.assertEqual(magicite.special, ("item_use", "MAGICITE"))
        self.assertIn(("EquipSpecial", "IMP"), by_name["TORTOISESHLD"].equip)

    def test_last_record_is_empty_sentinel_row(self):
        self.assertEqual(self.records[0xFF].name, "EMPTY")


if __name__ == "__main__":
    unittest.main()
