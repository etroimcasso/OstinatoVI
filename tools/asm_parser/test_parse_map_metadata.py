#!/usr/bin/env python3
"""Unit tests for parse_map_metadata.py.

Three layers per the parser-test discipline:
  1. pure helpers (_word / _signed and the bg-anim .byte-body reader, including
     the label-alias and offset-by-summation logic);
  2. synthetic inputs exercising every structural assert (record widths, the
     layout spare-bit invariant, the bg-anim index-space coverage, the bg3 fixed
     record width, malformed grammar);
  3. end-to-end against the real original-src field .dat / .asm files (skipped
     cleanly when the rip output is absent).

Python 3 stdlib only. Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_map_metadata as pmm
from common import ParseError


def _write_dat(data):
    fd, path = tempfile.mkstemp(suffix=".dat")
    with os.fdopen(fd, "wb") as fh:
        fh.write(bytes(data))
    return path


def _write_asm(text):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


# --- Layer 1: pure helpers ---------------------------------------------------

class HelperTests(unittest.TestCase):

    def test_word_little_endian(self):
        self.assertEqual(pmm._word([0x00, 0x01], 0), 0x0100)
        self.assertEqual(pmm._word([0xFF, 0xFF], 0), 0xFFFF)
        self.assertEqual(pmm._word([0xAA, 0x34, 0x12], 1), 0x1234)

    def test_signed_twos_complement(self):
        self.assertEqual(pmm._signed(0), 0)
        self.assertEqual(pmm._signed(127), 127)
        self.assertEqual(pmm._signed(128), -128)
        self.assertEqual(pmm._signed(0xD0), -48)
        self.assertEqual(pmm._signed(255), -1)


class ByteBodyTests(unittest.TestCase):

    def _read(self, text, expected):
        path = _write_asm(text)
        try:
            return pmm._read_byte_bodies(path, pmm._BG_ANIM_LABEL, expected)
        finally:
            os.remove(path)

    def test_basic_bodies_and_offsets(self):
        text = (".list off\n"
                "MapBGAnimProp::_0:\n"
                "        .byte   $00,$01\n"
                "MapBGAnimProp::_1:\n"
                "        .byte   $02,$03,$04\n"
                ".list on\n")
        stream, offsets = self._read(text, 2)
        self.assertEqual(stream, bytes([0, 1, 2, 3, 4]))
        self.assertEqual(offsets, [0, 2, 5])  # index0, index1, end

    def test_alias_shares_one_offset(self):
        # _2 and _1 both precede one body -> both alias it (physical order).
        text = ("MapBGAnimProp::_0:\n"
                "        .byte   $00,$01\n"
                "MapBGAnimProp::_2:\n"
                "MapBGAnimProp::_1:\n"
                "        .byte   $02,$03,$04\n")
        stream, offsets = self._read(text, 3)
        self.assertEqual(stream, bytes([0, 1, 2, 3, 4]))
        # offsets indexed by animation index: 0 -> 0, 1 -> 2, 2 -> 2, end -> 5.
        self.assertEqual(offsets, [0, 2, 2, 5])

    def test_malformed_line_raises(self):
        text = ("MapBGAnimProp::_0:\n"
                "        .word   $1234\n")  # .word is not valid here
        with self.assertRaises(ParseError) as ctx:
            self._read(text, 1)
        self.assertIn("label or .byte", str(ctx.exception))

    def test_incomplete_index_space_raises(self):
        # labels 0 and 2 but not 1 -> the 0..2 space is not fully covered.
        text = ("MapBGAnimProp::_0:\n"
                "        .byte   $00\n"
                "MapBGAnimProp::_2:\n"
                "        .byte   $01\n")
        with self.assertRaises(ParseError) as ctx:
            self._read(text, 3)
        self.assertIn("index space", str(ctx.exception))

    def test_out_of_range_byte_raises(self):
        text = ("MapBGAnimProp::_0:\n"
                "        .byte   $100\n")  # not a valid byte
        with self.assertRaises(ParseError):
            self._read(text, 1)


# --- Layer 2: synthetic structural guards ------------------------------------

class MapPropTests(unittest.TestCase):

    def test_wrong_length_raises(self):
        path = _write_dat([0] * (pmm.MAP_PROP_WIDTH * 2))
        try:
            with self.assertRaises(ParseError):
                pmm.read_map_prop(path)
        finally:
            os.remove(path)

    def test_layout_spare_bits_set_raises(self):
        data = bytearray(pmm.MAP_PROP_WIDTH * pmm.MAP_PROP_ROWS)
        data[16] = 0xC0  # record 0 layout byte +16 top bits -> spare bits 30-31
        path = _write_dat(data)
        try:
            with self.assertRaises(ParseError) as ctx:
                pmm.read_map_prop(path)
            self.assertIn("spare bits", str(ctx.exception))
        finally:
            os.remove(path)

    def test_all_zero_ok(self):
        data = bytearray(pmm.MAP_PROP_WIDTH * pmm.MAP_PROP_ROWS)
        path = _write_dat(data)
        try:
            rows = pmm.read_map_prop(path)
            self.assertEqual(len(rows), pmm.MAP_PROP_ROWS)
        finally:
            os.remove(path)


class FlatDatTests(unittest.TestCase):

    def test_init_npc_wrong_size_raises(self):
        path = _write_dat([0] * 64)
        try:
            with self.assertRaises(ParseError):
                pmm.read_init_npc_switch(path)
        finally:
            os.remove(path)

    def test_parallax_wrong_size_raises(self):
        path = _write_dat([0] * 7)
        try:
            with self.assertRaises(ParseError):
                pmm.read_parallax(path)
        finally:
            os.remove(path)


class Bg3RecordTests(unittest.TestCase):

    def test_wrong_record_width_raises(self):
        # All six indexes present, but _0 is not 20 bytes -> width check fires.
        good = "        .byte   " + ",".join(["$00"] * 20) + "\n"
        text = "MapBG3AnimProp::_0:\n        .byte   $00,$01,$02\n"
        for i in range(1, 6):
            text += "MapBG3AnimProp::_{}:\n".format(i) + good
        path = _write_asm(text)
        try:
            with self.assertRaises(ParseError) as ctx:
                pmm.read_bg3_anim(path)
            self.assertIn("bg3 index", str(ctx.exception))
        finally:
            os.remove(path)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "field", "map_prop.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src field .dat files not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _find_source_root()
        cls.p = pmm._paths(cls.root)

    def test_record_counts(self):
        self.assertEqual(len(pmm.read_map_prop(self.p["map_prop"])), 415)
        self.assertEqual(len(pmm.read_parallax(self.p["parallax"])), 21)
        self.assertEqual(len(pmm.read_pal_anim(self.p["pal_anim"])), 10)
        self.assertEqual(len(pmm.read_init_npc_switch(self.p["init_npc"])), 128)
        self.assertEqual(len(pmm.read_bg3_anim(self.p["bg3_anim"])), 6)

    def test_bg_anim_offsets_hand_traced(self):
        _stream, offsets = pmm._read_byte_bodies(
            self.p["bg_anim"], pmm._BG_ANIM_LABEL, pmm.BG_ANIM_INDEXES)
        self.assertEqual(len(offsets), 21)
        self.assertEqual(offsets[-1], 1440)          # end == stream length
        self.assertEqual(offsets[7], 560)
        self.assertEqual(offsets[8], 580)            # index 7 body is 20 bytes
        # Q1: 11/12/17/19 alias one body.
        self.assertEqual(offsets[11], offsets[12])
        self.assertEqual(offsets[11], offsets[17])
        self.assertEqual(offsets[11], offsets[19])

    def test_map_prop_hand_traced(self):
        rows = pmm.read_map_prop(self.p["map_prop"])
        self.assertEqual(list(rows[0]), [0] * 33)    # map 0 is all-zero
        self.assertEqual(rows[414][2], 0x80)         # map 414 battle-bg byte
        self.assertEqual(rows[4][2], 0xA6)           # map 4 battle-bg byte

    def test_parallax_hand_traced(self):
        rows = pmm.read_parallax(self.p["parallax"])
        self.assertEqual(rows[3][0], 0xD0)           # -48 as a raw byte

    def test_run_check_only_ok(self):
        self.assertEqual(pmm.run(self.root, ".", check_only=True), 0)


if __name__ == "__main__":
    unittest.main()
