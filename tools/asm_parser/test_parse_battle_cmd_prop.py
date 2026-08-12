#!/usr/bin/env python3
"""Unit tests for parse_battle_cmd_prop.py.

Three layers per the parser-test discipline:
  1. pure helpers (arg splitting, member resolution);
  2. synthetic asm inputs (row parsing, count/padding/unknown-member guards);
  3. end-to-end against the real original-src battle_cmd_prop.asm + const.inc
     (skipped cleanly when the rip/clone is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import common
import parse_battle_cmd_prop as pbcp
import parse_const_enums as pce
from common import ParseError

_FLAGS = {"NONE": 0, "GOGO": 1, "MIMIC": 2, "IMP": 4, "UNKNOWN": 8}
_TARGETS = {"MANUAL": 1, "ONE_SIDE": 2, "INIT_MASK": 0x0C, "INIT_SINGLE": 0,
            "INIT_ALL": 4, "INIT_GROUP": 8, "INIT_HALF": 0x0C,
            "AUTO_CONFIRM": 0x10, "MULTI_TARGET": 0x20, "ENEMY": 0x40,
            "ROULETTE": 0x80, "SELF": 2, "MENU": 0xFF}


def _write_asm(text):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


def _synthetic_asm(rows):
    """rows: list of (flags_arg, target_arg). Emits a well-formed 32-row file
    with the standard '; $NN: name' documenting comments."""
    lines = [".export BattleCmdProp", "BattleCmdProp:"]
    for i, (flags, target) in enumerate(rows):
        lines.append("; ${:02x}: cmd{}".format(i, i))
        lines.append("make_battle_cmd_prop {}, {}".format(flags, target))
    return "\n".join(lines) + "\n"


# --- Layer 1: pure helpers ---------------------------------------------------

class SplitArgsTests(unittest.TestCase):
    def test_single_single(self):
        self.assertEqual(pbcp._split_args("f", 1, "NONE, SELF"), ("NONE", "SELF"))

    def test_bracelist_then_single(self):
        self.assertEqual(
            pbcp._split_args("f", 1, "{GOGO, MIMIC, IMP}, MENU"),
            ("{GOGO, MIMIC, IMP}", "MENU"))

    def test_both_bracelists(self):
        a, b = pbcp._split_args("f", 1, "{GOGO, MIMIC}, {MANUAL, ENEMY}")
        self.assertEqual((a, b), ("{GOGO, MIMIC}", "{MANUAL, ENEMY}"))

    def test_missing_comma_errors(self):
        with self.assertRaises(ParseError):
            pbcp._split_args("f", 1, "{GOGO MIMIC}")


class MembersTests(unittest.TestCase):
    def test_single(self):
        self.assertEqual(
            pbcp._members("f", 1, "NONE", "BATTLE_CMD_FLAG", _FLAGS), ["NONE"])

    def test_bracelist(self):
        self.assertEqual(
            pbcp._members("f", 1, "{GOGO, MIMIC, IMP}", "BATTLE_CMD_FLAG",
                          _FLAGS),
            ["GOGO", "MIMIC", "IMP"])

    def test_unknown_member_errors(self):
        with self.assertRaises(ParseError):
            pbcp._members("f", 1, "{GOGO, BOGUS}", "BATTLE_CMD_FLAG", _FLAGS)

    def test_empty_errors(self):
        with self.assertRaises(ParseError):
            pbcp._members("f", 1, "{}", "BATTLE_CMD_FLAG", _FLAGS)


# --- Layer 2: synthetic inputs -----------------------------------------------

class ReadRowsTests(unittest.TestCase):
    def _full_rows(self):
        # 30 real commands (row 0 exercises a 4-flag list) + 2 NONE/MENU pads.
        rows = [("{GOGO, MIMIC, IMP, UNKNOWN}", "{MANUAL, INIT_SINGLE, ENEMY}")]
        rows += [("NONE", "MENU")] * 29
        rows += [("NONE", "MENU"), ("NONE", "MENU")]
        return rows

    def test_happy_path_bytes(self):
        path = _write_asm(_synthetic_asm(self._full_rows()))
        try:
            rows = pbcp.read_rows(path, _FLAGS, _TARGETS)
        finally:
            os.unlink(path)
        self.assertEqual(len(rows), 32)
        # row 0: GOGO|MIMIC|IMP|UNKNOWN = 0x0F ; MANUAL|INIT_SINGLE|ENEMY = 0x41
        self.assertEqual(rows[0][3], 0x0F)
        self.assertEqual(rows[0][4], 0x41)

    def test_wrong_count_errors(self):
        path = _write_asm(_synthetic_asm([("NONE", "MENU")] * 4))
        try:
            with self.assertRaises(ParseError):
                pbcp.read_rows(path, _FLAGS, _TARGETS)
        finally:
            os.unlink(path)

    def test_bad_padding_errors(self):
        rows = [("NONE", "MENU")] * 30 + [("GOGO", "SELF"), ("NONE", "MENU")]
        path = _write_asm(_synthetic_asm(rows))
        try:
            with self.assertRaises(ParseError):
                pbcp.read_rows(path, _FLAGS, _TARGETS)  # $1E not NONE/MENU pad
        finally:
            os.unlink(path)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle",
                                   "battle_cmd_prop.asm")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src battle_cmd_prop.asm not present")
class EndToEndTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        const_inc = os.path.join(root, "include", "const.inc")
        parsed = common.parse_ca65_constants(const_inc, skip_body_enums=pce.SKIP)
        cls.flags = {m.name: m.value
                     for m in parsed.enum("BATTLE_CMD_FLAG").members}
        cls.targets = {m.name: m.value for m in parsed.enum("TARGET").members}
        cls.rows = pbcp.read_rows(
            os.path.join(root, "src", "battle", "battle_cmd_prop.asm"),
            cls.flags, cls.targets)

    def test_row_count(self):
        self.assertEqual(len(self.rows), 32)

    def test_fight_bytes(self):
        # $00 fight: {GOGO,MIMIC,IMP,UNKNOWN}=0x0F ; {MANUAL,INIT_SINGLE,ENEMY}=0x41
        self.assertEqual(self.rows[0][3], 0x0F)
        self.assertEqual(self.rows[0][4], 0x41)

    def test_padding_rows(self):
        for pad in (0x1E, 0x1F):
            self.assertEqual((self.rows[pad][3], self.rows[pad][4]), (0x00, 0xFF))


if __name__ == "__main__":
    unittest.main()
