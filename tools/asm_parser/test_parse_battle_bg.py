#!/usr/bin/env python3
"""Unit tests for parse_battle_bg.py.

Three layers, matching the parser test discipline: the byte resolver against
hand-made expressions, synthetic assembly fragments exercising the grammar's
edges and every error path, and an end-to-end pass over the real disassembly
(skipped when it is not present) that pins the corpus's own counts and quirks.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_battle_bg as pbb  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    """The disassembly root, if this machine has one."""
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "gfx", "battle_bg.asm")):
        return candidate
    return None


# A stand-in for the asset-id enums, small enough to reason about.
_FAKE_INC = """\
.enum BATTLE_BG
        FIRST
        SECOND
        DEFAULT = $ff
.endenum

.enum BATTLE_BG_GFX
        BLOCK_ZERO
        BLOCK_ONE
        NONE = $ff
.endenum

.enum BATTLE_BG_TILES
        MAP_ZERO
        MAP_ONE
.endenum

.enum BATTLE_BG_PAL
        PAL_ZERO
        PAL_ONE
.endenum
"""

# Two whole records in the real table's shape.
_FAKE_ASM = """\
.segment "battle_bg"

BattleBGProp:
        .byte BATTLE_BG_GFX::BLOCK_ZERO
        .byte BATTLE_BG_GFX::BLOCK_ONE
        .byte BATTLE_BG_GFX::NONE
        .byte BATTLE_BG_TILES::MAP_ZERO
        .byte BATTLE_BG_TILES::MAP_ZERO
        .byte BATTLE_BG_PAL::PAL_ZERO

        .byte BATTLE_BG_GFX::BLOCK_ONE | $80
        .byte BATTLE_BG_GFX::NONE
        .byte BATTLE_BG_GFX::BLOCK_ZERO
        .byte BATTLE_BG_TILES::MAP_ONE
        .byte BATTLE_BG_TILES::MAP_ONE
        .byte BATTLE_BG_PAL::PAL_ONE | $80

AfterTheTable:
        .byte $00
