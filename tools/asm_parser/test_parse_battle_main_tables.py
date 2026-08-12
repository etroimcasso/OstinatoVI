#!/usr/bin/env python3
"""Unit tests for parse_battle_main_tables.py.

Three layers per the parser-test discipline:
  1. pure helpers (byte-run extraction, command-mask decode round-trip, name
     resolution);
  2. synthetic asm fragments (multi-line runs, @addr prefixes, stop-at-label,
     length/round-trip/unknown-member guards);
  3. end-to-end against the real original-src battle_main.asm + const.inc
     (skipped cleanly when the rip/clone is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import common
import parse_battle_main_tables as pbmt
import parse_const_enums as pce
from common import ParseError

# Enough of BATTLE_CMD for the mask/list tests (values 0..29).
_CMD_NAMES = {0: "FIGHT", 2: "MAGIC", 3: "MORPH", 5: "STEAL", 6: "CAPTURE",
              7: "BUSHIDO", 9: "TOOLS", 10: "BLITZ", 11: "RUNIC", 12: "LORE",
              13: "SKETCH", 16: "RAGE", 18: "MIMIC", 19: "DANCE", 20: "ROW",
              22: "JUMP", 23: "X_MAGIC", 24: "GP_RAIN", 26: "HEALTH",
              27: "SHOCK", 29: "MAGITEK"}


def _write_asm(text):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


# --- Layer 1 + 2: helpers over synthetic input -------------------------------

class ByteRunTests(unittest.TestCase):
    def _run(self, text, label, count):
        path = _write_asm(text)
        try:
            return pbmt.byte_run(path, open(path).readlines(), label, count)
        finally:
            os.unlink(path)

    def test_single_line_with_addr_prefix(self):
        text = "Tbl:\n@04d0:  .byte     $ed,$3e,$dd,$2d\n"
        self.assertEqual(self._run(text, "Tbl", 4), [0xED, 0x3E, 0xDD, 0x2D])

    def test_multi_line_run(self):
        text = ("Tbl:\n@067b:  .byte   $10,$10,$20,$00\n"
                "        .byte   $05,$06,$07,$08\n")
        self.assertEqual(self._run(text, "Tbl", 8),
                         [0x10, 0x10, 0x20, 0x00, 0x05, 0x06, 0x07, 0x08])

    def test_stops_at_next_label(self):
        text = ("Tbl:\n@0:  .byte   $01,$02\n\n; comment\nNext:\n"
                "@1:  .byte   $ff\n")
        self.assertEqual(self._run(text, "Tbl", 2), [0x01, 0x02])

    def test_wrong_count_errors(self):
        with self.assertRaises(ParseError):
            self._run("Tbl:\n@0:  .byte $01,$02,$03\n", "Tbl", 4)

    def test_missing_label_errors(self):
        with self.assertRaises(ParseError):
            self._run("Other:\n@0:  .byte $01\n", "Tbl", 1)


class DecodeCommandSetTests(unittest.TestCase):
    def test_roundtrip_confused(self):
        # $ed,$3e,$dd,$2d -> the muddled/charmed command list (GetBitPtr order).
        names = pbmt.decode_command_set("f", "Confused",
                                        [0xED, 0x3E, 0xDD, 0x2D], _CMD_NAMES)
        self.assertEqual(names[:6],
                         ["FIGHT", "MAGIC", "MORPH", "STEAL", "CAPTURE",
                          "BUSHIDO"])

    def test_berserk_bits(self):
        # $41,$00,$41,$20 -> fight, capture, rage, jump, magitek.
        self.assertEqual(
            pbmt.decode_command_set("f", "Berserk", [0x41, 0x00, 0x41, 0x20],
                                    _CMD_NAMES),
            ["FIGHT", "CAPTURE", "RAGE", "JUMP", "MAGITEK"])

    def test_unknown_bit_errors(self):
        # bit 1 (ITEM) is set but absent from the trimmed name map -> escalate.
        with self.assertRaises(ParseError):
            pbmt.decode_command_set("f", "X", [0x02, 0, 0, 0], _CMD_NAMES)


class NamesForTests(unittest.TestCase):
    def test_resolves(self):
        self.assertEqual(
            pbmt.names_for("f", "L", [0x00, 0x10], _CMD_NAMES, "BATTLE_CMD"),
            ["FIGHT", "RAGE"])

    def test_unknown_errors(self):
        with self.assertRaises(ParseError):
            pbmt.names_for("f", "L", [0x01], _CMD_NAMES, "BATTLE_CMD")


# --- Layer 1 + 2: the s2 run readers -----------------------------------------

class WordRunTests(unittest.TestCase):
    def _run(self, text, label, count):
        path = _write_asm(text)
        try:
            return pbmt.word_run(path, open(path).readlines(), label, count)
        finally:
            os.unlink(path)

    def test_decimal_and_hex(self):
        text = "Tbl:\n@0:  .word   4,8\n        .word   $01d7\n"
        self.assertEqual(self._run(text, "Tbl", 3), [4, 8, 0x01D7])

    def test_loword_of_negative(self):
        # ca65 .loword(-10) is the two's-complement low word.
        text = "Tbl:\n@0:  .word   0\n        .word   .loword(-10)\n"
        self.assertEqual(self._run(text, "Tbl", 2), [0, 0xFFF6])

    def test_wrong_count_errors(self):
        with self.assertRaises(ParseError):
            self._run("Tbl:\n@0:  .word 1,2\n", "Tbl", 3)


class ByteRunExtTests(unittest.TestCase):
    def test_low_byte_of_negative(self):
        # ca65 <(-3) is the low byte of -3.
        path = _write_asm("Tbl:\n        .byte   0,2,5,<(-3)\n")
        try:
            self.assertEqual(
                pbmt.byte_run_ext(path, open(path).readlines(), "Tbl", 4),
                [0, 2, 5, 0xFD])
        finally:
            os.unlink(path)


class EnumRunTests(unittest.TestCase):
    NAMES = {"WIND_SONG": 0, "FOREST_SUITE": 1}

    def _run(self, text, count, scope="DANCE", start=None):
        path = _write_asm(text)
        try:
            return pbmt.enum_run(path, open(path).readlines(), "Tbl", count,
                                 scope, self.NAMES, start=start)
        finally:
            os.unlink(path)

    def test_resolves_scoped_tokens(self):
        text = ("Tbl:\n        .byte   DANCE::WIND_SONG\n"
                "        .byte   DANCE::FOREST_SUITE      ; FOREST_WOR\n")
        self.assertEqual(self._run(text, 2),
                         [(0, "WIND_SONG"), (1, "FOREST_SUITE")])

    def test_wrong_scope_errors(self):
        with self.assertRaises(ParseError):
            self._run("Tbl:\n        .byte   ATTACK::WIND_SONG\n", 1)

    def test_unknown_member_errors(self):
        with self.assertRaises(ParseError):
            self._run("Tbl:\n        .byte   DANCE::NOT_A_DANCE\n", 1)

    def test_bare_literal_errors(self):
        with self.assertRaises(ParseError):
            self._run("Tbl:\n        .byte   $00\n", 1)


class SegmentAnchorTests(unittest.TestCase):
    TEXT = ('.pushseg\n.segment "desperation_attack"\n\n'
            '; cf/fea0 desperation attacks (unused)\n'
            '        .byte ATTACK::RIOT_BLADE\n'
            '        .byte ATTACK::NONE\n\n.popseg\n')

    def test_label_less_run_anchors_on_segment(self):
        path = _write_asm(self.TEXT)
        try:
            lines = open(path).readlines()
            start = pbmt.find_segment(path, lines, "desperation_attack")
            self.assertEqual(
                pbmt.enum_run(path, lines, "(desperation)", 2, "ATTACK",
                              {"RIOT_BLADE": 0x94, "NONE": 0xFF}, start=start),
                [(0x94, "RIOT_BLADE"), (0xFF, "NONE")])
        finally:
            os.unlink(path)

    def test_missing_segment_errors(self):
        path = _write_asm(self.TEXT)
        try:
            with self.assertRaises(ParseError):
                pbmt.find_segment(path, open(path).readlines(), "nope")
        finally:
            os.unlink(path)


class SignedConversionTests(unittest.TestCase):
    def test_signed8(self):
        self.assertEqual(pbmt.to_signed8(0x00), 0)
        self.assertEqual(pbmt.to_signed8(0x7F), 127)
        self.assertEqual(pbmt.to_signed8(0xD3), -45)
        self.assertEqual(pbmt.to_signed8(0xFD), -3)

    def test_signed16(self):
        self.assertEqual(pbmt.to_signed16(0x0032), 50)
        self.assertEqual(pbmt.to_signed16(0xFFF6), -10)


class AiCommandNameTests(unittest.TestCase):
    # The three consecutive banners, and the one that names two commands.
    THREE = ("; [ ai script command $f0: use attack ]\n"
             "; [ ai script command $f1: targetting ]\n"
             "; [ ai script command $f2: battle change ]\n")
    SHARED = "; [ ai script command $fe/$ff: end if/end of script ]\n"
    BANNERS = THREE
    ALL = THREE

    def _names(self, text, count, first):
        path = _write_asm(text)
        try:
            return pbmt.read_ai_command_names(path, open(path).readlines(),
                                              count, first)
        finally:
            os.unlink(path)

    def test_derives_names_from_banners(self):
        names = self._names(self.ALL, 3, 0xF0)
        self.assertEqual(names[0xF0], "USE_ATTACK")
        self.assertEqual(names[0xF1], "TARGETTING")
        self.assertEqual(names[0xF2], "BATTLE_CHANGE")

    def test_shared_banner_names_two_commands(self):
        names = self._names(self.SHARED, 2, 0xFE)
        self.assertEqual(names[0xFE], "END_IF")
        self.assertEqual(names[0xFF], "END_OF_SCRIPT")

    def test_missing_command_errors(self):
        # $f2 is never named -> the port would have a gap; escalate.
        two = "".join(self.THREE.splitlines(True)[:2])
        with self.assertRaises(ParseError):
            self._names(two, 3, 0xF0)

    def test_duplicate_command_errors(self):
        with self.assertRaises(ParseError):
            self._names(self.ALL +
                        "; [ ai script command $f0: use attack ]\n", 3, 0xF0)


class ContiguousNamedTests(unittest.TestCase):
    def test_counts_up_to_the_first_gap(self):
        self.assertEqual(pbmt._contiguous_named({0: "A", 1: "B", 3: "D"}, 8), 2)
        self.assertEqual(pbmt._contiguous_named({0: "A"}, 1), 1)
        self.assertEqual(pbmt._contiguous_named({1: "B"}, 4), 0)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle", "battle_main.asm")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src battle_main.asm not present")
class EndToEndTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.t = pbmt.read_tables({
            "battle_main_asm": os.path.join(root, "src", "battle",
                                            "battle_main.asm"),
            "const_inc": os.path.join(root, "include", "const.inc"),
        })

    def test_confused_mask_raw(self):
        self.assertEqual(self.t["confused_raw"], [0xED, 0x3E, 0xDD, 0x2D])

    def test_confused_decode(self):
        self.assertEqual(self.t["confused"][0], "FIGHT")

    def test_rand_id_names(self):
        self.assertEqual(self.t["rand_id_names"][0], "MAGIC")
        self.assertEqual(len(self.t["rand_id_names"]), 10)

    def test_delay_and_padding(self):
        self.assertEqual(len(self.t["delay"]), 30)
        self.assertEqual(self.t["delay"][22], 224)          # JUMP
        self.assertEqual(self.t["delay_raw"][0x1E], 0x00)   # pad
        self.assertEqual(self.t["delay_raw"][0x1F], 0x00)

    def test_targeting_length(self):
        self.assertEqual(len(self.t["target"]), 30)
        self.assertEqual(self.t["target"][0], 0x20)         # FIGHT

    def test_attack_offset_names(self):
        self.assertEqual(self.t["cmd_with_attack_names"][0], "SUMMON")
        self.assertEqual(self.t["attack_offset_names"][0], "RAMUH")


@unittest.skipUnless(_find_source_root(),
                     "original-src battle_main.asm not present")
class EndToEndS2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        here = os.path.dirname(os.path.abspath(__file__))
        cls.t = pbmt.read_s2_tables({
            "battle_main_asm": os.path.join(root, "src", "battle",
                                            "battle_main.asm"),
            "const_inc": os.path.join(root, "include", "const.inc"),
            "battle_bg_inc": os.path.join(root, "include", "gfx",
                                          "battle_bg.inc"),
            "formation_id_h": os.path.join(here, "..", "..", "include",
                                           "ostinato", "formation_id.h"),
        })

    def test_dance_rate_ladder(self):
        self.assertEqual(self.t["dance_rate"], [0x10, 0x30, 0x90])

    def test_equip_evade_is_signed(self):
        self.assertEqual(self.t["equip_evade_signed"][5], 50)
        self.assertEqual(self.t["equip_evade_signed"][6], -10)
        self.assertEqual(self.t["equip_evade_signed"][10], -50)

    def test_final_battle_chain(self):
        self.assertEqual(self.t["final_ids"], [471, 512, 513, 514])
        self.assertEqual(self.t["final_id_names"][-1], "FINAL_KEFKA")

    def test_slot_outcomes_resolve(self):
        self.assertEqual(len(self.t["slot_attack_names"]), 8)
        self.assertEqual(self.t["slot_attack_names"][3], "NONE")  # esper

    def test_modeled_row_counts(self):
        self.assertEqual(self.t["named_dances"], 8)
        self.assertEqual(self.t["named_bgs"], 56)
        self.assertEqual(self.t["named_types"], 7)

    def test_bg_dance_run_is_full_width(self):
        self.assertEqual(len(self.t["bg_dance"]), 64)
        self.assertEqual(self.t["bg_dance"][0][1], "WIND_SONG")

    def test_desperation_run(self):
        self.assertEqual(len(self.t["desperation"]), 14)
        self.assertEqual(self.t["desperation"][0][1], "RIOT_BLADE")
        self.assertEqual(self.t["char_names"][0], "TERRA")

    def test_ai_script_command_names(self):
        self.assertEqual(self.t["ai_script_names"][0xF0], "USE_ATTACK")
        self.assertEqual(self.t["ai_script_names"][0xFF], "END_OF_SCRIPT")
        self.assertEqual(len(self.t["ai_script_names"]), 16)

    def test_throw_tools_pairs(self):
        self.assertEqual(self.t["throw_item_names"][0], "BIO_BLASTER")
        self.assertEqual(self.t["throw_offsets"], [0x27, 0x27, 0x5A, 0x5A, 0x5A])

    def test_magic_order_offsets_are_negative_where_expected(self):
        # White magic's band starts at attack $2D; setting 0 pulls it to 0.
        self.assertEqual(pbmt.to_signed8(self.t["white_order"][0]), -45)
        self.assertEqual(pbmt.to_signed8(self.t["black_order"][0]), 9)


if __name__ == "__main__":
    unittest.main()
