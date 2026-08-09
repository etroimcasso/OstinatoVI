#!/usr/bin/env python3
"""Unit tests for parse_encounters.py.

Three layers per the parser-test discipline:
  1. pure helpers (BATTLE_BG enum read incl. .scope skip, inline-directive
     block extraction, rate-class + range guards, rendering);
  2. synthetic inputs (per-table length/range guards, inline-grammar guards);
  3. end-to-end against the real original-src encounter .dat files +
     field/battle.asm + battle_bg.inc (skipped cleanly when the rip is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import common
import parse_encounters as pe
from common import ParseError


def _write(text, suffix):
    fd, path = tempfile.mkstemp(suffix=suffix)
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


def _write_dat(data):
    fd, path = tempfile.mkstemp(suffix=".dat")
    with os.fdopen(fd, "wb") as fh:
        fh.write(data)
    return path


_BG_INC = """.list off
.ifndef BATTLE_BG_INC
BATTLE_BG_INC = 1

.enum BATTLE_BG
        FIELD_WOB                       ;= $00  ; 0
        FOREST_WOR                      ;= $01  ; 1
        VELDT                           ;= $02  ; 2
        DEFAULT = $ff
.endenum

.scope BattleBG
        ARRAY_LENGTH = BATTLE_BG::VELDT + 1
.endscope

.endif
.list on
"""


# --- Layer 1: pure helpers ---------------------------------------------------

class BattleBgTests(unittest.TestCase):

    def test_reads_enum_past_scope_block(self):
        path = _write(_BG_INC, ".inc")
        try:
            names = pe.read_battle_bg(path)
        finally:
            os.remove(path)
        self.assertEqual(names, [("FIELD_WOB", 0), ("FOREST_WOR", 1),
                                 ("VELDT", 2), ("DEFAULT", 255)])

    def test_missing_enum_raises(self):
        path = _write(".enum OTHER\n    X\n.endenum\n", ".inc")
        try:
            with self.assertRaises(ParseError):
                pe.read_battle_bg(path)
        finally:
            os.remove(path)

    def test_common_skips_scope_blocks(self):
        # The .scope body (ARRAY_LENGTH = BATTLE_BG::VELDT + 1) uses '+' and
        # '::' the emitter does not evaluate; common must skip it wholesale.
        path = _write(_BG_INC, ".inc")
        try:
            parsed = common.parse_ca65_constants(path)
        finally:
            os.remove(path)
        self.assertIsNotNone(parsed.enum("BATTLE_BG"))
        self.assertNotIn("ARRAY_LENGTH", parsed.globals)


_INLINE_ASM = """
WorldBattleBGTbl:
; world of balance
        .byte   BATTLE_BG::FIELD_WOB
        .byte   BATTLE_BG::VELDT
        .byte   BATTLE_BG::FIELD_WOB
        .byte   BATTLE_BG::VELDT
        .byte   BATTLE_BG::FIELD_WOB
        .byte   BATTLE_BG::VELDT
        .byte   BATTLE_BG::FIELD_WOB
        .byte   BATTLE_BG::VELDT
; world of ruin
        .byte   BATTLE_BG::VELDT
        .byte   BATTLE_BG::FIELD_WOB
        .byte   BATTLE_BG::VELDT
        .byte   BATTLE_BG::FIELD_WOB
        .byte   BATTLE_BG::VELDT
        .byte   BATTLE_BG::FIELD_WOB
        .byte   BATTLE_BG::VELDT
        .byte   BATTLE_BG::FIELD_WOB

BattleBGRateTbl:
        .byte   3,2,1,2,3,0,3,3

BattleBGGroupTbl:
        .byte   0,1,2,1,0,3,0,0

WorldBattleRateTbl:
        .word   $00c0,$0060,$0180,$0000
        .word   $0060,$0030,$00c0,$0000
        .word   $0000,$0000,$0000,$0000
        .word   $0000,$0000,$0000,$0000

SubBattleRateTbl:
        .word   $0070,$0040,$0160,$0200
        .word   $0038,$0020,$00b0,$0100
        .word   $0000,$0000,$0000,$0000
        .word   $0000,$0000,$0000,$0000

.proc GetVeldtBattle
        rts
