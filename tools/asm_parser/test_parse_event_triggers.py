#!/usr/bin/env python3
"""Unit tests for parse_event_triggers.py + the shared ROM/HiROM helpers.

Three layers per the parser-test discipline:
  1. pure helpers (HiROM mapping, address-label resolution);
  2. synthetic-input edge cases (the .asm/.inc grammar, macro skipping, error
     paths) on hand-written fragments;
  3. end-to-end against the real original-src + the vanilla ROM, skipped cleanly
     when either is absent.

Run: python3 -m unittest tools.asm_parser.test_parse_event_triggers
Std-lib only.
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_event_triggers as pet  # noqa: E402
from common import ParseError  # noqa: E402


def _write_tmp(text):
    fh = tempfile.NamedTemporaryFile("w", suffix=".asm", delete=False,
                                     encoding="utf-8")
    fh.write(text)
    fh.close()
    return fh.name


# --- Layer 1: pure helpers ---------------------------------------------------

class HiRomTests(unittest.TestCase):
    def test_known_addresses(self):
        self.assertEqual(common.hirom_file_offset(0xC40000), 0x040000)
        self.assertEqual(common.hirom_file_offset(0xD1FA00), 0x11FA00)
        self.assertEqual(common.hirom_file_offset(0xCA0000), 0x0A0000)

    def test_non_hirom_address_raises(self):
        with self.assertRaises(ValueError):
            common.hirom_file_offset(0x7E0000)


class AddrLabelTests(unittest.TestCase):
    def test_address_named_label(self):
        # The label IS the SNES address; offset = addr - EventScript.
        self.assertEqual(common.resolve_event_addr_label("_cae8f4"), 0xE8F4)
        self.assertEqual(common.resolve_event_addr_label("_cb0bb7"), 0x10BB7)
        self.assertEqual(common.resolve_event_addr_label("_ca0000"), 0x0000)

    def test_named_and_malformed_labels_return_none(self):
        self.assertIsNone(common.resolve_event_addr_label("SavePoint"))
        self.assertIsNone(common.resolve_event_addr_label("EventReturn"))
        self.assertIsNone(common.resolve_event_addr_label("_cb0bb"))    # 5 hex
        self.assertIsNone(common.resolve_event_addr_label("_cb0bb7x"))  # trailing


# --- Layer 2: synthetic-input edge cases ------------------------------------

_MINI_TRIGGER = """\
.mac make_event_trigger xy_pos, addr
        .byte xy_pos
        .faraddr addr - EventScript
.endmac

.segment "event_triggers"

EventTriggerPtrs:
        fixed_block $0040
        ptr_tbl EventTrigger
        end_ptr EventTrigger

EventTrigger:

EventTrigger::_0:
        make_event_trigger {10, 20}, _cb0bb7
        make_event_trigger {11, 21}, SavePoint

EventTrigger::_1:

EventTrigger::_2:
        make_event_trigger {12, 22}, _ca0100

EventTrigger::End:
        end_fixed_block
"""

_MINI_MAP_INIT = """\
.export MapInitEvent

.mac map_init_ptr addr
        .faraddr _event_addr addr
.endmac

.segment "map_init_event"

MapInitEvent:
        map_init_ptr EventReturn
        map_init_ptr _cae8f4
        map_init_ptr EventReturn
