#!/usr/bin/env python3
"""Unit tests for parse_monster_overlap.py.

Three layers per the parser-test discipline:
  1. pure helpers (renderer formatting);
  2. synthetic inputs (label guard, count guard, malformed .byte);
  3. end-to-end against the real original-src monster_overlap.asm + const.inc
     (skipped cleanly when the rip output is absent).

Python 3 stdlib only. Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_monster_overlap as pmo
from common import ParseError


class FakeSymbols(object):
    def __init__(self, count):
        self.monster_names = {i: "M{}".format(i) for i in range(count)}


def _read_asm(text, count=pmo.RECORD_COUNT):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    try:
        return pmo.read_rows(path, FakeSymbols(count))
    finally:
        os.remove(path)


def _table(values):
    lines = ["MonsterOverlap:"]
    for v in values:
        lines.append("        .byte   {}".format(v))
    return "\n".join(lines) + "\n"


class ReadRowsTests(unittest.TestCase):

    def test_missing_label_raises(self):
        with self.assertRaises(ParseError) as ctx:
            _read_asm("        .byte 0\n")
        self.assertIn("label not found", str(ctx.exception))

    def test_wrong_count_raises(self):
        with self.assertRaises(ParseError):
            _read_asm(_table([0] * (pmo.RECORD_COUNT - 1)))

    def test_malformed_byte_raises(self):
        text = "MonsterOverlap:\n        .byte   notanumber\n"
        with self.assertRaises(ParseError):
            _read_asm(text)

    def test_exact_count_returns_values(self):
        values = _read_asm(_table(list(range(pmo.RECORD_COUNT))
                                  if pmo.RECORD_COUNT <= 256
                                  else [i % 73 for i in range(pmo.RECORD_COUNT)]))
        self.assertEqual(len(values), pmo.RECORD_COUNT)

    def test_comment_mismatch_is_tolerated(self):
        # The upstream comments are colloquial labels; they are not asserted
        # against the enum (row 239 is 1ST_CLASS vs enum FIRST_CLASS).
        text = ("MonsterOverlap:\n"
                + "".join("        .byte   {}                ; WRONG_{}\n"
                          .format(v, i)
                          for i, v in enumerate([0] * pmo.RECORD_COUNT)))
        values = _read_asm(text)
        self.assertEqual(len(values), pmo.RECORD_COUNT)


class RendererFormattingTests(unittest.TestCase):

    def test_inc_rows_render_named_monster_and_decimal_shift(self):
        inc = pmo.render_inc([72, 0], FakeSymbols(2))
        self.assertIn(".monster = MonsterId::M0,", inc)
        self.assertIn(".yShift = 72 },", inc)
        self.assertIn(".monster = MonsterId::M1,", inc)
        self.assertIn(".yShift = 0 },", inc)

    def test_fixture_rows_render_decimal_id_and_value(self):
        fixture = pmo.render_fixture([72, 0])
        self.assertIn("std::array<ExpectedMonsterOverlapEntry, 2>", fixture)
        self.assertIn("{ .id =   0, .yShift = 72 },", fixture)
        self.assertIn("{ .id =   1, .yShift = 0 },", fixture)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "btlgfx",
                                   "monster_overlap.asm")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src monster_overlap.asm not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = pmo.Symbols(os.path.join(root, "include", "const.inc"))
        cls.values = pmo.read_rows(
            os.path.join(root, "src", "btlgfx", "monster_overlap.asm"),
            cls.symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.values), 384)
        self.assertTrue(all(0 <= v <= 0xFF for v in self.values))

    def test_fourteen_non_zero_rows(self):
        self.assertEqual(sum(1 for v in self.values if v), 14)

    def test_max_shift_is_seventy_two(self):
        self.assertEqual(max(self.values), 72)

    def test_inc_renders_named_shift(self):
        inc = pmo.render_inc(self.values, self.symbols)
        self.assertIn(".yShift = 72 },", inc)


if __name__ == "__main__":
    unittest.main()
