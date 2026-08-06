#!/usr/bin/env python3
"""Unit tests for parse_colosseum.py.

Three layers per the parser-test discipline:
  1. pure helpers (row parsing: defaults, hide flag, comment anchor);
  2. synthetic inputs (table walker: label anchor, terminator, grammar
     deviations, row count, one-byte monster guard);
  3. end-to-end against the real original-src colosseum.asm + const.inc
     (colosseum.asm is committed source, so this layer always runs).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_colosseum as pc
from common import ParseError


class _FakeSymbols(object):
    """Just the attributes row parsing consumes."""

    monster_values = {"CHUPON_COLOSSEUM": 0x40, "WART_PUCK": 0x12,
                      "DIDALOS": 0x9E, "WIDE_MONSTER": 0x180}
    item_values = {"ELIXIR": 0xEE, "THIEF_GLOVE": 0xB1, "ILLUMINA": 0x1A,
                   "DIRK": 0x00, "THIEFKNIFE": 0x04, "RAGNAROK": 0x1B}
    item_names = {0: "DIRK", 1: "MITHRILKNIFE", 4: "THIEFKNIFE",
                  27: "RAGNAROK"}


SYM = _FakeSymbols()


# --- Layer 1: row parsing ----------------------------------------------------

class RowParsingTests(unittest.TestCase):

    def test_blank_row_takes_macro_defaults(self):
        rec = pc._parse_row("make_colosseum_prop", "DIRK", 0, SYM, "t.asm", 1)
        self.assertEqual(rec.monster_name, "CHUPON_COLOSSEUM")
        self.assertEqual(rec.prize_name, "ELIXIR")
        self.assertFalse(rec.hidden)
        self.assertEqual(rec.raw, [0x40, 0x40, 0xEE, 0x00])

    def test_two_arg_row(self):
        rec = pc._parse_row("make_colosseum_prop WART_PUCK, THIEF_GLOVE",
                            "THIEFKNIFE", 4, SYM, "t.asm", 1)
        self.assertEqual(rec.raw, [0x12, 0x40, 0xB1, 0x00])
        self.assertFalse(rec.hidden)

    def test_three_arg_row_hides_prize(self):
        rec = pc._parse_row("make_colosseum_prop DIDALOS, ILLUMINA, 1",
                            "RAGNAROK", 27, SYM, "t.asm", 1)
        self.assertEqual(rec.raw, [0x9E, 0x40, 0x1A, 0xFF])
        self.assertTrue(rec.hidden)

    def test_one_arg_row_raises(self):
        with self.assertRaises(ParseError):
            pc._parse_row("make_colosseum_prop WART_PUCK", "DIRK", 0, SYM,
                          "t.asm", 1)

    def test_nonliteral_hide_arg_raises(self):
        with self.assertRaises(ParseError):
            pc._parse_row("make_colosseum_prop DIDALOS, ILLUMINA, 2",
                          "RAGNAROK", 27, SYM, "t.asm", 1)

    def test_comment_anchor_mismatch_raises(self):
        for comment in (None, "", "MITHRILKNIFE"):
            with self.assertRaises(ParseError):
                pc._parse_row("make_colosseum_prop", comment, 0, SYM,
                              "t.asm", 1)

    def test_unknown_symbols_raise(self):
        with self.assertRaises(ParseError):
            pc._parse_row("make_colosseum_prop NO_SUCH, ELIXIR", "DIRK", 0,
                          SYM, "t.asm", 1)
        with self.assertRaises(ParseError):
            pc._parse_row("make_colosseum_prop WART_PUCK, NO_SUCH", "DIRK",
                          0, SYM, "t.asm", 1)

    def test_wide_monster_index_raises(self):
        with self.assertRaises(pc.ColosseumError):
            pc._parse_row("make_colosseum_prop WIDE_MONSTER, ELIXIR", "DIRK",
                          0, SYM, "t.asm", 1)


# --- Layer 2: table walker on synthetic files --------------------------------

def _write_table(rows, header="ColosseumProp:", footer=".popseg"):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w") as fh:
        fh.write("; synthetic\n{}\n".format(header))
        for row in rows:
            fh.write(row + "\n")
        fh.write("\n{}\n".format(footer))
    return path


class TableWalkerTests(unittest.TestCase):

    def test_missing_label_raises(self):
        path = _write_table([], header="; no label here")
        try:
            with self.assertRaises(ParseError):
                pc.read_records(path, SYM)
        finally:
            os.remove(path)

    def test_missing_terminator_raises(self):
        path = _write_table([], footer="; no popseg")
        try:
            with self.assertRaises(ParseError):
                pc.read_records(path, SYM)
        finally:
            os.remove(path)

    def test_wrong_row_count_raises(self):
        # A structurally valid but 2-row table (comment anchors satisfied via
        # the fake ITEM-name map).
        path = _write_table(["make_colosseum_prop  ; DIRK",
                             "make_colosseum_prop  ; MITHRILKNIFE"])
        try:
            with self.assertRaises(pc.ColosseumError):
                pc.read_records(path, SYM)
        finally:
            os.remove(path)

    def test_foreign_line_inside_table_raises(self):
        path = _write_table(["make_colosseum_prop  ; DIRK",
                             ".byte $12"])
        try:
            with self.assertRaises(ParseError):
                pc.read_records(path, SYM)
        finally:
            os.remove(path)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "menu", "colosseum.asm")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src colosseum.asm not present")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = pc.Symbols(os.path.join(root, "include", "const.inc"))
        cls.records = pc.read_records(
            os.path.join(root, "src", "menu", "colosseum.asm"), cls.symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.records), 256)
        for rec in self.records:
            self.assertEqual(len(rec.raw), 4)
            self.assertEqual(rec.raw[1], 0x40)
            self.assertLessEqual(rec.raw[0], 0xFF)

    def test_default_rows(self):
        # 151 blank-argument rows take the macro defaults (hand-counted).
        defaults = [rec for rec in self.records
                    if rec.monster_name == "CHUPON_COLOSSEUM"
                    and rec.prize_name == "ELIXIR"]
        self.assertEqual(len(defaults), 151)
        dirk = self.records[0]
        self.assertEqual(dirk.raw, [0x40, 0x40, 0xEE, 0x00])

    def test_hide_prize_rows(self):
        # Exactly four rows hide their prize (hand-traced): Ragnarok ($1B),
        # Striker ($29), Cat Hood ($80), and Merit Award ($DA).
        hidden = [rec.index for rec in self.records if rec.hidden]
        self.assertEqual(hidden, [0x1B, 0x29, 0x80, 0xDA])
        ragnarok = self.records[0x1B]
        self.assertEqual(ragnarok.monster_name, "DIDALOS")
        self.assertEqual(ragnarok.prize_name, "ILLUMINA")

    def test_boundary_rows(self):
        # Row 4 (Thiefknife) is the first non-default wager; row 255 is the
        # EMPTY sentinel's default row (hand-traced).
        thiefknife = self.records[4]
        self.assertEqual(thiefknife.monster_name, "WART_PUCK")
        self.assertEqual(thiefknife.prize_name, "THIEF_GLOVE")
        self.assertFalse(thiefknife.hidden)
        self.assertEqual(self.records[255].monster_name, "CHUPON_COLOSSEUM")


if __name__ == "__main__":
    unittest.main()
