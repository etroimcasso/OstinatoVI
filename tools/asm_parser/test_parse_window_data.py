#!/usr/bin/env python3
"""Unit tests for parse_window_data.py.

Four layers, matching the parser test discipline: the readers against synthetic
assembly, the structural checks' error paths (a tile out of place, an edge
column that does not repeat, a shading byte that misses a channel or exceeds
level 7, a wrong wallpaper count), the cartridge occurrence check, and an
end-to-end pass over the real disassembly and cartridge.
"""

from __future__ import annotations

import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_window_data as pwd  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "field", "window.asm")):
        return candidate
    return None


PATHS = {"window_asm": "window.asm", "hdma_asm": "hdma.asm",
         "window_gfx_asm": "window_gfx.asm"}


def _shading():
    # Each synthetic table is a run of one level, and the 8-row one a ramp, so no table occurs
    # inside another.
    tables = {h: [0xE0 | (7 - i)] * h for i, h in enumerate(pwd.SHADING_HEIGHTS[:-1])}
    tables[8] = [0xE0 | level for level in (3, 2, 1, 0, 1, 2, 3, 4)]
    return tables


def _data(formation=None, shading=None, wallpapers=8, palettes=8, stride=0x380):
    return pwd.WindowData(formation if formation is not None else pwd.expected_formation(),
                          shading if shading is not None else _shading(),
                          wallpapers, palettes, stride)


class NamingTest(unittest.TestCase):

    def test_wallpaper_tiles_name_their_row_and_column(self):
        self.assertEqual(pwd.tile_name(0xE0), "WALLPAPER_R0_C0")
        self.assertEqual(pwd.tile_name(0xE7), "WALLPAPER_R1_C3")
        self.assertEqual(pwd.tile_name(0xEF), "WALLPAPER_R3_C3")

    def test_border_tiles_name_their_place(self):
        self.assertEqual(pwd.tile_name(0xF0), "CORNER_TOP_LEFT")
        self.assertEqual(pwd.tile_name(0xF7), "EDGE_RIGHT_ODD")
        self.assertEqual(pwd.tile_name(0xFB), "CORNER_BOTTOM_RIGHT")

    def test_a_byte_outside_the_window_tiles_has_no_name(self):
        for byte in (0xDF, 0xFC, 0x00):
            with self.assertRaises(ValueError):
                pwd.tile_name(byte)

    def test_there_are_28_distinct_names(self):
        names = [pwd.tile_name(t) for t in pwd.all_tiles()]
        self.assertEqual(len(names), 28)
        self.assertEqual(len(set(names)), 28)


