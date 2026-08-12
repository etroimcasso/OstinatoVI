#!/usr/bin/env python3
"""Unit tests for parse_level_up.py.

Three layers per the parser-test discipline:
  1. pure helpers (the language-fork reader);
  2. synthetic asm fragments (both branches, malformed forks, length guards);
  3. end-to-end against the real original-src event.asm + const.inc (skipped
     cleanly when the rip/clone is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_level_up as plu
from common import ParseError


def _write_asm(text):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


# --- Layer 1 + 2: the language fork over synthetic input ---------------------

class LangForkTests(unittest.TestCase):
    GOOD = ("LevelUpMP:\n\n"
            ".if LANG_EN\n"
            "        .byte   4,4,5  ; 2-4\n"
            ".else\n"
            "        .byte   5,6,7  ; 2-4\n"
            ".endif\n")

    def _read(self, text, count=3, label="LevelUpMP"):
        path = _write_asm(text)
        try:
            return plu.read_lang_forked_byte_run(path, open(path).readlines(),
                                                 label, count)
        finally:
            os.unlink(path)

    def test_reads_both_branches(self):
        en, jp = self._read(self.GOOD)
        self.assertEqual(en, [4, 4, 5])
        self.assertEqual(jp, [5, 6, 7])

    def test_branch_length_mismatch_errors(self):
        text = self.GOOD.replace("        .byte   5,6,7  ; 2-4\n",
                                 "        .byte   5,6  ; 2-3\n")
        with self.assertRaises(ParseError):
            self._read(text)

    def test_missing_fork_errors(self):
        with self.assertRaises(ParseError):
            self._read("LevelUpMP:\n        .byte   4,4,5\n")

    def test_unexpected_directive_inside_fork_errors(self):
        text = self.GOOD.replace("        .byte   5,6,7  ; 2-4\n",
                                 "        .word   5,6,7\n")
        with self.assertRaises(ParseError):
            self._read(text)

    def test_wrong_count_errors(self):
        with self.assertRaises(ParseError):
            self._read(self.GOOD, count=4)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "field", "event.asm")):
        return root
    return None


@unittest.skipUnless(_find_source_root(), "original-src event.asm not present")
class EndToEndTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.t = plu.read_tables({
            "event_asm": os.path.join(root, "src", "field", "event.asm"),
            "battle_main_asm": os.path.join(root, "src", "battle",
                                            "battle_main.asm"),
            "const_inc": os.path.join(root, "include", "const.inc"),
        })

    def test_progression_lengths(self):
        for key in ("exp", "hp", "mp_en", "mp_jp"):
            self.assertEqual(len(self.t[key]), plu.LEVEL_ROWS, key)

    def test_exp_ramp_and_outlier(self):
        self.assertEqual(self.t["exp"][0], 4)        # level 2
        self.assertEqual(self.t["exp"][-2], 9603)    # level 98
        self.assertEqual(self.t["exp"][-1], 11111)   # level 99, the outlier

    def test_mp_curves_differ_by_language(self):
        self.assertEqual(self.t["mp_en"][0], 4)
        self.assertEqual(self.t["mp_jp"][0], 5)
        self.assertNotEqual(self.t["mp_en"], self.t["mp_jp"])

    def test_hp_curve(self):
        self.assertEqual(self.t["hp"][0], 11)
        self.assertEqual(self.t["hp"][-1], 88)

    def test_ability_levels_and_names(self):
        self.assertEqual(self.t["bushido"], [1, 6, 12, 15, 24, 34, 44, 70])
        self.assertEqual(self.t["blitz"], [1, 6, 10, 15, 23, 30, 42, 70])
        self.assertEqual(self.t["bushido_names"][0], "DISPATCH")
        self.assertEqual(self.t["bushido_names"][-1], "CLEAVE")
        self.assertEqual(self.t["blitz_names"][0], "PUMMEL")
        self.assertEqual(self.t["blitz_names"][-1], "BUM_RUSH")

    def test_learn_flags_are_cumulative(self):
        self.assertEqual(self.t["learn_flags"],
                         [0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF])

    def test_level_modifiers_are_signed(self):
        self.assertEqual(self.t["level_mod_signed"], [0, 2, 5, -3])
        self.assertEqual(self.t["mod_names"],
                         ["NORMAL", "HIGH", "VERY_HIGH", "LOW"])


if __name__ == "__main__":
    unittest.main()
