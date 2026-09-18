#!/usr/bin/env python3
"""Unit tests for parse_menu_orders.py.

Three layers, matching the parser test discipline: the list reader against
synthetic assembly, the structural checks' error paths (a repeated menu place, a
missing terminator, an imp slot on the wrong side of the ten the game reads),
and an end-to-end pass over the real disassembly and cartridge.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_menu_orders as pmo  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "menu", "skills.asm")):
        return candidate
    return None


class _FakeSymbols(object):
    """Stands in for const.inc: three tiny enums."""

    def __init__(self):
        self.by_name = {
            "GENJU": {"RAMUH": 0x36, "KIRIN": 0x37, "SIREN": 0x38},
            "ATTACK": {"FIRE": 0x00, "SCAN": 0x18, "CURE": 0x2D},
            "ITEM": {"DIRK": 0x01, "EMPTY": 0xFF},
        }
        self.by_value = {scope: {v: k for k, v in members.items()}
                         for scope, members in self.by_name.items()}

    value = pmo.Symbols.value
    name = pmo.Symbols.name


class ReaderTest(unittest.TestCase):

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.symbols = _FakeSymbols()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def read(self, text, label="Tbl"):
        path = os.path.join(self.dir, "x.asm")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        return pmo.read_byte_list(path, label, self.symbols)

    def test_decimal_values_with_name_comments(self):
        text = ("Tbl:\n        .byte 1   ; RAMUH\n        .byte 5   ; IFRIT\n"
                "\nOther:\n        .byte 9\n")
        self.assertEqual(self.read(text), [1, 5])

    def test_scoped_item_references(self):
        text = "Tbl:\n        .byte ITEM::DIRK\n        .byte ITEM::EMPTY\n"
        self.assertEqual(self.read(text), [0x01, 0xFF])

    def test_a_local_label_and_a_comma_list(self):
        text = "Tbl:\n@4f49:  .byte   $2d,$00,$18,$ff\n"
        self.assertEqual(self.read(text), [0x2D, 0x00, 0x18, 0xFF])

    def test_a_segment_pop_ends_the_list(self):
        text = "Tbl:\n        .byte 1\n.popseg\n        .byte 2\n"
        self.assertEqual(self.read(text), [1])

    def test_an_unknown_member_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.read("Tbl:\n        .byte ITEM::SWORD\n")
        self.assertIn("unknown ITEM::SWORD", str(ctx.exception))

    def test_an_unknown_scope_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.read("Tbl:\n        .byte WEAPON::DIRK\n")
        self.assertIn("grammar not covered", str(ctx.exception))

    def test_a_code_line_inside_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.read("Tbl:\n        .byte 1\n        rts\n")
        self.assertIn("unexpected line", str(ctx.exception))

    def test_a_missing_label_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.read("Other:\n        .byte 1\n")
        self.assertIn("not found", str(ctx.exception))


def _lists(esper=None, magic=None, imp=None):
    esper = esper if esper is not None else list(range(1, pmo.ESPER_COUNT + 1))
    if magic is None:
        magic = []
        for row in ([0x2D, 0x00, 0x18], [0x2D, 0x18, 0x00], [0x00, 0x18, 0x2D],
                    [0x00, 0x2D, 0x18], [0x18, 0x2D, 0x00], [0x18, 0x00, 0x2D]):
            magic += row + [pmo.TERMINATOR]
    if imp is None:
        imp = [0x01] * pmo.IMP_SLOTS_READ + [pmo.ITEM_EMPTY] * (
            pmo.IMP_SLOTS - pmo.IMP_SLOTS_READ)
    return pmo.Lists(esper, magic, imp)


class CheckTest(unittest.TestCase):

    def setUp(self):
        self.symbols = _FakeSymbols()
        self.paths = {"skills_asm": "skills.asm", "equip_asm": "equip.asm"}

    def check(self, lists):
        pmo.check(lists, self.symbols, self.paths)

    def test_well_formed_lists_pass(self):
        self.check(_lists())

    def test_a_repeated_menu_place_is_an_error(self):
        places = list(range(1, pmo.ESPER_COUNT + 1))
        places[5] = places[4]
        with self.assertRaises(ParseError) as ctx:
            self.check(_lists(esper=places))
        self.assertIn("permutation", str(ctx.exception))

    def test_a_short_esper_list_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.check(_lists(esper=[1, 2, 3]))
        self.assertIn("GenjuOrder holds 3", str(ctx.exception))

    def test_a_missing_terminator_is_an_error(self):
        magic = _lists().magic_order
        magic[3] = 0x00
        with self.assertRaises(ParseError) as ctx:
            self.check(_lists(magic=magic))
        self.assertIn("terminator", str(ctx.exception))

    def test_a_row_missing_a_band_is_an_error(self):
        magic = _lists().magic_order
        magic[4 + 1] = magic[4]
        with self.assertRaises(ParseError) as ctx:
            self.check(_lists(magic=magic))
        self.assertIn("three spell bands", str(ctx.exception))

    def test_a_first_row_that_repeats_a_band_is_an_error(self):
        magic = _lists().magic_order
        magic[1] = magic[0]
        with self.assertRaises(ParseError) as ctx:
            self.check(_lists(magic=magic))
        self.assertIn("repeats a spell band", str(ctx.exception))

    def test_a_band_without_an_attack_name_is_an_error(self):
        magic = _lists().magic_order
        magic[0] = 0x77
        with self.assertRaises(ParseError) as ctx:
            self.check(_lists(magic=magic))
        self.assertIn("no ATTACK enumerator", str(ctx.exception))

    def test_an_empty_read_slot_is_an_error(self):
        imp = _lists().imp_equipment
        imp[3] = pmo.ITEM_EMPTY
        with self.assertRaises(ParseError) as ctx:
            self.check(_lists(imp=imp))
        self.assertIn("ImpItem slot 3 is empty", str(ctx.exception))

    def test_an_item_past_the_read_slots_is_an_error(self):
        imp = _lists().imp_equipment
        imp[12] = 0x01
        with self.assertRaises(ParseError) as ctx:
            self.check(_lists(imp=imp))
        self.assertIn("past the 10", str(ctx.exception))


class RomAssertTest(unittest.TestCase):

    def _rom(self, lists):
        rom = bytearray(common.hirom_file_offset(pmo.IMP_EQUIPMENT_SNES)
                        + pmo.IMP_SLOTS)
        for snes, payload in ((pmo.ESPER_ORDER_SNES, lists.esper_order),
                              (pmo.MAGIC_ORDER_SNES, lists.magic_order),
                              (pmo.IMP_EQUIPMENT_SNES, lists.imp_equipment)):
            base = common.hirom_file_offset(snes)
            rom[base:base + len(payload)] = bytes(payload)
        return rom

    def setUp(self):
        self.paths = {"skills_asm": "skills.asm", "equip_asm": "equip.asm"}
        self.lists = _lists()

    def test_identical_bytes_pass(self):
        pmo.assert_rom(bytes(self._rom(self.lists)), self.lists, self.paths)

    def test_a_differing_byte_is_reported(self):
        rom = self._rom(self.lists)
        rom[common.hirom_file_offset(pmo.ESPER_ORDER_SNES) + 2] ^= 0xFF
        with self.assertRaises(ParseError) as ctx:
            pmo.assert_rom(bytes(rom), self.lists, self.paths)
        self.assertIn("GenjuOrder", str(ctx.exception))

    def test_a_truncated_cartridge_is_reported(self):
        with self.assertRaises(ParseError) as ctx:
            pmo.assert_rom(b"\x00" * 16, self.lists, self.paths)
        self.assertIn("ROM MISMATCH", str(ctx.exception))


@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.paths, cls.symbols, cls.lists = pmo.read_lists(_source_root())

    def test_the_esper_order_is_hand_traced(self):
        self.assertEqual(len(self.lists.esper_order), 27)
        self.assertEqual(self.lists.esper_order[:4], [1, 5, 6, 3])
        self.assertEqual(self.lists.esper_order[-1], 22)

    def test_the_six_settings_are_the_six_band_orderings(self):
        rows = [tuple(row[:3]) for row in pmo.magic_rows(self.lists)]
        self.assertEqual(len(set(rows)), 6)
        self.assertEqual(rows[0], (0x2D, 0x00, 0x18))

    def test_the_imp_list_names_ten_items(self):
        named = [v for v in self.lists.imp_equipment if v != pmo.ITEM_EMPTY]
        self.assertEqual(len(named), 10)
        self.assertEqual(self.lists.imp_equipment[0],
                         self.symbols.by_name["ITEM"]["CURSED_SHLD"])

    def test_all_three_lists_match_the_cartridge(self):
        pmo.assert_rom(common.load_vanilla_rom(_source_root()), self.lists,
                       self.paths)


if __name__ == "__main__":
    unittest.main()
