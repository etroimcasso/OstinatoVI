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


if __name__ == "__main__":
    unittest.main()
