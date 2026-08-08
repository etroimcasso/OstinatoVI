#!/usr/bin/env python3
"""Unit tests for parse_monster_special_anim.py.

Three layers per the parser-test discipline:
  1. pure helpers (name sanitization, dominant-name derivation, renderer
     formatting);
  2. synthetic inputs (length guard, row-range guard, derivation-drift
     guard);
  3. end-to-end against the real original-src monster_special_anim.dat +
     monster_special_name_en.json + const.inc (skipped cleanly when the rip
     output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_monster_special_anim as psa
from common import ParseError


class FakeSymbols(object):
    monster_names = {0: "GUARD", 1: "SOLDIER"}


class ReadValuesTests(unittest.TestCase):

    def _read_bytes(self, data):
        fd, path = tempfile.mkstemp(suffix=".dat")
        with os.fdopen(fd, "wb") as fh:
            fh.write(data)
        try:
            return psa.read_values(path)
        finally:
            os.remove(path)

    def test_wrong_length_raises(self):
        for length in (0, 256, psa.RECORD_COUNT - 1, psa.RECORD_COUNT + 1):
            with self.assertRaises(ParseError):
                self._read_bytes(bytes(length))

    def test_out_of_range_row_raises(self):
        data = bytearray(psa.RECORD_COUNT)
        data[42] = psa.TABLE_ROWS
        with self.assertRaises(ParseError) as ctx:
            self._read_bytes(bytes(data))
        self.assertIn("outside the 35-row table", str(ctx.exception))

    def test_exact_length_in_range_returns_all_bytes(self):
        data = bytes([i % psa.TABLE_ROWS for i in range(psa.RECORD_COUNT)])
        values = self._read_bytes(data)
        self.assertEqual(len(values), 384)
        self.assertEqual(values[34], 34)
        self.assertEqual(values[35], 0)


class DerivationTests(unittest.TestCase):

    def test_sanitize_name(self):
        self.assertEqual(psa.sanitize_name("Iron Ball"), "IRON_BALL")
        self.assertEqual(psa.sanitize_name("Near Fatal"), "NEAR_FATAL")
        self.assertEqual(psa.sanitize_name("Wind-up"), "WIND_UP")
        self.assertEqual(psa.sanitize_name("10 Hits"), "N10_HITS")

    def test_dominant_name_wins(self):
        # Row 0: two "Bite" users beat one "Claw" user.
        values = [0, 0, 0]
        names = ["Bite", "Claw", "Bite"]
        derived = psa.derive_names(values, names)
        self.assertEqual(derived[0], "BITE")

    def test_tie_breaks_to_earliest_monster(self):
        values = [0, 0]
        names = ["Claw", "Bite"]
        derived = psa.derive_names(values, names)
        self.assertEqual(derived[0], "CLAW")

    def test_unused_rows_get_unused_names(self):
        derived = psa.derive_names([0], ["Hit"])
        self.assertEqual(derived[29], "UNUSED_29")
        self.assertEqual(derived[34], "UNUSED_34")
        self.assertEqual(len(derived), psa.TABLE_ROWS)

    def test_mirror_table_shape(self):
        self.assertEqual(len(psa._ANIMATION_NAMES), psa.TABLE_ROWS)
        self.assertEqual(len(set(psa._ANIMATION_NAMES)), psa.TABLE_ROWS)


class RendererFormattingTests(unittest.TestCase):

    def test_inc_rows_render_named_enumerators(self):
        inc = psa.render_inc([3, 24], FakeSymbols())
        self.assertIn(".id = MonsterId::GUARD,", inc)
        self.assertIn(".specialAnim = MonsterAttackAnimation::CRITICAL },",
                      inc)
        self.assertIn(".id = MonsterId::SOLDIER,", inc)
        self.assertIn(".specialAnim = MonsterAttackAnimation::INVIZ },", inc)
        # Symbol-set semantics — no numbers in the emitted rows.
        self.assertNotIn("0x", inc.split("DO NOT EDIT")[1])

    def test_fixture_rows_render_decimal_id_and_raw_hex_byte(self):
        fixture = psa.render_fixture([0x03, 0x18])
        self.assertIn("std::array<ExpectedMonsterSpecialAnimEntry, 2>",
                      fixture)
        self.assertIn("{ .id =   0, .specialAnim = 0x03 },", fixture)
        self.assertIn("{ .id =   1, .specialAnim = 0x18 },", fixture)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle",
                                   "monster_special_anim.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src monster_special_anim.dat not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = psa.Symbols(os.path.join(root, "include", "const.inc"))
        cls.values = psa.read_values(
            os.path.join(root, "src", "battle", "monster_special_anim.dat"))
        cls.display_names = psa.read_display_names(
            os.path.join(root, "src", "text",
                         "monster_special_name_en.json"))

    def test_corpus_shape(self):
        self.assertEqual(len(self.values), 384)
        self.assertEqual(len(self.symbols.monster_names), 384)
        self.assertTrue(all(v < psa.TABLE_ROWS for v in self.values))

    def test_derivation_matches_the_mirror(self):
        # The corpus-recomputed dominant names ARE the mirror table (and
        # therefore the C++ enumerators) — the parser's own hard-error gate,
        # asserted directly.
        derived = psa.derive_names(self.values, self.display_names)
        self.assertEqual(tuple(derived), psa._ANIMATION_NAMES)

    def test_boundary_bytes_hand_traced(self):
        # Hand-traced from the .dat: GUARD's byte is $03 (CRITICAL), the
        # last record's byte is $00 (HIT).
        self.assertEqual(self.values[0], 0x03)
        self.assertEqual(self.values[383], 0x00)

    def test_inc_rows_render_named(self):
        inc = psa.render_inc(self.values, self.symbols)
        self.assertIn(".id = MonsterId::GUARD,", inc)
        self.assertIn(".specialAnim = MonsterAttackAnimation::CRITICAL },",
                      inc)


if __name__ == "__main__":
    unittest.main()
