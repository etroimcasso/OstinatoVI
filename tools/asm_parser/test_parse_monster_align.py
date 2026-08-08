#!/usr/bin/env python3
"""Unit tests for parse_monster_align.py.

Three layers per the parser-test discipline:
  1. pure helpers (renderer formatting);
  2. synthetic inputs (length guard, value-space guard);
  3. end-to-end against the real original-src monster_align.dat + const.inc
     (skipped cleanly when the rip output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_monster_align as pal
from common import ParseError


class FakeSymbols(object):
    monster_names = {0: "GUARD", 1: "SOLDIER"}


def _read_bytes(data):
    fd, path = tempfile.mkstemp(suffix=".dat")
    with os.fdopen(fd, "wb") as fh:
        fh.write(data)
    try:
        return pal.read_values(path)
    finally:
        os.remove(path)


class ReadValuesTests(unittest.TestCase):

    def test_wrong_length_raises(self):
        for length in (0, 384, pal.RECORD_COUNT - 1, pal.RECORD_COUNT + 1):
            with self.assertRaises(ParseError):
                _read_bytes(bytes(length))

    def test_out_of_range_value_raises(self):
        data = bytearray(256)
        data[100] = 5
        with self.assertRaises(ParseError) as ctx:
            _read_bytes(bytes(data))
        self.assertIn("outside the named 0..4 space", str(ctx.exception))

    def test_exact_length_in_range_returns_all_bytes(self):
        values = _read_bytes(bytes([0, 1, 2, 3, 4]) * 51 + bytes(1))
        self.assertEqual(len(values), 256)
        self.assertEqual(values[:5], [0, 1, 2, 3, 4])


class RendererFormattingTests(unittest.TestCase):

    def test_inc_rows_render_both_enumerators(self):
        inc = pal.render_inc([1, 4], FakeSymbols())
        self.assertIn(".id = MonsterId::GUARD,", inc)
        self.assertIn(".alignment = MonsterVerticalAlignment::GROUND },", inc)
        self.assertIn(".id = MonsterId::SOLDIER,", inc)
        self.assertIn(".alignment = MonsterVerticalAlignment::FLYING },", inc)

    def test_alignment_names_cover_the_value_space(self):
        self.assertEqual(pal._ALIGNMENT_NAMES,
                         ("CEILING", "GROUND", "BURIED", "FLOATING",
                          "FLYING"))

    def test_fixture_rows_render_decimal_id_and_raw_value(self):
        fixture = pal.render_fixture([1, 4])
        self.assertIn("std::array<ExpectedMonsterAlignEntry, 2>", fixture)
        self.assertIn("{ .id =   0, .alignment = 1 },", fixture)
        self.assertIn("{ .id =   1, .alignment = 4 },", fixture)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "btlgfx",
                                   "monster_align.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src monster_align.dat not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = pal.Symbols(os.path.join(root, "include", "const.inc"))
        cls.values = pal.read_values(
            os.path.join(root, "src", "btlgfx", "monster_align.dat"))

    def test_corpus_shape(self):
        self.assertEqual(len(self.values), 256)
        self.assertTrue(all(0 <= v <= 4 for v in self.values))

    def test_spot_values_hand_traced(self):
        # Hand-traced from the .dat: GUARD is ground; monster 45 is one of
        # the three ceiling records; 34 flies, 10 is buried, 35 floats.
        self.assertEqual(self.values[0], 1)
        self.assertEqual(self.values[45], 0)
        self.assertEqual(self.values[34], 4)
        self.assertEqual(self.values[10], 2)
        self.assertEqual(self.values[35], 3)

    def test_inc_rows_render_named(self):
        inc = pal.render_inc(self.values, self.symbols)
        self.assertIn(".alignment = MonsterVerticalAlignment::GROUND },", inc)
        self.assertIn(".alignment = MonsterVerticalAlignment::CEILING },",
                      inc)


if __name__ == "__main__":
    unittest.main()
