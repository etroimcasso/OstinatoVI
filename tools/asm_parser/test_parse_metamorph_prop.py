#!/usr/bin/env python3
"""Unit tests for parse_metamorph_prop.py.

Three layers per the parser-test discipline:
  1. pure helpers (rate-row grammar, renderer formatting);
  2. synthetic inputs (length guard, missing/duplicated label, malformed or
     wrong-arity .byte rows);
  3. end-to-end against the real original-src metamorph_prop.dat +
     battle_main.asm + const.inc (skipped cleanly when the rip output is
     absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_metamorph_prop as pmm


def _write_temp(suffix, data):
    fd, path = tempfile.mkstemp(suffix=suffix)
    mode = "wb" if isinstance(data, bytes) else "w"
    with os.fdopen(fd, mode) as fh:
        fh.write(data)
    return path


# --- Layer 1 + 2: rate-row grammar over synthetic sources --------------------

class ReadRatesTests(unittest.TestCase):

    def _rates_from(self, text):
        path = _write_temp(".asm", text)
        try:
            return pmm.read_rates(path)
        finally:
            os.remove(path)

    def _assert_raises(self, text):
        path = _write_temp(".asm", text)
        try:
            with self.assertRaises(pmm.MetamorphError):
                pmm.read_rates(path)
        finally:
            os.remove(path)

    def test_labeled_byte_row_parses(self):
        text = ("; metamorph probabilities\n"
                "MetamorphRateTbl:\n"
                "@3dc5:  .byte   $ff,$c0,$80,$40,$20,$10,$08,$00\n")
        self.assertEqual(self._rates_from(text),
                         [0xFF, 0xC0, 0x80, 0x40, 0x20, 0x10, 0x08, 0x00])

    def test_row_without_local_label_parses(self):
        text = ("MetamorphRateTbl:\n"
                "        .byte   $ff,$c0,$80,$40,$20,$10,$08,$00\n")
        self.assertEqual(self._rates_from(text),
                         [0xFF, 0xC0, 0x80, 0x40, 0x20, 0x10, 0x08, 0x00])

    def test_blank_and_comment_lines_before_row_are_skipped(self):
        text = ("MetamorphRateTbl:\n"
                "\n"
                "; the eight probability bytes\n"
                "        .byte   $ff,$c0,$80,$40,$20,$10,$08,$00\n")
        self.assertEqual(self._rates_from(text),
                         [0xFF, 0xC0, 0x80, 0x40, 0x20, 0x10, 0x08, 0x00])

    def test_missing_label_raises(self):
        self._assert_raises("SomeOtherTbl:\n        .byte $00\n")

    def test_duplicated_label_raises(self):
        self._assert_raises(
            "MetamorphRateTbl:\n"
            "        .byte   $ff,$c0,$80,$40,$20,$10,$08,$00\n"
            "MetamorphRateTbl:\n"
            "        .byte   $ff,$c0,$80,$40,$20,$10,$08,$00\n")

    def test_wrong_arity_raises(self):
        self._assert_raises(
            "MetamorphRateTbl:\n"
            "        .byte   $ff,$c0,$80,$40\n")
        self._assert_raises(
            "MetamorphRateTbl:\n"
            "        .byte   $ff,$c0,$80,$40,$20,$10,$08,$00,$00\n")

    def test_non_byte_row_raises(self):
        self._assert_raises(
            "MetamorphRateTbl:\n"
            "        .word   $ffc0\n")

    def test_malformed_literal_raises(self):
        self._assert_raises(
            "MetamorphRateTbl:\n"
            "        .byte   $ff,$c0,$80,$40,$20,$10,$08,bogus\n")

    def test_label_at_eof_raises(self):
        self._assert_raises("MetamorphRateTbl:\n")


class ReadPacksTests(unittest.TestCase):

    def test_wrong_length_raises(self):
        # Length is checked before any pack is resolved, so no symbol table
        # is needed.
        for length in (0, 4, pmm.EXPECTED_LEN - 1, pmm.EXPECTED_LEN + 1):
            path = _write_temp(".dat", bytes(length))
            try:
                with self.assertRaises(pmm.MetamorphError):
                    pmm.read_packs(path, None)
            finally:
                os.remove(path)


class RendererFormattingTests(unittest.TestCase):

    def test_rate_rows_render_enumerator_id_and_hex_value(self):
        inc = pmm.render_rate_inc([0xFF, 0xC0, 0x80, 0x40, 0x20, 0x10, 0x08,
                                   0x00])
        self.assertIn(
            "    { .id = MetamorphRate::ODDS_255_256, .value = 0xFF },\n",
            inc)
        self.assertIn("    { .id = MetamorphRate::NEVER, .value = 0x00 },\n",
                      inc)

    def test_pack_row_renders_item_enumerators(self):
        class FakePack(object):
            index = 3
            raw = [0x01, 0x02, 0x03, 0x04]
            item_names = ["DIRK", "MITHRILKNIFE", "GUARDIAN", "AIR_LANCET"]
        row = pmm._render_pack_row(FakePack())
        self.assertIn(".index = 3,", row)
        self.assertIn("ItemId::DIRK, ItemId::MITHRILKNIFE,", row)
        self.assertIn("ItemId::GUARDIAN, ItemId::AIR_LANCET", row)
        self.assertNotIn("0x", row)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "battle",
                                   "metamorph_prop.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src metamorph_prop.dat not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = pmm.Symbols(os.path.join(root, "include", "const.inc"))
        cls.packs = pmm.read_packs(
            os.path.join(root, "src", "battle", "metamorph_prop.dat"),
            cls.symbols)
        cls.rates = pmm.read_rates(
            os.path.join(root, "src", "battle", "battle_main.asm"))

    def test_corpus_shape(self):
        self.assertEqual(len(self.packs), 32)
        for pack in self.packs:
            self.assertEqual(len(pack.raw), 4)
            self.assertEqual(len(pack.item_names), 4)

    def test_rate_row_matches_the_documented_ladder(self):
        # Hand-traced from battle_main.asm:10009; battle-ram.txt:963 documents
        # the same ladder (255/256, 3/4, 1/2, 1/4, 1/8, 1/16, 1/32, 0).
        self.assertEqual(self.rates,
                         [0xFF, 0xC0, 0x80, 0x40, 0x20, 0x10, 0x08, 0x00])

    def test_first_pack_raw_bytes(self):
        # Pack 0, hand-traced from the .dat's first 4 bytes: F2 F8 F3 F4.
        self.assertEqual(self.packs[0].raw, [0xF2, 0xF8, 0xF3, 0xF4])

    def test_last_pack_is_all_zero(self):
        # Packs 28-31 are all item byte $00 in the ROM; the resolver names
        # them like any other byte (item $00 = DIRK).
        self.assertEqual(self.packs[31].raw, [0x00, 0x00, 0x00, 0x00])
        self.assertEqual(self.packs[31].item_names, ["DIRK"] * 4)

    def test_pack_rows_render_named(self):
        inc = pmm.render_pack_inc(self.packs)
        self.assertNotRegex(inc, r"\.items = \{ 0x")
        self.assertIn("MetamorphPackEntry{  // [0]", inc)
        self.assertIn("MetamorphPackEntry{  // [31]", inc)

    def test_fixture_carries_both_tables(self):
        fixture = pmm.render_fixture(self.packs, self.rates)
        self.assertIn("std::array<ExpectedMetamorphPackEntry, 32>", fixture)
        self.assertIn("std::array<ExpectedMetamorphRateEntry, 8>", fixture)
        self.assertIn(".item0 = 0xF2, .item1 = 0xF8, .item2 = 0xF3,"
                      " .item3 = 0xF4", fixture)
        self.assertIn("{ .index = 0, .value = 0xFF },", fixture)


if __name__ == "__main__":
    unittest.main()
