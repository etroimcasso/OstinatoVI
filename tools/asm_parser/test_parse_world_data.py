#!/usr/bin/env python3
"""Unit tests for parse_world_data.py.

Three layers per the parser-test discipline:
  1. pure helpers (chunk decoding, the sine generator, patch-record bounds,
     fixed_block tail classification);
  2. synthetic-input edge cases (the world_data.asm / world_mod / event_main.asm
     grammars, and every error path) on hand-written fragments;
  3. end-to-end against the real original-src + the vanilla ROM, skipped cleanly
     when either is absent.

Run: python3 -m unittest tools.asm_parser.test_parse_world_data
Std-lib only.
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_world_data as pwd  # noqa: E402
from common import ParseError  # noqa: E402


def _write_tmp(text, suffix=".asm"):
    fh = tempfile.NamedTemporaryFile("w", suffix=suffix, delete=False,
                                     encoding="utf-8")
    fh.write(text)
    fh.close()
    return fh.name


def _chunk(event_bit, patch_offset):
    return bytes([event_bit & 0xFF, (event_bit >> 8) & 0xFF,
                  patch_offset & 0xFF, (patch_offset >> 8) & 0xFF])


def _patch(width, height):
    """A well-formed patch record: u16 destination, packed size, w*h payload."""
    return bytes([0x00, 0x00, ((width & 0xF) << 4) | (height & 0xF)]) \
        + bytes(width * height)


# --- Layer 1: pure helpers ---------------------------------------------------

class HiRomBlockAddressTests(unittest.TestCase):
    """The three block bases this parser reads, mapped to file offsets."""

    def test_block_bases(self):
        self.assertEqual(common.hirom_file_offset(pwd.WORLD_MOD_SNES), 0x0EF600)
        self.assertEqual(common.hirom_file_offset(pwd.WORLD_DATA_SNES), 0x2EB260)
        self.assertEqual(common.hirom_file_offset(pwd.WORLD_SINE_SNES), 0x2FFEF1)


class SineFormulaTests(unittest.TestCase):
    def test_accepts_the_generated_table(self):
        import math
        table = bytes(
            math.floor(abs(math.sin(2.0 * math.pi * x / 360.0) * 255.0))
            for x in range(pwd.WORLD_SINE_LENGTH))
        pwd._assert_sine_formula(table, "<synthetic>")  # must not raise

    def test_known_quadrant_values(self):
        import math
        value = lambda x: math.floor(  # noqa: E731
            abs(math.sin(2.0 * math.pi * x / 360.0) * 255.0))
        self.assertEqual(value(0), 0)
        self.assertEqual(value(90), 255)
        self.assertEqual(value(180), 0)
        self.assertEqual(value(270), 255)

    def test_single_wrong_entry_raises(self):
        import math
        table = bytearray(
            math.floor(abs(math.sin(2.0 * math.pi * x / 360.0) * 255.0))
            for x in range(pwd.WORLD_SINE_LENGTH))
        table[137] ^= 0x01
        with self.assertRaises(ParseError) as ctx:
            pwd._assert_sine_formula(bytes(table), "<synthetic>")
        self.assertIn("sine[137]", str(ctx.exception))


class ModificationDecodeTests(unittest.TestCase):
    """The 4-byte chunk grammar and its two range asserts."""

    def test_two_lists_become_one_contiguous_run_with_offsets(self):
        lists = [_chunk(267, 0x48) + _chunk(268, 0x4F), _chunk(262, 0x56)]
        mods, offsets = pwd._parse_modifications(lists, 0x0C, 0x100, "<syn>")
        self.assertEqual(len(mods), 3)
        self.assertEqual(offsets, [0, 2, 3])
        self.assertEqual([m.world for m in mods], [0, 0, 1])
        self.assertEqual(mods[0].event_bit, 267)
        self.assertEqual(mods[0].patch_offset, 0x48)
        self.assertEqual(mods[2].event_bit, 262)

    def test_empty_list_yields_a_zero_length_slice(self):
        mods, offsets = pwd._parse_modifications(
            [_chunk(1, 0x10), b""], 0x08, 0x100, "<syn>")
        self.assertEqual(len(mods), 1)
        self.assertEqual(offsets, [0, 1, 1])

    def test_partial_chunk_raises(self):
        with self.assertRaises(ParseError) as ctx:
            pwd._parse_modifications([_chunk(1, 0x10)[:3]], 0x00, 0x100, "<syn>")
        self.assertIn("whole number", str(ctx.exception))

    def test_event_bit_high_bit_raises(self):
        # Bit 15 is masked off by the consumer; a row that sets it is a contract
        # change, not something to silently carry.
        with self.assertRaises(ParseError) as ctx:
            pwd._parse_modifications([_chunk(0x8001, 0x10)], 0x00, 0x100, "<syn>")
        self.assertIn("high bit", str(ctx.exception))

    def test_patch_offset_before_the_pool_raises(self):
        with self.assertRaises(ParseError) as ctx:
            pwd._parse_modifications([_chunk(1, 0x02)], 0x08, 0x100, "<syn>")
        self.assertIn("outside the pool", str(ctx.exception))

    def test_patch_offset_past_the_block_raises(self):
        with self.assertRaises(ParseError) as ctx:
            pwd._parse_modifications([_chunk(1, 0x200)], 0x08, 0x100, "<syn>")
        self.assertIn("outside the pool", str(ctx.exception))


class PatchRecordBoundsTests(unittest.TestCase):
    def test_well_formed_records_pass(self):
        block = bytes(4) + _patch(2, 2) + _patch(5, 5)
        mods, _ = pwd._parse_modifications([_chunk(1, 4), _chunk(2, 11)],
                                           4, len(block), "<syn>")
        pwd._assert_patch_records_fit(mods, block, len(block), "<syn>")

    def test_zero_dimension_raises(self):
        block = bytes(4) + _patch(0, 3)
        mods, _ = pwd._parse_modifications([_chunk(1, 4)], 4, len(block), "<syn>")
        with self.assertRaises(ParseError) as ctx:
            pwd._assert_patch_records_fit(mods, block, len(block), "<syn>")
        self.assertIn("zero dimension", str(ctx.exception))

    def test_payload_running_past_the_block_raises(self):
        block = bytes(4) + _patch(8, 8)[:10]  # truncated payload
        mods, _ = pwd._parse_modifications([_chunk(1, 4)], 4, len(block), "<syn>")
        with self.assertRaises(ParseError) as ctx:
            pwd._assert_patch_records_fit(mods, block, len(block), "<syn>")
        self.assertIn("runs past", str(ctx.exception))

    def test_header_running_past_the_block_raises(self):
        block = bytes(6)
        mods, _ = pwd._parse_modifications([_chunk(1, 5)], 4, len(block), "<syn>")
        with self.assertRaises(ParseError) as ctx:
            pwd._assert_patch_records_fit(mods, block, len(block), "<syn>")
        self.assertIn("3-byte header", str(ctx.exception))


class FixedBlockTailTests(unittest.TestCase):
    def test_uniform_tail_is_accepted(self):
        rom = bytes(16) + b"\xff" * 8
        self.assertEqual(pwd._uniform_tail(rom, 0, 16, 24, "<syn>"), (0xFF, 8))

    def test_exactly_full_block_has_no_tail(self):
        self.assertEqual(pwd._uniform_tail(bytes(16), 0, 16, 16, "<syn>"),
                         (None, 0))

    def test_mixed_tail_raises(self):
        rom = bytes(16) + b"\xff\xff\x00\xff"
        with self.assertRaises(ParseError) as ctx:
            pwd._uniform_tail(rom, 0, 16, 20, "<syn>")
        self.assertIn("not uniform padding", str(ctx.exception))

    def test_overflowing_block_raises(self):
        with self.assertRaises(ParseError) as ctx:
            pwd._uniform_tail(bytes(32), 0, 32, 16, "<syn>")
        self.assertIn("block overflow", str(ctx.exception))


# --- Layer 2: synthetic-input edge cases ------------------------------------

_MINI_WORLD_DATA = """\
; ------------------------------------------------------------------------------

