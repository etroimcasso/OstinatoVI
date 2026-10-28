#!/usr/bin/env python3
"""Unit tests for parse_battle_layout.py.

Three layers, matching the parser test discipline: the table reader against
synthetic assembly, the structural checks' error paths (a facing that is not 0
or 1, a flip that disagrees with the facing, a member off the middle facing
away from it, something between two tables that must be adjacent, a block found
twice), and an end-to-end pass over the real disassembly and cartridge.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_battle_layout as pbl  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "btlgfx",
                                   "btlgfx_main.asm")):
        return candidate
    return None


def _table(label, directive, values):
    return pbl.Table(label=label, line=1, directive=directive, values=values,
                     end_line=1)


def _layout(facings=None, flips=None, angles=None):
    facings = facings or [0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1]
    if flips is None:
        flips = [0x40 if f else 0 for f in facings]
    angles = angles or ([0x170] * 4 + [0x90] * 4 + [0x100] * 4
                        + [0x170, 0x170, 0x90, 0x90])
    return pbl.Layout(
        path="btlgfx_main.asm",
        row_offsets=_table("CharRowOffsetTbl", "word", [12] * 16),
        y_offsets=_table("CharYOffsetTbl", "byte", [0x44] * 20),
        angles=_table("CharXAngleTbl", "word", angles),
        flips=_table("CharFlipTbl", "byte", flips),
        facings=_table("CharDirTbl", "byte", facings))


class ReaderTest(unittest.TestCase):

    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def lines(self, text):
        return text.splitlines(keepends=True)

    def test_words_after_a_local_label_and_on_continuation_lines(self):
        lines = self.lines("Tbl:\n@cee3:  .word   $000c,$fff4\n"
                           "        .word   $0000,$0100\n\nOther:\n")
        table = pbl.read_table("x.asm", lines, "Tbl")
        self.assertEqual(table.directive, "word")
        self.assertEqual(table.values, [0x000C, 0xFFF4, 0x0000, 0x0100])
        self.assertEqual(table.bytes(), [0x0C, 0x00, 0xF4, 0xFF, 0x00, 0x00,
                                         0x00, 0x01])
        self.assertEqual(table.end_line, 3)

    def test_a_blank_line_ends_the_table(self):
        lines = self.lines("Tbl:\n        .byte   1,0\n\n        .byte   7\n")
        self.assertEqual(pbl.read_table("x.asm", lines, "Tbl").values, [1, 0])

    def test_mixed_directives_are_an_error(self):
        lines = self.lines("Tbl:\n        .byte   1\n        .word   2\n")
        with self.assertRaises(ParseError) as ctx:
            pbl.read_table("x.asm", lines, "Tbl")
        self.assertIn("mixes", str(ctx.exception))

    def test_an_instruction_inside_a_table_is_an_error(self):
        lines = self.lines("Tbl:\n        .byte   1\n        rts\n")
        with self.assertRaises(ParseError) as ctx:
            pbl.read_table("x.asm", lines, "Tbl")
        self.assertIn("grammar not covered", str(ctx.exception))

    def test_a_byte_past_ff_is_an_error(self):
        lines = self.lines("Tbl:\n        .byte   $100\n")
        with self.assertRaises(ParseError):
            pbl.read_table("x.asm", lines, "Tbl")

    def test_a_missing_label_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            pbl.read_table("x.asm", self.lines("Other:\n .byte 1\n"), "Tbl")
        self.assertIn("Tbl not found", str(ctx.exception))

    def test_comments_between_adjacent_tables_are_allowed(self):
        lines = self.lines("A:\n        .byte   1\n\n; the next one\nB:\n"
                           "        .byte   2\n")
        first = pbl.read_table("x.asm", lines, "A")
        second = pbl.read_table("x.asm", lines, "B")
        pbl.check_adjacent("x.asm", lines, first, second)

    def test_data_between_adjacent_tables_is_an_error(self):
        lines = self.lines("A:\n        .byte   1\n\nX:\n        .byte   9\n"
                           "\nB:\n        .byte   2\n")
        first = pbl.read_table("x.asm", lines, "A")
        second = pbl.read_table("x.asm", lines, "B")
        with self.assertRaises(ParseError) as ctx:
            pbl.check_adjacent("x.asm", lines, first, second)
        self.assertIn("sits between", str(ctx.exception))

    def test_the_segment_is_the_last_one_above_the_table(self):
        lines = self.lines('.segment "a"\n.segment "b"\nTbl:\n .byte 1\n')
        table = pbl.read_table("x.asm", lines, "Tbl")
        self.assertEqual(pbl.segment_of("x.asm", lines, table), "b")


class CheckTest(unittest.TestCase):

    def test_the_real_shape_passes(self):
        pbl.check(_layout())

    def test_slopes_measure_from_the_middle(self):
        slopes = _layout().slopes()
        self.assertEqual(slopes[0], [112] * 4)
        self.assertEqual(slopes[1], [-112] * 4)
        self.assertEqual(slopes[2], [0] * 4)
        self.assertEqual(slopes[3], [112, 112, -112, -112])

    def test_a_facing_that_is_not_zero_or_one_is_an_error(self):
        facings = [0] * 4 + [2, 1, 1, 1] + [1, 0, 1, 0] + [0, 0, 1, 1]
        with self.assertRaises(ParseError) as ctx:
            pbl.check(_layout(facings=facings,
                              flips=[0x40 if f else 0 for f in facings]))
        self.assertIn("is not 0 or 1", str(ctx.exception))

    def test_a_flip_that_disagrees_with_the_facing_is_an_error(self):
        flips = [0x40 if f else 0 for f in _layout().facings.values]
        flips[0] = 0x40
        with self.assertRaises(ParseError) as ctx:
            pbl.check(_layout(flips=flips))
        self.assertIn("disagrees", str(ctx.exception))

    def test_a_member_right_of_the_middle_facing_right_is_an_error(self):
        angles = [0x170] * 4 + [0x90] * 4 + [0x100] * 4 + [0x170] * 4
        with self.assertRaises(ParseError) as ctx:
            pbl.check(_layout(angles=angles))
        self.assertIn("right of the middle but facing right",
                      str(ctx.exception))

    def test_an_angle_past_the_mask_is_an_error(self):
        angles = [0x200] + [0x170] * 3 + [0x90] * 4 + [0x100] * 4 \
            + [0x170, 0x170, 0x90, 0x90]
        with self.assertRaises(ParseError) as ctx:
            pbl.check(_layout(angles=angles))
        self.assertIn("past $01FF", str(ctx.exception))

    def test_a_short_table_is_an_error(self):
        layout = _layout()
        layout.facings = _table("CharDirTbl", "byte", [0] * 12)
        with self.assertRaises(ParseError) as ctx:
            pbl.check(layout)
        self.assertIn("not 4 rows of 4", str(ctx.exception))


class PlaceTest(unittest.TestCase):

    BANK = 0xC20000

    def _rom(self, at, payload, again=None):
        rom = bytearray(common.hirom_file_offset(self.BANK) + pbl.BANK_SIZE)
        base = common.hirom_file_offset(self.BANK)
        rom[base + at:base + at + len(payload)] = bytes(payload)
        if again is not None:
            rom[base + again:base + again + len(payload)] = bytes(payload)
        return bytes(rom)

    def test_a_block_found_once_gives_its_address(self):
        rom = self._rom(0x1234, [1, 2, 3, 4])
        self.assertEqual(pbl.place_block(rom, self.BANK, [1, 2, 3, 4], "x",
                                         "p"), 0xC21234)

    def test_a_block_found_twice_is_an_error(self):
        rom = self._rom(0x1234, [1, 2, 3, 4], again=0x2000)
        with self.assertRaises(ParseError) as ctx:
            pbl.place_block(rom, self.BANK, [1, 2, 3, 4], "x", "p")
        self.assertIn("occurs 2 times", str(ctx.exception))

    def test_a_block_not_found_is_an_error(self):
        rom = self._rom(0x1234, [1, 2, 3, 4])
        with self.assertRaises(ParseError) as ctx:
            pbl.place_block(rom, self.BANK, [9, 9, 9, 9], "x", "p")
        self.assertIn("occurs 0 times", str(ctx.exception))


class RenderTest(unittest.TestCase):

    def test_rows_name_their_battle_type_facing_and_slope(self):
        text = pbl.render_party_layout_inc(_layout())
        self.assertIn("PartyLayoutRow{ .battleType = BattleType::Pincer", text)
        self.assertIn("{ .facing = Facing::Right, .slope = -112 }", text)
        self.assertEqual(text.count("PartyLayoutRow{"), 4)


@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.layout, cls.segment = pbl.read_layout(_source_root())

    def test_the_tables_are_in_the_far_code_segment(self):
        self.assertEqual(self.segment, "btlgfx_code_far")

    def test_the_facings_are_hand_traced(self):
        self.assertEqual(self.layout.facings.rows(),
                         [[0, 0, 0, 0], [1, 1, 1, 1], [1, 0, 1, 0],
                          [0, 0, 1, 1]])

    def test_the_magitek_train_row_follows_the_four_battle_types(self):
        rows = self.layout.y_offsets.rows()
        self.assertEqual(len(rows), 5)
        self.assertEqual(rows[4], [0x50, 0x5E, 0x6C, 0x7A])

    def test_the_back_row_leans_further_from_the_middle(self):
        for slopes, offsets in zip(self.layout.slopes(),
                                   self.layout.row_offsets.rows()):
            for slope, offset in zip(slopes, offsets):
                self.assertEqual(slope * pbl.signed(offset) >= 0, True)

    def test_both_blocks_are_found_once_in_the_cartridge(self):
        rom = common.load_vanilla_rom(_source_root())
        bank = pbl.read_segment_bank(_source_root(), self.segment)
        self.assertEqual(pbl.place_layout(self.layout, rom, bank),
                         (0xC2CEE3, 0xC2E43E))


if __name__ == "__main__":
    unittest.main()
