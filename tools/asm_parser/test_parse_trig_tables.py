#!/usr/bin/env python3
"""Unit tests for parse_trig_tables.py.

Three layers, matching the parser test discipline: the helpers (sign
conversion, the two formulas), synthetic assembly fragments exercising the
table reader and the formula checks' error paths, and an end-to-end pass over
the real disassembly and cartridge.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_trig_tables as ptt  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "btlgfx",
                                   "btlgfx_main.asm")):
        return candidate
    return None


class HelperTest(unittest.TestCase):

    def test_signed_bytes_and_words(self):
        self.assertEqual(ptt.signed(0x7F, 8), 127)
        self.assertEqual(ptt.signed(0x81, 8), -127)
        self.assertEqual(ptt.signed(0x8001, 16), -32767)
        self.assertEqual(ptt.signed(0x0000, 16), 0)

    def test_arc_tangent_formula(self):
        self.assertEqual(ptt.arc_tangent_formula(0, 0), 64)
        self.assertEqual(ptt.arc_tangent_formula(7, 0), 64)
        self.assertEqual(ptt.arc_tangent_formula(1, 1), 32)
        self.assertEqual(ptt.arc_tangent_formula(0, 9), 0)
        self.assertEqual(ptt.arc_tangent_formula(1, 2), 19)

    def test_sine_formula(self):
        self.assertEqual(ptt.sine_formula(64, 127), 127)
        self.assertEqual(ptt.sine_formula(192, 32767), -32767)
        self.assertEqual(ptt.sine_formula(0, 32767), 0)
        self.assertEqual(ptt.sine_formula(128, 127), 0)


class ReaderTest(unittest.TestCase):

    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def read(self, text, label, directive):
        path = os.path.join(self.dir, "x.asm")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        return ptt.read_run(path, label, directive)

    def test_values_stop_at_the_next_label(self):
        text = ("; comment\nTbl:\n@fc6d:  .word   $0000,$0324\n"
                "        .word   $fcdc\n\n; next\nOther:\n        .word   $1234\n")
        self.assertEqual(self.read(text, "Tbl", "word"), [0, 0x324, 0xFCDC])

    def test_a_code_line_inside_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.read("Tbl:\n        .byte   $01\n        rts\n", "Tbl", "byte")
        self.assertIn("grammar not covered", str(ctx.exception))

    def test_mixed_widths_are_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.read("Tbl:\n        .word   $0001\n", "Tbl", "byte")
        self.assertIn("mixes", str(ctx.exception))

    def test_an_oversized_byte_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.read("Tbl:\n        .byte   $100\n", "Tbl", "byte")
        self.assertIn("8-bit", str(ctx.exception))

    def test_a_missing_label_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.read("Other:\n        .byte   $01\n", "Tbl", "byte")
        self.assertIn("not found", str(ctx.exception))


def _formula_tables():
    """Tables built exactly from the formulas, plus the pinned deviations."""
    arc = []
    below = ptt.ARC_TANGENT_BELOW
    for y in range(32):
        for x in range(32):
            value = ptt.arc_tangent_formula(y, x)
            if below and x > 0 and value > 0:
                value -= 1
                below -= 1
            arc.append(value)
    sine16 = [ptt.sine_formula(a, 32767) & 0xFFFF for a in range(256)]
    down = ptt.SINE16_BELOW
    up = ptt.SINE16_ABOVE
    for a in range(1, 256):
        if a in (64, 192):
            continue
        if down:
            sine16[a] = (sine16[a] - 1) & 0xFFFF
            down -= 1
        elif up:
            sine16[a] = (sine16[a] + 1) & 0xFFFF
            up -= 1
    sine8 = [ptt.sine_formula(a, 127) & 0xFF for a in range(256)]
    return ptt.Tables(arc, sine16, sine8)


class FormulaCheckTest(unittest.TestCase):

    def test_tables_with_the_pinned_deviations_pass(self):
        ptt.check(_formula_tables(), "x")

    def test_a_short_table_is_an_error(self):
        tables = _formula_tables()
        tables.sine8 = tables.sine8[:-1]
        with self.assertRaises(ParseError) as ctx:
            ptt.check(tables, "x")
        self.assertIn("SineTbl8 holds 255", str(ctx.exception))

    def test_an_arc_tangent_above_the_formula_is_an_error(self):
        tables = _formula_tables()
        tables.arc_tangent[33] += 2
        with self.assertRaises(ParseError) as ctx:
            ptt.check(tables, "x")
        self.assertIn("ArcTanTbl (1, 1)", str(ctx.exception))

    def test_a_changed_arc_tangent_count_is_an_error(self):
        tables = _formula_tables()
        tables.arc_tangent[31 * 32 + 31] -= 1       # (31, 31) is exact
        with self.assertRaises(ParseError) as ctx:
            ptt.check(tables, "x")
        self.assertIn("expected 323", str(ctx.exception))

    def test_a_sine16_two_away_is_an_error(self):
        tables = _formula_tables()
        tables.sine16[64] = 32765
        with self.assertRaises(ParseError) as ctx:
            ptt.check(tables, "x")
        self.assertIn("SineTbl16[64]", str(ctx.exception))

    def test_a_changed_sine16_count_is_an_error(self):
        tables = _formula_tables()
        tables.sine16[64] = 32766
        with self.assertRaises(ParseError) as ctx:
            ptt.check(tables, "x")
        self.assertIn("expected 13 and 18", str(ctx.exception))

    def test_any_sine8_deviation_is_an_error(self):
        tables = _formula_tables()
        tables.sine8[32] += 1
        with self.assertRaises(ParseError) as ctx:
            ptt.check(tables, "x")
        self.assertIn("SineTbl8[32]", str(ctx.exception))


class RomAssertTest(unittest.TestCase):

    def _rom(self, tables):
        rom = bytearray(common.hirom_file_offset(ptt.SINE8_SNES) + 256)
        base = common.hirom_file_offset(ptt.ARC_TANGENT_SNES)
        rom[base:base + 1024] = bytes(tables.arc_tangent)
        base = common.hirom_file_offset(ptt.SINE16_SNES)
        for i, word in enumerate(tables.sine16):
            rom[base + 2 * i] = word & 0xFF
            rom[base + 2 * i + 1] = word >> 8
        base = common.hirom_file_offset(ptt.SINE8_SNES)
        rom[base:base + 256] = bytes(tables.sine8)
        return rom

    def test_identical_bytes_pass(self):
        tables = _formula_tables()
        ptt.assert_rom(bytes(self._rom(tables)), tables, "x")

    def test_a_byte_swapped_word_is_reported(self):
        tables = _formula_tables()
        rom = self._rom(tables)
        base = common.hirom_file_offset(ptt.SINE16_SNES)
        rom[base + 2], rom[base + 3] = rom[base + 3], rom[base + 2]
        with self.assertRaises(ParseError) as ctx:
            ptt.assert_rom(bytes(rom), tables, "x")
        self.assertIn("SineTbl16", str(ctx.exception))


@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.asm = os.path.join(_source_root(), "src", "btlgfx", "btlgfx_main.asm")
        cls.tables = ptt.read_tables(cls.asm)

    def test_the_first_arc_tangent_rows_are_hand_traced(self):
        self.assertEqual(self.tables.arc_tangent[:4], [0x40, 0, 0, 0])
        self.assertEqual(self.tables.arc_tangent[32:36], [0x40, 0x20, 0x12, 0x0D])

    def test_the_sine_extremes_are_hand_traced(self):
        self.assertEqual(self.tables.sine16[64], 0x7FFF)
        self.assertEqual(self.tables.sine16[99], 0x539A)
        self.assertEqual(self.tables.sine16[192], 0x8001)
        self.assertEqual(self.tables.sine8[64], 0x7F)
        self.assertEqual(self.tables.sine8[192], 0x81)

    def test_all_three_tables_match_the_cartridge(self):
        ptt.assert_rom(common.load_vanilla_rom(_source_root()), self.tables,
                       self.asm)


if __name__ == "__main__":
    unittest.main()