.endproc
"""

_BG_VALUES = {"FIELD_WOB": 0, "VELDT": 6}


class InlineTableTests(unittest.TestCase):

    def _read(self):
        path = _write(_INLINE_ASM, ".asm")
        try:
            return pe.read_inline_tables(path, _BG_VALUES)
        finally:
            os.remove(path)

    def test_all_tables_parse(self):
        t = self._read()
        self.assertEqual(len(t["world_bg"]), 16)
        self.assertEqual(t["world_bg"][0], ("FIELD_WOB", 0))
        self.assertEqual(t["rate_slot"], [3, 2, 1, 2, 3, 0, 3, 3])
        self.assertEqual(t["group_off"], [0, 1, 2, 1, 0, 3, 0, 0])
        self.assertEqual(t["world_rate"][0], 0x00C0)
        self.assertEqual(t["world_rate"][2], 0x0180)
        self.assertEqual(t["sub_rate"][3], 0x0200)

    def test_unknown_battle_bg_symbol_raises(self):
        asm = _INLINE_ASM.replace("BATTLE_BG::VELDT",
                                  "BATTLE_BG::NOT_A_BG", 1)
        path = _write(asm, ".asm")
        try:
            with self.assertRaises(ParseError) as ctx:
                pe.read_inline_tables(path, _BG_VALUES)
            self.assertIn("not a BATTLE_BG enumerator", str(ctx.exception))
        finally:
            os.remove(path)


class DirectiveBlockTests(unittest.TestCase):

    def test_short_block_raises(self):
        lines = ["Foo:\n", "        .byte   1,2\n", "Bar:\n"]
        with self.assertRaises(ParseError):
            pe._directive_block("x", lines, "Foo", ".byte", 8)

    def test_wrong_directive_raises(self):
        lines = ["Foo:\n", "        .word   1,2\n"]
        with self.assertRaises(ParseError) as ctx:
            pe._directive_block("x", lines, "Foo", ".byte", 2)
        self.assertIn("expected", str(ctx.exception))

    def test_missing_label_raises(self):
        with self.assertRaises(ParseError):
            pe._directive_block("x", ["Nope:\n"], "Foo", ".byte", 1)


class RenderTests(unittest.TestCase):

    def test_battle_bg_header(self):
        h = pe.render_battle_bg_h([("FIELD_WOB", 0), ("DEFAULT", 255)])
        self.assertIn("enum class BattleBackgroundId : std::uint8_t {", h)
        self.assertIn("FIELD_WOB", h)
        self.assertIn("= 0xFF,", h)

    def test_group_words_inc_names_formation(self):
        inc = pe.render_group_words_inc(
            [[0, 0x8000 | 1]], ["LOBO", "HORNET"], "RandomBattleGroupEntry",
            "RandomBattleGroup", "rand_battle_group", "CF/4800")
        self.assertIn(".index = 0,", inc)
        self.assertIn("FormationRef::of(FormationId::LOBO)", inc)
        self.assertIn("FormationRef::of(FormationId::HORNET, "
                      "/*randomizePlus3=*/true)", inc)

    def test_index_value_inc_row_shape(self):
        # Flat-table values are indices / packed class bytes, rendered decimal
        # (not opaque bytes) — 9 and the 255 Veldt sentinel, not 0x09 / 0xFF.
        inc = pe.render_index_value_inc(
            [9, 255], "WorldBattleGroupEntry", "world_battle_group",
            "CF/5400", "note")
        self.assertIn("WorldBattleGroupEntry{ .index = 0, .value = 9 },", inc)
        self.assertIn("WorldBattleGroupEntry{ .index = 1, .value = 255 },", inc)

    def test_inline_increments_render_decimal(self):
        inline = {
            "world_bg": [("FIELD_WOB", 0)] * 16,
            "rate_slot": [0] * 8, "group_off": [0] * 8,
            "world_rate": [0x00C0, 0x0060, 0x0180, 0] + [0] * 12,
            "sub_rate": [0] * 16,
        }
        inc = pe.render_inline_tables_inc(inline)
        self.assertIn(".byRate = {{ 192, 96, 384, 0 }}", inc)
        self.assertNotIn("0x00C0", inc)


# --- Layer 2: synthetic structural guards ------------------------------------

class ReadGroupWordsGuards(unittest.TestCase):

    def test_wrong_length_raises(self):
        path = _write_dat(bytes(10))
        try:
            with self.assertRaises(ParseError) as ctx:
                pe.read_group_words(path, 256, 4, "rand_battle_group")
            self.assertIn("wrong artifact", str(ctx.exception))
        finally:
            os.remove(path)

    def test_out_of_range_formation_raises(self):
        data = bytearray(256 * 4 * 2)
        data[0:2] = (600).to_bytes(2, "little")  # >= 576
        path = _write_dat(bytes(data))
        try:
            with self.assertRaises(ParseError) as ctx:
                pe.read_group_words(path, 256, 4, "rand_battle_group")
            self.assertIn(">=", str(ctx.exception))
        finally:
            os.remove(path)

    def test_randomize_flag_word_is_accepted(self):
        data = bytearray(256 * 4 * 2)
        data[0:2] = (0x8000 | 42).to_bytes(2, "little")
        path = _write_dat(bytes(data))
        try:
            rows = pe.read_group_words(path, 256, 4, "rand_battle_group")
        finally:
            os.remove(path)
        self.assertEqual(rows[0][0], 0x8000 | 42)


class ReadRateBytesGuards(unittest.TestCase):

    def test_class_three_raises(self):
        data = bytes([0x03]) + bytes(127)  # first field is class 3
        path = _write_dat(data)
        try:
            with self.assertRaises(ParseError) as ctx:
                pe.read_rate_bytes(path, 128, "world_battle_rate")
            self.assertIn("rate class 3", str(ctx.exception))
        finally:
            os.remove(path)

    def test_classes_zero_to_two_pass(self):
        data = bytes([0x55, 0xAA]) + bytes(126)  # 0b01010101, 0b10101010
        path = _write_dat(data)
        try:
            vals = pe.read_rate_bytes(path, 128, "world_battle_rate")
        finally:
            os.remove(path)
        self.assertEqual(vals[0], 0x55)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "field",
                                   "rand_battle_group.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src encounter .dat files not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.field = os.path.join(root, "src", "field")
        cls.bg = pe.read_battle_bg(
            os.path.join(root, "include", "gfx", "battle_bg.inc"))
        cls.bg_values = {n: v for n, v in cls.bg}
        cls.rand = pe.read_group_words(
            os.path.join(cls.field, "rand_battle_group.dat"), 256, 4,
            "rand_battle_group")
        cls.event = pe.read_group_words(
            os.path.join(cls.field, "event_battle_group.dat"), 256, 2,
            "event_battle_group")
        cls.world_group = pe.read_group_bytes(
            os.path.join(cls.field, "world_battle_group.dat"), 512,
            "world_battle_group")
        cls.sub_group = pe.read_group_bytes(
            os.path.join(cls.field, "sub_battle_group.dat"), 512,
            "sub_battle_group")
        cls.world_rate = pe.read_rate_bytes(
            os.path.join(cls.field, "world_battle_rate.dat"), 128,
            "world_battle_rate")
        cls.sub_rate = pe.read_rate_bytes(
            os.path.join(cls.field, "sub_battle_rate.dat"), 128,
            "sub_battle_rate")
        cls.inline = pe.read_inline_tables(
            os.path.join(cls.field, "battle.asm"), cls.bg_values)

    def test_battle_bg_shape(self):
        self.assertEqual(len(self.bg), 57)
        self.assertEqual(self.bg[0], ("FIELD_WOB", 0))
        self.assertEqual(self.bg[-1], ("DEFAULT", 255))

    def test_corpus_shapes(self):
        self.assertEqual(len(self.rand), 256)
        self.assertEqual(len(self.event), 256)
        self.assertEqual(len(self.world_group), 512)
        self.assertEqual(len(self.sub_group), 512)
        self.assertEqual(len(self.world_rate), 128)
        self.assertEqual(len(self.sub_rate), 128)

    def test_veldt_sector_count(self):
        self.assertEqual(self.world_group.count(pe.VELDT_SECTOR), 28)

    def test_randomize_only_on_group_112(self):
        randomized = [(g, s) for g, row in enumerate(self.rand)
                      for s, w in enumerate(row) if w & 0x8000]
        self.assertEqual([g for g, _ in randomized], [112, 112, 112, 112])
        self.assertFalse(any(w & 0x8000 for row in self.event for w in row))

    def test_event_group_93_first_formation(self):
        self.assertEqual(self.event[93][0] & 0x7FFF, 463)

    def test_inline_tables(self):
        self.assertEqual(self.inline["rate_slot"], [3, 2, 1, 2, 3, 0, 3, 3])
        self.assertEqual(self.inline["group_off"], [0, 1, 2, 1, 0, 3, 0, 0])
        self.assertEqual(self.inline["world_rate"][:4], [192, 96, 384, 0])
        self.assertEqual(self.inline["sub_rate"][:4], [112, 64, 352, 512])
        self.assertEqual(len(self.inline["world_bg"]), 16)
        for name, _ in self.inline["world_bg"]:
            self.assertIn(name, self.bg_values)


if __name__ == "__main__":
    unittest.main()