class ReaderTest(unittest.TestCase):

    def test_bytes_run_from_the_label_to_the_next_label(self):
        lines = ["Top:\n", "        .byte   $f0,$f1 ; a comment\n", "\n",
                 "        .byte   $f2\n", "Mid:\n", "        .byte   $e0\n"]
        self.assertEqual(pwd.read_label_bytes("w.asm", "Top", lines), [0xF0, 0xF1, 0xF2])
        self.assertEqual(pwd.read_label_bytes("w.asm", "Mid", lines), [0xE0])

    def test_endproc_ends_a_run(self):
        lines = ["Btm:\n", "  .byte $f8\n", ".endproc\n", "  .byte $00\n"]
        self.assertEqual(pwd.read_label_bytes("w.asm", "Btm", lines), [0xF8])

    def test_a_code_line_inside_a_run_is_an_error(self):
        lines = ["Top:\n", "  .byte $f0\n", "  lda #$00\n"]
        with self.assertRaises(ParseError):
            pwd.read_label_bytes("w.asm", "Top", lines)

    def test_a_missing_label_is_an_error(self):
        with self.assertRaises(ParseError):
            pwd.read_label_bytes("w.asm", "Top", ["Other:\n", "  .byte 1\n"])

    def test_local_groups_split_at_each_local_label(self):
        lines = ["Tbl:\n", "@1:  .byte $e7,$e6\n", "     .byte $e5\n",
                 "@2:  .byte $e4\n", "Next:\n", "  .byte $e0\n"]
        self.assertEqual(pwd.read_local_groups("h.asm", "Tbl", lines),
                         [[0xE7, 0xE6, 0xE5], [0xE4]])

    def test_data_before_a_local_label_is_an_error(self):
        with self.assertRaises(ParseError):
            pwd.read_local_groups("h.asm", "Tbl", ["Tbl:\n", "  .byte $e0\n"])

    def test_the_pointer_repeat_gives_count_and_stride(self):
        lines = ["DlgWindowGfxPtrs:\n", "        .repeat 8, i\n",
                 "        .word   i*$0380\n", "        .endrep\n"]
        self.assertEqual(pwd.read_gfx_pointer_stride("w.asm", lines), (8, 0x380))

    def test_an_unexpected_pointer_line_is_an_error(self):
        lines = ["DlgWindowGfxPtrs:\n", "        .word   $0000\n"]
        with self.assertRaises(ParseError):
            pwd.read_gfx_pointer_stride("w.asm", lines)

    def test_incbins_are_counted_per_label(self):
        lines = ["WindowGfx:\n", '  .incbin "a.4bpp"\n', '  .incbin "b.4bpp"\n',
                 "WindowPal:\n", '  .incbin "a.pal"\n']
        self.assertEqual(pwd.count_incbins("g.asm", "WindowGfx", lines), 2)
        self.assertEqual(pwd.count_incbins("g.asm", "WindowPal", lines), 1)


class CheckTest(unittest.TestCase):

    def test_the_expected_formation_passes(self):
        pwd.check(_data(), PATHS)

    def test_the_expected_formation_is_9_by_32(self):
        rows = pwd.expected_formation()
        self.assertEqual(len(rows), 9)
        self.assertTrue(all(len(r) == 32 for r in rows))

    def test_a_tile_out_of_place_is_an_error(self):
        formation = copy.deepcopy(pwd.expected_formation())
        formation[3][10] = 0xE0 if formation[3][10] != 0xE0 else 0xE1
        with self.assertRaises(ParseError) as ctx:
            pwd.check(_data(formation=formation), PATHS)
        self.assertIn("row 3 column 10", str(ctx.exception))

    def test_an_edge_column_that_does_not_repeat_is_an_error(self):
        formation = copy.deepcopy(pwd.expected_formation())
        formation[0][0] = 0xF3
        with self.assertRaises(ParseError) as ctx:
            pwd.check(_data(formation=formation), PATHS)
        self.assertIn("do not repeat", str(ctx.exception))

    def test_a_byte_that_is_no_window_tile_is_an_error(self):
        formation = copy.deepcopy(pwd.expected_formation())
        formation[4][12] = 0x10
        with self.assertRaises(ParseError) as ctx:
            pwd.check(_data(formation=formation), PATHS)
        self.assertIn("not a window tile", str(ctx.exception))

    def test_a_wrong_wallpaper_count_is_an_error(self):
        with self.assertRaises(ParseError):
            pwd.check(_data(wallpapers=7), PATHS)
        with self.assertRaises(ParseError):
            pwd.check(_data(palettes=9), PATHS)

    def test_a_wrong_wallpaper_size_is_an_error(self):
        with self.assertRaises(ParseError):
            pwd.check(_data(stride=0x400), PATHS)

    def test_a_shading_byte_missing_a_channel_is_an_error(self):
        shading = _shading()
        shading[40][5] = 0xA3
        with self.assertRaises(ParseError) as ctx:
            pwd.check(_data(shading=shading), PATHS)
        self.assertIn("all three channels", str(ctx.exception))

    def test_a_shading_level_above_7_is_an_error(self):
        shading = _shading()
        shading[72][0] = 0xE8
        with self.assertRaises(ParseError) as ctx:
            pwd.check(_data(shading=shading), PATHS)
        self.assertIn("above 7", str(ctx.exception))


