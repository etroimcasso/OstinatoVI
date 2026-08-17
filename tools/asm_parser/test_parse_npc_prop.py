#!/usr/bin/env python3
"""Unit tests for parse_npc_prop.py.

Three layers per the parser-test discipline:
  1. pure helpers (the single-enum reader with its shift/alias grammar, switch-id
     validation, and the end_npc byte packing for each variant);
  2. synthetic-input edge cases (record-grammar macro replay, error paths) on
     hand-written fragments;
  3. end-to-end against the real original-src + the vanilla ROM, skipped cleanly
     when either is absent.

Run: python3 -m unittest tools.asm_parser.test_parse_npc_prop
Std-lib only.
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_npc_prop as pnp  # noqa: E402
from common import ParseError  # noqa: E402


def _write_tmp(text, suffix=".inc"):
    fh = tempfile.NamedTemporaryFile("w", suffix=suffix, delete=False,
                                     encoding="utf-8")
    fh.write(text)
    fh.close()
    return fh.name


# --- Layer 1: the single-enum reader ----------------------------------------

_MINI_ENUM = """\
.enum OTHER
        SKIP = 5
.endenum

.enum SAMPLE
        FIRST                           ;= 0
        SECOND                          ;= 1
        HIGH = 1 << 4
        WIDE = %11 << 4
        ALIAS = SECOND
        MASK = %1111