"""


def _write(tmpdir, rel_parts, text):
    path = os.path.join(tmpdir, *rel_parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


class _Fixture(object):
    """A throwaway source tree holding one .inc and one .asm."""

    def __init__(self, tmpdir, asm_text=_FAKE_ASM, inc_text=_FAKE_INC):
        self.inc = _write(tmpdir, ("include", "gfx", "battle_bg.inc"), inc_text)
        self.asm = _write(tmpdir, ("src", "gfx", "battle_bg.asm"), asm_text)
        self.symbols = pbb._Symbols(self.inc)

    def parse(self):
        return pbb.parse_battle_bg_prop(self.asm, self.symbols)


class ByteResolverTest(unittest.TestCase):
    """One `.byte` expression at a time, at the position it belongs to."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.fx = _Fixture(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def _resolve(self, expr, position):
        return pbb._resolve_byte(expr, position, self.fx.symbols, "x.asm", 1)

    def test_plain_symbol(self):
        field = self._resolve("BATTLE_BG_GFX::BLOCK_ONE", 0)
        self.assertEqual(field.value, 1)
        self.assertEqual(field.member, "BLOCK_ONE")
        self.assertFalse(field.flag)
        self.assertFalse(field.from_literal)

    def test_flagged_symbol_sets_bit_seven(self):
        field = self._resolve("BATTLE_BG_GFX::BLOCK_ONE | $80", 0)
        self.assertEqual(field.value, 0x81)
        self.assertTrue(field.flag)

    def test_palette_may_be_flagged(self):
        field = self._resolve("BATTLE_BG_PAL::PAL_ONE | $80", 5)
        self.assertEqual(field.value, 0x81)
        self.assertTrue(field.flag)

    def test_bare_literal_resolves_to_the_member_holding_that_value(self):
        # 0 is BLOCK_ZERO, never NONE — NONE is $FF.
        field = self._resolve("0", 1)
        self.assertEqual(field.value, 0)
        self.assertEqual(field.member, "BLOCK_ZERO")
        self.assertTrue(field.from_literal)

    def test_wrong_scope_for_the_position_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve("BATTLE_BG_TILES::MAP_ZERO", 0)
        self.assertIn("this position names", str(ctx.exception))

    def test_flag_on_an_unflagged_position_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve("BATTLE_BG_GFX::BLOCK_ONE | $80", 1)
        self.assertIn("only graphics 1 and the palette", str(ctx.exception))

    def test_unexpected_flag_value_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve("BATTLE_BG_GFX::BLOCK_ONE | $40", 0)
        self.assertIn("unexpected flag", str(ctx.exception))

    def test_unknown_member_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve("BATTLE_BG_GFX::NOT_A_BLOCK", 0)
        self.assertIn("unknown", str(ctx.exception))

    def test_unnameable_literal_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve("77", 0)
        self.assertIn("unnameable", str(ctx.exception))

    def test_flagging_a_member_that_already_sets_bit_seven_is_lossy(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve("BATTLE_BG_GFX::NONE | $80", 0)
        self.assertIn("lossy", str(ctx.exception))


class TableWalkTest(unittest.TestCase):
    """The walker, over whole synthetic tables."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self._tmp.cleanup()

    def test_two_records_parse_with_their_flags(self):
        records = _Fixture(self._tmp.name).parse()
        self.assertEqual(len(records), 2)
        self.assertEqual(records[0].bytes, [0x00, 0x01, 0xFF, 0x00, 0x00, 0x00])
        self.assertEqual(records[1].bytes, [0x81, 0xFF, 0x00, 0x01, 0x01, 0x81])
        self.assertEqual(records[0].name, "FIRST")
        self.assertEqual(records[1].name, "SECOND")

    def test_the_next_label_ends_the_table(self):
        # AfterTheTable's own .byte must not be swept into the last record.
        records = _Fixture(self._tmp.name).parse()
        self.assertEqual(sum(len(r.bytes) for r in records), 12)

    def test_a_partial_record_is_an_error(self):
        text = _FAKE_ASM.replace("        .byte BATTLE_BG_PAL::PAL_ONE | $80\n", "")
        with self.assertRaises(ParseError) as ctx:
            _Fixture(self._tmp.name, asm_text=text).parse()
        self.assertIn("not a whole number", str(ctx.exception))

    def test_record_count_must_match_the_index_space(self):
        # Three records against a two-member BATTLE_BG.
        extra = ("        .byte BATTLE_BG_GFX::BLOCK_ZERO\n" * 3
                 + "        .byte BATTLE_BG_TILES::MAP_ZERO\n" * 2
                 + "        .byte BATTLE_BG_PAL::PAL_ZERO\n")
        text = _FAKE_ASM.replace("AfterTheTable:", extra + "AfterTheTable:")
        with self.assertRaises(ParseError) as ctx:
            _Fixture(self._tmp.name, asm_text=text).parse()
        self.assertIn("index space", str(ctx.exception))

    def test_an_uncovered_directive_stops_the_run(self):
        text = _FAKE_ASM.replace("        .byte BATTLE_BG_PAL::PAL_ZERO\n",
                                 "        .word $1234\n")
        with self.assertRaises(ParseError) as ctx:
            _Fixture(self._tmp.name, asm_text=text).parse()
        self.assertIn("grammar not covered", str(ctx.exception))

    def test_a_missing_table_is_an_error(self):
        text = _FAKE_ASM.replace("BattleBGProp:", "SomethingElse:")
        with self.assertRaises(ParseError) as ctx:
            _Fixture(self._tmp.name, asm_text=text).parse()
        self.assertIn("not found", str(ctx.exception))


class RomAssertTest(unittest.TestCase):
    """The cartridge comparison reports the first disagreeing byte."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.records = _Fixture(self._tmp.name).parse()

    def tearDown(self):
        self._tmp.cleanup()

    def _rom_with(self, payload):
        base = common.hirom_file_offset(pbb.BATTLE_BG_PROP_SNES)
        rom = bytearray(base + len(payload))
        rom[base:base + len(payload)] = payload
        return bytes(rom)

    def test_identical_bytes_pass(self):
        payload = bytes(b for r in self.records for b in r.bytes)
        pbb.assert_rom(self._rom_with(payload), self.records, "x.asm")

    def test_a_single_differing_byte_is_reported_with_its_field(self):
        payload = bytearray(b for r in self.records for b in r.bytes)
        payload[8] ^= 0xFF          # record 1, byte 2 -> graphics3
        with self.assertRaises(ParseError) as ctx:
            pbb.assert_rom(self._rom_with(payload), self.records, "x.asm")
        message = str(ctx.exception)
        self.assertIn("ROM MISMATCH", message)
        self.assertIn("graphics3", message)

    def test_a_short_cartridge_is_reported(self):
        with self.assertRaises(ParseError) as ctx:
            pbb.assert_rom(b"\x00" * 4, self.records, "x.asm")
        self.assertIn("ROM MISMATCH", str(ctx.exception))


@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):
    """The real table: its counts, its flags and its one grammar oddity."""

    @classmethod
    def setUpClass(cls):
        root = _source_root()
        cls.symbols = pbb._Symbols(
            os.path.join(root, "include", "gfx", "battle_bg.inc"))
        cls.records = pbb.parse_battle_bg_prop(
            os.path.join(root, "src", "gfx", "battle_bg.asm"), cls.symbols)

    def test_the_corpus_is_fifty_six_records(self):
        self.assertEqual(len(self.records), 56)
        for record in self.records:
            self.assertEqual(len(record.bytes), 6)

    def test_every_record_is_named_by_its_position(self):
        for index, record in enumerate(self.records):
            self.assertEqual(record.index, index)
            self.assertIsNotNone(record.name)

    def test_the_flag_counts_are_pinned(self):
        self.assertEqual(sum(1 for r in self.records if r.fields[0].flag), 30)
        self.assertEqual(sum(1 for r in self.records if r.fields[5].flag), 6)

    def test_three_records_have_no_first_graphics_block(self):
        empty = [r.index for r in self.records if r.bytes[0] == 0xFF]
        self.assertEqual(empty, [8, 19, 30])

    def test_the_bare_literal_rows_are_pinned(self):
        literals = [(r.index, i) for r in self.records
                    for i, f in enumerate(r.fields) if f.from_literal]
        self.assertEqual(literals, [(31, 1), (31, 2), (44, 2), (46, 2)])
        # Each resolves to block 0, which is a real block and not the sentinel.
        for index, position in literals:
            self.assertEqual(self.records[index].bytes[position], 0)


@unittest.skipIf(_source_root() is None, "disassembly not present")
class CartridgeTest(unittest.TestCase):
    """The assembled table against the vanilla cartridge, end to end."""

    def test_the_whole_table_matches_the_cartridge(self):
        root = _source_root()
        symbols = pbb._Symbols(
            os.path.join(root, "include", "gfx", "battle_bg.inc"))
        asm = os.path.join(root, "src", "gfx", "battle_bg.asm")
        records = pbb.parse_battle_bg_prop(asm, symbols)
        pbb.assert_rom(common.load_vanilla_rom(root), records, asm)


if __name__ == "__main__":
    unittest.main()
