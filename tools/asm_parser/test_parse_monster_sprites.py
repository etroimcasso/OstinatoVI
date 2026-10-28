#!/usr/bin/env python3
"""Unit tests for parse_monster_sprites.py.

Three layers, matching the parser test discipline: pure helpers against hand-made
inputs, synthetic assembly fragments exercising the grammar's edges and its error
paths, and an end-to-end pass over the real disassembly (skipped when it is not
present) that pins the corpus's own counts and places.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_monster_sprites as pms  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    """The disassembly root, if this machine has one."""
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "gfx", "monster_gfx.asm")):
        return candidate
    return None


SMALL = {"GUARD": 0, "SOLDIER": 1}
LARGE = {"OSTEOSAUR": 0}


class Row(object):
    """A stand-in for a parse_rom_regions.RegionRow."""

    def __init__(self, asset, begin, end):
        self.asset = asset
        self.begin = begin
        self.end = end


def _write_tree(root, monster_gfx, stencil_inc, btlgfx, cfg_align="$0200"):
    files = {
        os.path.join("src", "gfx", "monster_gfx.asm"): monster_gfx,
        os.path.join("include", "gfx", "monster_gfx.inc"): stencil_inc,
        os.path.join("src", "btlgfx", "btlgfx_main.asm"): btlgfx,
        os.path.join("cfg", "ff6-en.cfg"):
            "    monster_gfx_prop: load = bank_d2, type = ro, align = {};\n"
            "    btlgfx_code_far: load = bank_c2, type = ro;\n"
            .format(cfg_align),
        os.path.join("cfg", "ff6-jp.cfg"):
            "    monster_gfx_prop: load = bank_d2, type = ro, align = {};\n"
            .format(cfg_align),
    }
    for path, text in files.items():
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as fh:
            fh.write(text)


STENCIL_INC = (
    ".enum MONSTER_STENCIL_SMALL\n"
    "        GUARD\n"
    "        SOLDIER\n"
    ".endenum\n"
    ".enum MONSTER_STENCIL_LARGE\n"
    "        OSTEOSAUR\n"
    ".endenum\n")

MONSTER_GFX = (
    ".segment \"monster_gfx_prop\"\n"
    "\n"
    "; d2/7000\n"
    "MonsterGfxProp:\n"
    "        make_monster_gfx_prop GUARD, 4, 8\n"
    "        make_monster_gfx_prop OSTEOSAUR, 4, 16\n"
    "        make_monster_gfx_prop SOLDIER, 3, 8, GUARD\n"
    "\n"
    "; ------------------------------------------------------------------\n"
    "\n"
    ".segment \"monster_pal\"\n")

BTLGFX = (
    ".segment \"btlgfx_code_far\"\n"
    "\n"
    ".scope MonsterVRAMMap\n"
    "        ARRAY_LENGTH = 2\n"
    ".endscope\n"
    "\n"
    "MonsterVRAMMapPtrs:\n"
    "@d01a:  ptr_tbl MonsterVRAMMap\n"
    "\n"
    "MonsterVRAMMap::_0:\n"
    "@d034:  def_monster_vram_map 0,0,8,8\n"
    "        def_monster_vram_map 8,0,8,8\n"
    "\n"
    "MonsterVRAMMap::_1:\n"
    "        def_monster_vram_map 0,0,16,16  ; one big box\n"
    "\n"
    "; ------------------------------------------------------------------\n")


class GfxPropLineTest(unittest.TestCase):
    def test_a_small_record(self):
        record = pms.parse_gfx_prop_line(
            "make_monster_gfx_prop SOLDIER, 4, 8", "x", 1, SMALL, LARGE)
        self.assertEqual((record.gfx, record.bpp, record.size, record.pal,
                          record.stencil, record.large),
                         ("SOLDIER", 4, 8, None, 1, False))

    def test_a_large_record_with_a_palette(self):
        record = pms.parse_gfx_prop_line(
            "make_monster_gfx_prop OSTEOSAUR, 3, 16, GUARD", "x", 1, SMALL,
            LARGE)
        self.assertEqual((record.bpp, record.pal, record.stencil,
                          record.large), (3, "GUARD", 0, True))

    def test_the_size_picks_the_stencil_family(self):
        # GUARD has a small stencil and no large one.
        with self.assertRaises(ParseError):
            pms.parse_gfx_prop_line("make_monster_gfx_prop GUARD, 4, 16", "x",
                                    1, SMALL, LARGE)

    def test_bad_depth_and_size_are_errors(self):
        for line in ("make_monster_gfx_prop GUARD, 2, 8",
                     "make_monster_gfx_prop GUARD, 4, 12",
                     "make_monster_gfx_prop GUARD 4 8",
                     ".word $1234"):
            with self.assertRaises(ParseError, msg=line):
                pms.parse_gfx_prop_line(line, "x", 1, SMALL, LARGE)


class AssembleBoxesTest(unittest.TestCase):
    def test_pointers_address_each_layout_after_the_table(self):
        layouts = [[pms.Box(0, 0, 0, 0, 8, 8, 1), pms.Box(0, 1, 8, 0, 8, 8, 2)],
                   [pms.Box(1, 0, 0, 8, 16, 16, 3)]]
        pointers, boxes = pms.assemble_boxes(layouts, 0xD01A)
        # Two pointers (4 bytes), then layout 0 at $D01E and layout 1 at $D026.
        self.assertEqual(pointers, [0x1E, 0xD0, 0x26, 0xD0])
        # Destination word x*$20 + y*$200, then width and height.
        self.assertEqual(boxes, [0x00, 0x00, 8, 8,
                                 0x00, 0x01, 8, 8,
                                 0x00, 0x10, 16, 16])


class SyntheticSourceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_reads_records_up_to_the_separator(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC, BTLGFX)
        props = pms.read_gfx_props(self.root)
        self.assertEqual([r.gfx for r in props.records],
                         ["GUARD", "OSTEOSAUR", "SOLDIER"])
        self.assertEqual((props.small_count, props.large_count), (2, 1))

    def test_a_stray_line_in_the_records_is_an_error(self):
        _write_tree(self.root, MONSTER_GFX.replace(
            "make_monster_gfx_prop OSTEOSAUR, 4, 16",
            ".byte $00"), STENCIL_INC, BTLGFX)
        with self.assertRaises(ParseError):
            pms.read_gfx_props(self.root)

    def test_a_missing_label_is_an_error(self):
        _write_tree(self.root, MONSTER_GFX.replace("MonsterGfxProp:", "Other:"),
                    STENCIL_INC, BTLGFX)
        with self.assertRaises(ParseError):
            pms.read_gfx_props(self.root)

    def test_reads_the_layouts_and_their_segment(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC, BTLGFX)
        layouts, segment = pms.read_boxes(self.root)
        self.assertEqual(segment, "btlgfx_code_far")
        self.assertEqual([[(b.slot, b.width, b.height) for b in layout]
                          for layout in layouts],
                         [[(0, 8, 8), (1, 8, 8)], [(0, 16, 16)]])
        self.assertEqual(pms.read_segment_bank(self.root, segment), 0xC20000)

    def test_a_layout_count_unlike_array_length_is_an_error(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC,
                    BTLGFX.replace("ARRAY_LENGTH = 2", "ARRAY_LENGTH = 3"))
        with self.assertRaises(ParseError):
            pms.read_boxes(self.root)

    def test_layouts_out_of_order_are_an_error(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC,
                    BTLGFX.replace("MonsterVRAMMap::_1:", "MonsterVRAMMap::_2:"))
        with self.assertRaises(ParseError):
            pms.read_boxes(self.root)

    def test_a_layout_with_no_box_is_an_error(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC, BTLGFX.replace(
            "        def_monster_vram_map 0,0,16,16  ; one big box\n", ""))
        with self.assertRaises(ParseError):
            pms.read_boxes(self.root)

    def test_the_records_end_where_the_palettes_begin(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC, BTLGFX)
        props = pms.read_gfx_props(self.root)
        rows = {language: [Row("MONSTER_GFX_PAL", 0xD2700F, 0xD27FFF)]
                for language in ("en", "jp")}
        placed = pms.place_gfx_props(props, rows, None, {"en": 0x200})
        # Three records of five bytes end at $D2700E.
        self.assertEqual([(p.language, p.begin, p.size, p.proved)
                          for p in placed],
                         [("en", 0xD27000, 15, 0), ("jp", 0xD27000, 15, 0)])

    def test_records_off_the_alignment_are_an_error(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC, BTLGFX)
        props = pms.read_gfx_props(self.root)
        rows = {language: [Row("MONSTER_GFX_PAL", 0xD27010, 0xD27FFF)]
                for language in ("en", "jp")}
        with self.assertRaises(ParseError):
            pms.place_gfx_props(props, rows, None, {"en": 0x200})

    def test_a_record_unlike_the_cartridge_is_an_error(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC, BTLGFX)
        props = pms.read_gfx_props(self.root)
        # A stand-in cartridge: bank $D2 holding the three records at $7000.
        rom = bytearray(0x130000)
        at = common.hirom_file_offset(0xD27000)
        good = [0, 0, 0x00, 0, 0,   0, 0, 0x80, 0, 0,   0, 0x80, 0x00, 0, 1]
        rom[at:at + 15] = bytes(good)
        self.assertEqual(pms.check_gfx_props_against_rom(props, 0xD27000, rom),
                         3)
        for byte, value in ((4, 1), (7, 0x00), (11, 0x00)):
            bad = bytearray(rom)
            bad[at + byte] = value
            with self.assertRaises(ParseError, msg="byte {}".format(byte)):
                pms.check_gfx_props_against_rom(props, 0xD27000, bad)

    def test_the_boxes_are_found_once_with_their_pointers_before_them(self):
        _write_tree(self.root, MONSTER_GFX, STENCIL_INC, BTLGFX)
        layouts, _segment = pms.read_boxes(self.root)
        pointers, boxes = pms.assemble_boxes(layouts, 0xD01A)
        rom = bytearray(0x130000)
        at = common.hirom_file_offset(0xC2D01A)
        rom[at:at + len(pointers) + len(boxes)] = bytes(pointers + boxes)
        self.assertEqual(pms.place_boxes(layouts, 0xC20000, rom), 0xC2D01A)
        # Pointers that do not address the boxes are refused.
        wrong = bytearray(rom)
        wrong[at] ^= 0xFF
        with self.assertRaises(ParseError):
            pms.place_boxes(layouts, 0xC20000, wrong)
        # So are boxes that appear twice.
        twice = bytearray(rom)
        later = common.hirom_file_offset(0xC2E000)
        twice[later:later + len(boxes)] = bytes(boxes)
        with self.assertRaises(ParseError):
            pms.place_boxes(layouts, 0xC20000, twice)


@unittest.skipUnless(_source_root(), "original-src not present")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _source_root()
        cls.props = pms.read_gfx_props(cls.root)
        cls.layouts, cls.segment = pms.read_boxes(cls.root)

    def test_corpus_counts(self):
        self.assertEqual(len(self.props.records), 416)
        self.assertEqual(self.props.small_count, 128)
        self.assertEqual(self.props.large_count, 48)
        self.assertEqual(len(self.layouts), 13)
        self.assertEqual(sum(len(layout) for layout in self.layouts), 48)

    def test_the_boxes_live_in_the_second_battle_graphics_bank(self):
        self.assertEqual(pms.read_segment_bank(self.root, self.segment),
                         0xC20000)

    def test_both_languages_align_the_records_on_a_512_byte_boundary(self):
        self.assertEqual(pms.read_segment_align(self.root, "en"), 0x200)
        self.assertEqual(pms.read_segment_align(self.root, "jp"), 0x200)

    @unittest.skipIf(common.find_vanilla_rom(_source_root()) is None,
                     "no vanilla cartridge (set FF6_VANILLA_ROM)")
    def test_run_check_only_ok(self):
        self.assertEqual(pms.run(self.root, REPO_ROOT, check_only=True), 0)

    @unittest.skipIf(common.find_vanilla_rom(_source_root()) is None,
                     "no vanilla cartridge (set FF6_VANILLA_ROM)")
    def test_the_box_table_is_where_the_source_places_it(self):
        rom = common.load_vanilla_rom(self.root)
        self.assertEqual(pms.place_boxes(self.layouts, 0xC20000, rom),
                         0xC2D01A)


if __name__ == "__main__":
    unittest.main()