.endenum
"""


class EnumReaderTests(unittest.TestCase):
    def setUp(self):
        self.path = _write_tmp(_MINI_ENUM)

    def tearDown(self):
        os.unlink(self.path)

    def test_reads_only_target_enum(self):
        e = pnp.read_named_enum(self.path, "SAMPLE")
        names = [m.name for m in e.members]
        self.assertEqual(names,
                         ["FIRST", "SECOND", "HIGH", "WIDE", "ALIAS", "MASK"])

    def test_member_forms(self):
        e = pnp.read_named_enum(self.path, "SAMPLE")
        self.assertEqual(e.value_of("FIRST"), 0)      # bare auto-increment
        self.assertEqual(e.value_of("SECOND"), 1)     # bare auto-increment
        self.assertEqual(e.value_of("HIGH"), 0x10)    # 1 << 4
        self.assertEqual(e.value_of("WIDE"), 0x30)    # %11 << 4
        self.assertEqual(e.value_of("ALIAS"), 1)      # alias to SECOND
        self.assertEqual(e.value_of("MASK"), 0x0F)    # %1111

    def test_alias_kind_is_tracked(self):
        e = pnp.read_named_enum(self.path, "SAMPLE")
        by = {m.name: m for m in e.members}
        self.assertEqual(by["ALIAS"].kind, "alias")
        self.assertEqual(by["ALIAS"].symbol, "SECOND")
        self.assertEqual(by["HIGH"].kind, "literal")
        self.assertEqual(by["FIRST"].kind, "bare")

    def test_missing_enum_raises(self):
        with self.assertRaises(ParseError):
            pnp.read_named_enum(self.path, "NOPE")

    def test_doc_value_mismatch_raises(self):
        bad = _write_tmp(".enum E\n        X                    ;= 3\n.endenum\n")
        try:
            with self.assertRaises(ParseError):
                pnp.read_named_enum(bad, "E")  # bare X computes 0, comment says 3
        finally:
            os.unlink(bad)


class EnumHeaderRenderTests(unittest.TestCase):
    def test_exclude_and_alias_rendering(self):
        path = _write_tmp(_MINI_ENUM)
        try:
            e = pnp.read_named_enum(path, "SAMPLE")
            out = pnp.render_enum_header(e, "Sample", "std::uint8_t", "desc",
                                         exclude={"MASK"})
        finally:
            os.unlink(path)
        self.assertIn("enum class Sample : std::uint8_t", out)
        self.assertIn("= SECOND", out)   # ALIAS preserved as an alias
        self.assertIn("= 0x10", out)     # HIGH literal rendered as hex
        self.assertNotIn("MASK", out)    # excluded member absent


class SwitchIdTests(unittest.TestCase):
    def test_valid_and_invalid(self):
        self.assertEqual(pnp._switch_id("$03a0", 1, "p"), 0x3A0)
        with self.assertRaises(ParseError):
            pnp._switch_id("$02ff", 1, "p")   # below the $0300 bias


# --- Layer 1: end_npc byte packing ------------------------------------------

class PackTests(unittest.TestCase):
    def test_pack_normal(self):
        # pos (8,11), switch $043f, EventReturn ($5eb3), CLYDE/LOCKE, SLOW, UP,
        # no reaction -> the exact ROM bytes.
        r = pnp.NpcRecord("npc", 8, 11, 0x43F, 1)
        r.event_offset = 0x5EB3
        r.props = {"gfx": ("CLYDE", 0x23), "pal": ("LOCKE", 1),
                   "speed": ("SLOW", 1), "dir": ("UP", 0),
                   "react": ("NONE", 0x04)}
        self.assertEqual(pnp.pack_record(r),
                         [0xB3, 0x5E, 0xC4, 0x4F, 0x08, 0x4B, 0x23, 0x00, 0x04])

    def test_pack_special_32x32(self):
        # The map-3 airship deck NPC: pos (4,4), switch $03a0, NOTHING/VEHICLE,
        # SLOWER, BACKGROUND, 32x32.
        r = pnp.NpcRecord("special", 4, 4, 0x3A0, 1)
        r.is_32x32 = True
        r.props = {"gfx": ("NOTHING", 0x65), "pal": ("VEHICLE", 7),
                   "speed": ("SLOWER", 0),
                   "layerPriority": ("BACKGROUND", 0x18)}
        self.assertEqual(pnp.pack_record(r),
                         [0x00, 0x00, 0x1C, 0x28, 0x84, 0x04, 0x65, 0x00, 0x1C])

    def test_pack_special_master_no_slave(self):
        # A master reference whose slave bit is cleared: byte 2 bit 1 stays 0.
        r = pnp.NpcRecord("special", 0, 0, 0x300, 1)
        r.master = (0, 4, "DOWN", 1)   # id 0, offset 4, dir DOWN(1)
        r.is_slave = 0
        packed = pnp.pack_record(r)
        self.assertEqual(packed[1], 4 << 5)          # offset packed <<5
        self.assertEqual(packed[2] & 0x03, 0x01)     # master_dir set, slave clear


# --- Layer 2: record-grammar replay -----------------------------------------

_MINI_ENUMS = {
    "MAP_SPRITE_GFX": pnp.NamedEnum("MAP_SPRITE_GFX"),
    "MAP_SPRITE_PAL": pnp.NamedEnum("MAP_SPRITE_PAL"),
    "OBJ_SPEED": pnp.NamedEnum("OBJ_SPEED"),
    "EVENT_DIR": pnp.NamedEnum("EVENT_DIR"),
    "NPC_ANIM_TYPE": pnp.NamedEnum("NPC_ANIM_TYPE"),
    "NPC_ANIM_FRAME": pnp.NamedEnum("NPC_ANIM_FRAME"),
}
for _e, _pairs in [
    ("MAP_SPRITE_GFX", [("TERRA", 0), ("NOTHING", 0x65)]),
    ("MAP_SPRITE_PAL", [("TERRA", 2), ("VEHICLE", 7)]),
    ("OBJ_SPEED", [("SLOWER", 0), ("NORMAL", 2)]),
    ("EVENT_DIR", [("DOWN", 2), ("UP", 0)]),
    ("NPC_ANIM_TYPE", [("TWO_FRAMES", 2)]),
    ("NPC_ANIM_FRAME", [("SPECIAL", 0x40)]),
]:
    for _n, _v in _pairs:
        _MINI_ENUMS[_e].add(pnp.EnumMember(_n, _v, "literal"))

_MINI_RECORDS = """\
.segment "npc_prop"
NPCProp::_0:
        make_npc {8, 3}, $03ff
                set_npc_dir DOWN
                set_npc_speed NORMAL
                set_npc_gfx TERRA
                end_npc
NPCProp::_1:
        make_special_npc {4, 4}, $03a0, {0, 0}
                set_npc_speed SLOWER
                set_npc_gfx NOTHING, VEHICLE
                end_npc