class RomAssertTest(unittest.TestCase):

    def _bank(self, data, extra=b""):
        formation = bytes(b for row in data.formation for b in row)
        rom = bytearray(0x10000)
        at = 0x1000
        rom[at:at + len(formation)] = formation
        at += len(formation) + 16
        for height in pwd.SHADING_HEIGHTS:
            table = bytes(data.shading[height])
            rom[at:at + len(table)] = table
            at += len(table) + 16
        rom[at:at + len(extra)] = extra
        return bytes(rom)

    def test_each_table_once_passes(self):
        data = _data()
        pwd.assert_rom(self._bank(data), data, PATHS)

    def test_a_missing_formation_is_reported(self):
        data = _data()
        rom = bytearray(self._bank(data))
        rom[0x1000] ^= 0xFF
        with self.assertRaises(ParseError) as ctx:
            pwd.assert_rom(bytes(rom), data, PATHS)
        self.assertIn("formation occurs 0 times", str(ctx.exception))

    def test_a_table_found_twice_is_reported(self):
        data = _data()
        rom = self._bank(data, extra=bytes(data.shading[8]))
        with self.assertRaises(ParseError) as ctx:
            pwd.assert_rom(rom, data, PATHS)
        self.assertIn("8-row shading table occurs 2 times", str(ctx.exception))


class RenderTest(unittest.TestCase):

    def test_the_formation_rows_name_their_tiles(self):
        text = pwd.render_formation_inc(_data())
        self.assertIn("WindowFormationRow{ .row = 0, .tiles = {{", text)
        self.assertIn("WindowTile::CORNER_TOP_LEFT", text)
        self.assertEqual(text.count("WindowFormationRow{"), 9)

    def test_the_shading_rows_carry_decimal_levels(self):
        text = pwd.render_shading_inc(_data())
        for height in pwd.SHADING_HEIGHTS:
            self.assertIn("kWindowShading{} =".format(height), text)
        self.assertNotIn(".level = 0x", text)

    def test_the_tile_enum_has_28_members(self):
        text = pwd.render_window_tile_h()
        self.assertEqual(text.count(" = 0x"), 28)
        self.assertIn("WALLPAPER_R0_C0", text)

    def test_the_fixture_keeps_raw_bytes(self):
        text = pwd.render_fixture(_data())
        self.assertIn("0xF0, 0xF0, 0xF1, 0xF2", text)
        self.assertIn("kExpectedWindowShading72", text)


@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.paths, cls.data = pwd.read_window_data(_source_root())

    def test_the_formation_is_hand_traced(self):
        self.assertEqual(self.data.formation[0][:4], [0xF0, 0xF0, 0xF1, 0xF2])
        self.assertEqual(self.data.formation[1][:6], [0xF4, 0xF4, 0xE0, 0xE1, 0xE2, 0xE3])
        self.assertEqual(self.data.formation[2][-2:], [0xF7, 0xF7])
        self.assertEqual(self.data.formation[8][-1], 0xFB)

    def test_the_open_shading_table_is_hand_traced(self):
        table = [b & 0x1F for b in self.data.shading[72]]
        self.assertEqual(table[:3], [7, 7, 7])
        self.assertEqual(table[33:38], [0, 0, 0, 0, 0])
        self.assertEqual(table[-4:], [7, 7, 7, 7])

    def test_eight_wallpapers_and_palettes(self):
        self.assertEqual(self.data.wallpapers, 8)
        self.assertEqual(self.data.palettes, 8)

    def test_every_table_occurs_once_in_the_cartridge(self):
        pwd.assert_rom(common.load_vanilla_rom(_source_root()), self.data, self.paths)


if __name__ == "__main__":
    unittest.main()