.import EventScript_AirshipDeck, EventScript_WorldTent
.import EventScript_AirshipGround, EventScript_EnterPhoenixCave
.import EventScript_EnterKefkasTower, EventScript_EnterGogosLair
.import EventScript_DoomGazeDefeated

.segment "world_data"

        fixed_block $30

; ee/b260
WorldModDataPtrs:
        .faraddr World1ModData
        .faraddr World2ModData
        .faraddr WorldModDataEnd

; ee/b269
VehicleEvent_00:
        .faraddr EventScript_AirshipDeck - EventScript

VehicleEvent_01:
        .faraddr EventScript_WorldTent - EventScript

VehicleEvent_02:
        .faraddr EventScript_AirshipGround - EventScript

VehicleEvent_03:
        .faraddr EventScript_EnterPhoenixCave - EventScript

VehicleEvent_04:
        .faraddr EventScript_EnterKefkasTower - EventScript

VehicleEvent_05:
        .faraddr EventScript_EnterGogosLair - EventScript

VehicleEvent_06:
        .faraddr EventScript_DoomGazeDefeated - EventScript

        end_fixed_block

.segment "world_sine"
; python code to generate this table for x = [0...270]

; ef/fef1
WorldSineTbl:
        .incbin "world_sine.dat"
"""

_MINI_WORLD_MOD = """\
; unrelated code above the segment is ignored
SomeRoutine:
        rts