"""


class RecordReplayTests(unittest.TestCase):
    def test_parses_two_maps(self):
        path = _write_tmp(_MINI_RECORDS, suffix=".asm")
        try:
            recs, offs = pnp.parse_npc_asm(path, _MINI_ENUMS, map_slots=2)
        finally:
            os.unlink(path)
        self.assertEqual(len(recs), 2)
        self.assertEqual(offs, [0, 1, 2])
        self.assertEqual(recs[0].variant, "npc")
        self.assertEqual(recs[1].variant, "special")
        # a palette-less set_npc_gfx defaulting to the gfx-named palette alias
        self.assertEqual(recs[0].props["pal"][0], "TERRA")

    def test_unknown_macro_raises(self):
        bad = _write_tmp('.segment "npc_prop"\nNPCProp::_0:\n'
                         '        make_npc {0, 0}, $0300\n'
                         '                set_npc_bogus X\n'
                         '                end_npc\n', suffix=".asm")
        try:
            with self.assertRaises(ParseError):
                pnp.parse_npc_asm(bad, _MINI_ENUMS, map_slots=1)
        finally:
            os.unlink(bad)

    def test_make_without_end_raises(self):
        bad = _write_tmp('.segment "npc_prop"\nNPCProp::_0:\n'
                         '        make_npc {0, 0}, $0300\n'
                         '        make_npc {1, 1}, $0300\n', suffix=".asm")
        try:
            with self.assertRaises(ParseError):
                pnp.parse_npc_asm(bad, _MINI_ENUMS, map_slots=1)
        finally:
            os.unlink(bad)


# --- Layer 3: end-to-end against real source + ROM --------------------------

def _source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    for cand in (os.path.join(here, "..", "..", "original-src"),
                 os.path.join(here, "..", "..", "..", "ff6")):
        if os.path.isdir(os.path.join(cand, "src", "event")):
            return os.path.abspath(cand)
    return None


class EndToEndTests(unittest.TestCase):
    def setUp(self):
        self.root = _source_root()
        if self.root is None:
            self.skipTest("original-src not available")
        if common.find_vanilla_rom(self.root) is None:
            self.skipTest("vanilla ROM not available (set FF6_VANILLA_ROM)")

    def test_full_corpus_resolves_and_cross_checks(self):
        enums, res = pnp.load_and_resolve(self.root)
        self.assertEqual(len(res.records), pnp.NPC_RECORD_COUNT)
        self.assertEqual(len(res.records), 2193)
        specials = sum(1 for r in res.records if r.variant == "special")
        animated = sum(1 for r in res.records if r.variant == "animated")
        self.assertEqual(specials, 240)
        self.assertEqual(animated, 451)
        self.assertEqual(len(res.records) - specials - animated, 1502)
        # EventReturn cross-consistent with the s1 map-init value.
        self.assertEqual(res.event_return_offset, 0x5EB3)
        # 417 offset entries (416 map slots + end); slot 415 empty.
        self.assertEqual(len(res.record_offsets), 417)
        self.assertEqual(res.record_offsets[415], res.record_offsets[416])
        # fixed_block tail is pure 0xFF padding.
        self.assertEqual(res.pad[0], 0xFF)

    def test_all_value_enums_load(self):
        enums = pnp.load_value_enums(self.root)
        # MapSpriteGfx: 165 named sprites, TERRA=0 .. SMALL_BIRD_LEFT=164.
        gfx = enums["MAP_SPRITE_GFX"]
        self.assertEqual(len(gfx.members), 165)
        self.assertEqual(gfx.value_of("TERRA"), 0)
        self.assertEqual(gfx.value_of("SMALL_BIRD_LEFT"), 164)
        # MapSpritePal alias set: LOCKE = MERCHANT = BROWN_SOLDIER = 1.
        pal = enums["MAP_SPRITE_PAL"]
        self.assertEqual(pal.value_of("LOCKE"), 1)
        self.assertEqual(pal.value_of("MERCHANT"), 1)
        self.assertEqual(pal.value_of("BROWN_SOLDIER"), 1)
        # EventVehicle spacing.
        veh = enums["EVENT_VEHICLE"]
        self.assertEqual(veh.value_of("CHOCOBO"), 0x20)
        self.assertEqual(veh.value_of("RAFT"), 0x60)


if __name__ == "__main__":
    unittest.main()