"""


class TriggerAsmTests(unittest.TestCase):
    def test_grouping_and_offsets(self):
        path = _write_tmp(_MINI_TRIGGER)
        try:
            records, offsets = pet.parse_event_trigger_asm(path, map_slots=3)
        finally:
            os.unlink(path)
        # 3 records total; map 0 has 2, map 1 empty, map 2 has 1.
        self.assertEqual(len(records), 3)
        self.assertEqual(offsets, [0, 2, 2, 3])  # per-map start + end marker
        self.assertEqual((records[0].pos_x, records[0].pos_y), (10, 20))
        self.assertEqual(records[0].label, "_cb0bb7")
        self.assertEqual(records[1].label, "SavePoint")

    def test_record_before_label_errors(self):
        bad = _MINI_TRIGGER.replace("EventTrigger::_0:\n", "")
        path = _write_tmp(bad)
        try:
            with self.assertRaises(ParseError) as ctx:
                pet.parse_event_trigger_asm(path, map_slots=3)
            self.assertIn("before any", str(ctx.exception))
        finally:
            os.unlink(path)

    def test_missing_slot_errors(self):
        # map_slots=4 but the fragment only defines _0.._2.
        path = _write_tmp(_MINI_TRIGGER)
        try:
            with self.assertRaises(ParseError) as ctx:
                pet.parse_event_trigger_asm(path, map_slots=4)
            self.assertIn("without a _N label", str(ctx.exception))
        finally:
            os.unlink(path)

    def test_unexpected_line_errors(self):
        bad = _MINI_TRIGGER.replace(
            "        make_event_trigger {12, 22}, _ca0100\n",
            "        lda #$01\n")
        path = _write_tmp(bad)
        try:
            with self.assertRaises(ParseError) as ctx:
                pet.parse_event_trigger_asm(path, map_slots=3)
            self.assertIn("unexpected line", str(ctx.exception))
        finally:
            os.unlink(path)


class MapInitAsmTests(unittest.TestCase):
    def test_parse_and_count(self):
        path = _write_tmp(_MINI_MAP_INIT)
        try:
            labels = pet.parse_map_init_asm(path, count=3)
        finally:
            os.unlink(path)
        self.assertEqual([l for l, _ in labels],
                         ["EventReturn", "_cae8f4", "EventReturn"])

    def test_count_mismatch_errors(self):
        path = _write_tmp(_MINI_MAP_INIT)
        try:
            with self.assertRaises(ParseError) as ctx:
                pet.parse_map_init_asm(path, count=5)
            self.assertIn("!= expected", str(ctx.exception))
        finally:
            os.unlink(path)


class ResolveUnitTests(unittest.TestCase):
    def test_mismatch_between_label_and_rom_errors(self):
        # ROM says the offset is 0x000005 but the label claims _ca0100 (0x0100).
        rom = bytearray(0x100000)
        rom[0x000005:0x000008] = bytes([0x05, 0x00, 0x00])
        named = {}
        with self.assertRaises(ParseError) as ctx:
            pet._resolve_event_offset(rom, 0x000005, "_ca0100",
                                      pet.EVENT_TRIGGER_NAMED, named, "x.asm")
        self.assertIn("ROM MISMATCH", str(ctx.exception))

    def test_unknown_named_label_errors(self):
        rom = bytearray(0x100000)
        named = {}
        with self.assertRaises(ParseError) as ctx:
            pet._resolve_event_offset(rom, 0x000000, "TotallyUnknownLabel",
                                      pet.EVENT_TRIGGER_NAMED, named, "x.asm")
        self.assertIn("unknown event label", str(ctx.exception))

    def test_named_label_accumulates_offset(self):
        rom = bytearray(0x100000)
        rom[0x10:0x13] = bytes([0xB3, 0x5E, 0x00])  # 0x5eb3
        named = {}
        off = pet._resolve_event_offset(rom, 0x10, "EventReturn",
                                        pet.MAP_INIT_NAMED, named, "x.asm")
        self.assertEqual(off, 0x5EB3)
        self.assertEqual(named["EventReturn"], {0x5EB3})


# --- Layer 3: end-to-end against the real contract + ROM ---------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    asm = os.path.join(root, "src", "event", "event_trigger.asm")
    return root if os.path.isfile(asm) else None


def _rom_available(root):
    return root is not None and common.find_vanilla_rom(root) is not None


@unittest.skipUnless(_rom_available(_find_source_root()),
                     "original-src event source and/or vanilla ROM absent")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _find_source_root()
        cls.res = pet.load_and_resolve(cls.root)

    def test_corpus_extents(self):
        self.assertEqual(len(self.res.trigger_records),
                         pet.EVENT_TRIGGER_RECORD_COUNT)
        self.assertEqual(len(self.res.trigger_offsets),
                         pet.EVENT_TRIGGER_MAP_SLOTS + 1)
        self.assertEqual(self.res.trigger_offsets[-1],
                         pet.EVENT_TRIGGER_RECORD_COUNT)
        self.assertEqual(len(self.res.map_init_offsets), pet.MAP_INIT_COUNT)

    def test_anchor_records(self):
        # Map 0's first trigger and the map-9 SavePoint, plus the resolved
        # EventReturn value — all cross-checked against the ROM at emit time.
        r0 = self.res.trigger_records[self.res.trigger_offsets[0]]
        self.assertEqual((r0.pos_x, r0.pos_y, r0.offset), (179, 71, 0x10BB7))
        r9 = self.res.trigger_records[self.res.trigger_offsets[9]]
        self.assertEqual((r9.pos_x, r9.pos_y, r9.label), (8, 6, "SavePoint"))
        self.assertEqual(r9.offset, 0x29AEB)
        self.assertEqual(self.res.event_return_offset, 0x5EB3)

    def test_empty_slots(self):
        off = self.res.trigger_offsets
        for m in (2, 4, 5, 415):
            self.assertEqual(off[m], off[m + 1], "map {} should be empty".format(m))

    def test_map_init_anchors(self):
        self.assertEqual(self.res.map_init_offsets[0], self.res.event_return_offset)
        self.assertEqual(self.res.map_init_offsets[3], 0xE8F4)
        self.assertEqual(self.res.map_init_offsets[6], 0x129F3)

    def test_fixed_block_tail_is_padding(self):
        fill, pad_len = self.res.trigger_pad
        self.assertEqual((fill, pad_len), (0xFF, 0x12))

    def test_run_check_only_ok(self):
        self.assertEqual(pet.run(self.root, ".", check_only=True), 0)


if __name__ == "__main__":
    unittest.main()