.pushseg
.segment "world_mod"

        fixed_block $0500

; ce/f600
World1ModData:
        .incbin "world_1_mod.dat"
World2ModData:
        .incbin "world_2_mod.dat"
WorldModDataEnd:

; ce/f648
WorldModTiles:
        .incbin "world_mod_tiles.dat"

        end_fixed_block

.popseg
"""

_MINI_EVENT_MAIN = """\
.export EventScript_PartyDefeated := PartyDefeated
.export EventScript_WorldTent := WorldTent_ext
.export EventScript_AirshipDeck := AirshipDeck

; ca/004f
.proc WorldTent_ext
        return
.endproc

; ca/0068
.proc AirshipDeck
        return
.endproc  ; AirshipDeck

; a proc with no address comment resolves to nothing
.proc Undocumented
        return
.endproc
"""


class WorldDataGrammarTests(unittest.TestCase):
    def test_parses_pointers_events_and_incbin(self):
        path = _write_tmp(_MINI_WORLD_DATA)
        try:
            out = pwd.parse_world_data_asm(path)
        finally:
            os.unlink(path)
        self.assertEqual(tuple(out.mod_ptr_symbols), pwd.MOD_PTR_SYMBOLS)
        self.assertEqual(len(out.vehicle_events), pwd.VEHICLE_EVENT_COUNT)
        self.assertEqual(out.vehicle_events[0],
                         [0, "EventScript_AirshipDeck"])
        self.assertEqual(out.vehicle_events[6],
                         [6, "EventScript_DoomGazeDefeated"])
        self.assertEqual(out.sine_file, "world_sine.dat")
        self.assertEqual(out.data_block_size, pwd.WORLD_DATA_BLOCK_SIZE)

    def test_unknown_line_raises_with_a_line_number(self):
        path = _write_tmp(_MINI_WORLD_DATA + "        lda #$00\n")
        try:
            with self.assertRaises(ParseError) as ctx:
                pwd.parse_world_data_asm(path)
        finally:
            os.unlink(path)
        self.assertIn("unexpected line", str(ctx.exception))

    def test_missing_vehicle_event_raises(self):
        trimmed = _MINI_WORLD_DATA.replace(
            "VehicleEvent_06:\n"
            "        .faraddr EventScript_DoomGazeDefeated - EventScript\n", "")
        path = _write_tmp(trimmed)
        try:
            with self.assertRaises(ParseError) as ctx:
                pwd.parse_world_data_asm(path)
        finally:
            os.unlink(path)
        self.assertIn("vehicle events 6", str(ctx.exception))

    def test_reordered_pointer_target_raises(self):
        swapped = _MINI_WORLD_DATA.replace(
            "        .faraddr World1ModData\n        .faraddr World2ModData\n",
            "        .faraddr World2ModData\n        .faraddr World1ModData\n")
        path = _write_tmp(swapped)
        try:
            with self.assertRaises(ParseError) as ctx:
                pwd.parse_world_data_asm(path)
        finally:
            os.unlink(path)
        self.assertIn("WorldModDataPtrs targets", str(ctx.exception))

    def test_changed_fixed_block_raises(self):
        resized = _MINI_WORLD_DATA.replace("fixed_block $30",
                                           "fixed_block $40")
        path = _write_tmp(resized)
        try:
            with self.assertRaises(ParseError) as ctx:
                pwd.parse_world_data_asm(path)
        finally:
            os.unlink(path)
        self.assertIn("world_data fixed_block", str(ctx.exception))


class WorldModSegmentTests(unittest.TestCase):
    def test_parses_the_segment_and_ignores_the_rest_of_the_file(self):
        path = _write_tmp(_MINI_WORLD_MOD)
        try:
            size, entries = pwd.parse_world_mod_segment(path)
        finally:
            os.unlink(path)
        self.assertEqual(size, pwd.WORLD_MOD_BLOCK_SIZE)
        self.assertEqual([e[0] for e in entries],
                         ["World1ModData", "World2ModData", "WorldModDataEnd",
                          "WorldModTiles"])
        self.assertEqual(entries[2][1], None)  # end label has no incbin
        self.assertEqual(entries[3][1], pwd.MOD_POOL_FILE)

    def test_reordered_layout_raises(self):
        swapped = _MINI_WORLD_MOD.replace("world_1_mod.dat", "world_X_mod.dat")
        path = _write_tmp(swapped)
        try:
            with self.assertRaises(ParseError) as ctx:
                pwd.parse_world_mod_segment(path)
        finally:
            os.unlink(path)
        self.assertIn("world_mod segment layout", str(ctx.exception))

    def test_unknown_line_inside_the_segment_raises(self):
        broken = _MINI_WORLD_MOD.replace(
            "WorldModDataEnd:\n", "WorldModDataEnd:\n        .word $1234\n")
        path = _write_tmp(broken)
        try:
            with self.assertRaises(ParseError) as ctx:
                pwd.parse_world_mod_segment(path)
        finally:
            os.unlink(path)
        self.assertIn("world_mod segment", str(ctx.exception))


class EventProcAddressTests(unittest.TestCase):
    def test_alias_plus_address_comment_resolves(self):
        path = _write_tmp(_MINI_EVENT_MAIN)
        try:
            addrs = pwd.parse_event_proc_addresses(path)
        finally:
            os.unlink(path)
        self.assertEqual(addrs["EventScript_WorldTent"], 0xCA004F)
        self.assertEqual(addrs["EventScript_AirshipDeck"], 0xCA0068)

    def test_export_without_a_documented_proc_is_absent(self):
        path = _write_tmp(_MINI_EVENT_MAIN)
        try:
            addrs = pwd.parse_event_proc_addresses(path)
        finally:
            os.unlink(path)
        self.assertNotIn("EventScript_PartyDefeated", addrs)


class VehicleEventNamingTests(unittest.TestCase):
    def test_every_source_target_has_an_enumerator(self):
        path = _write_tmp(_MINI_WORLD_DATA)
        try:
            out = pwd.parse_world_data_asm(path)
        finally:
            os.unlink(path)
        for _ordinal, symbol in out.vehicle_events:
            self.assertIn(symbol, pwd.VEHICLE_EVENT_NAMES)

    def test_no_stale_enumerator_entries(self):
        self.assertEqual(len(pwd.VEHICLE_EVENT_NAMES), pwd.VEHICLE_EVENT_COUNT)


# --- Layer 3: end-to-end against the real contract + ROM ---------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    asm = os.path.join(root, "src", "world", "world_data.asm")
    dat = os.path.join(root, "src", "world", "world_1_mod.dat")
    return root if os.path.isfile(asm) and os.path.isfile(dat) else None


def _rom_available(root):
    return root is not None and common.find_vanilla_rom(root) is not None


@unittest.skipUnless(_rom_available(_find_source_root()),
                     "original-src world source and/or vanilla ROM absent")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _find_source_root()
        cls.res = pwd.load_and_resolve(cls.root)

    def test_corpus_extents(self):
        self.assertEqual(len(self.res.modifications), 18)
        self.assertEqual(self.res.mod_offsets, [0, 15, 18])
        self.assertEqual(len(self.res.vehicle_offsets), pwd.VEHICLE_EVENT_COUNT)
        self.assertEqual(len(self.res.sine), pwd.WORLD_SINE_LENGTH)
        self.assertEqual(self.res.pool_length, 1182)

    def test_block_accounting_and_padding(self):
        # 60 + 12 + 1,182 = 1,254 inside fixed_block $0500 (1,280).
        self.assertEqual(self.res.mod_pad, (0xFF, 26))
        # 3 + 7 faraddrs = 30 B inside fixed_block $30 (48).
        self.assertEqual(self.res.data_pad, (0xFF, 18))

    def test_first_chunk_of_each_world(self):
        first = self.res.modifications[0]
        self.assertEqual(first.world, 0)
        self.assertEqual(first.event_bit, 0x010B)
        self.assertEqual(first.patch_offset, 0x0048)  # == WorldModTiles + 0
        second_world = self.res.modifications[15]
        self.assertEqual(second_world.world, 1)
        self.assertEqual(second_world.event_bit, 0x0106)
        self.assertEqual(second_world.patch_offset, 0x04D3)

    def test_every_patch_offset_lands_in_the_pool(self):
        pool_start = self.res.mod_offsets[-1] and 0x48  # 60 + 12
        for mod in self.res.modifications:
            self.assertGreaterEqual(mod.patch_offset, pool_start)
            self.assertLess(mod.patch_offset, 0x48 + self.res.pool_length)

    def test_no_event_bit_sets_the_masked_high_bit(self):
        for mod in self.res.modifications:
            self.assertEqual(mod.event_bit & 0x8000, 0)

    def test_vehicle_events_resolve_in_rom_order(self):
        by_name = {symbol: offset
                   for _o, symbol, offset in self.res.vehicle_offsets}
        self.assertEqual(by_name["EventScript_AirshipDeck"], 0x00068)
        self.assertEqual(by_name["EventScript_WorldTent"], 0x0004F)
        self.assertEqual(by_name["EventScript_AirshipGround"], 0x00059)
        self.assertEqual(by_name["EventScript_EnterPhoenixCave"], 0x00088)
        self.assertEqual(by_name["EventScript_EnterKefkasTower"], 0x0007F)
        self.assertEqual(by_name["EventScript_EnterGogosLair"], 0x0008F)
        self.assertEqual(by_name["EventScript_DoomGazeDefeated"], 0x00096)

    def test_sine_endpoints(self):
        self.assertEqual(self.res.sine[0], 0)
        self.assertEqual(self.res.sine[90], 255)
        self.assertEqual(self.res.sine[180], 0)
        self.assertEqual(self.res.sine[270], 255)

    def test_emitters_render_every_row(self):
        mods = pwd.render_modifications_inc(self.res.modifications)
        self.assertEqual(mods.count("WorldMapModification{"),
                         len(self.res.modifications))
        self.assertIn("EventBitRef::of(267)", mods)
        offsets = pwd.render_mod_offsets_inc(self.res.mod_offsets)
        self.assertEqual(offsets.count("WorldModDataEntry{"),
                         len(self.res.mod_offsets))
        events = pwd.render_vehicle_events_inc(self.res.vehicle_offsets)
        self.assertEqual(events.count("WorldVehicleEvent::"),
                         pwd.VEHICLE_EVENT_COUNT)
        self.assertIn("EventScriptRef::at(0x00068)", events)
        fixture = pwd.render_fixture(self.res)
        self.assertIn("kExpectedWorldMapModifications", fixture)
        self.assertIn("kExpectedWorldSine", fixture)

    def test_every_sine_row_carries_its_degree_as_a_typed_field(self):
        # Identity is a field on every row, never the array position: one row
        # per degree in both the engine table and the fixture.
        inc = pwd.render_sine_inc(self.res.sine)
        self.assertEqual(inc.count(".index ="), pwd.WORLD_SINE_LENGTH)
        self.assertEqual(inc.count(".amplitude ="), pwd.WORLD_SINE_LENGTH)
        self.assertIn("{ .index =   0, .amplitude =   0 },", inc)
        self.assertIn("{ .index =  90, .amplitude = 255 },", inc)
        self.assertIn("{ .index = 270, .amplitude = 255 },", inc)
        fixture = pwd.render_fixture(self.res)
        self.assertEqual(fixture.count(".amplitude ="), pwd.WORLD_SINE_LENGTH)

    def test_every_offset_row_carries_its_world_id_as_a_typed_field(self):
        fixture = pwd.render_fixture(self.res)
        self.assertEqual(fixture.count(".firstChunk ="), len(self.res.mod_offsets))
        self.assertIn("{ .index = 1, .firstChunk = 15 },", fixture)


if __name__ == "__main__":
    unittest.main()
