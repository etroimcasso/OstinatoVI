#!/usr/bin/env python3
"""Unit tests for parse_shop_prop.py.

Three layers per the parser-test discipline:
  1. pure helpers (config decomposition, brace-list wrapping);
  2. synthetic inputs (length guard, config/pad/name structural errors);
  3. end-to-end against the real original-src shop_prop.dat + const.inc
     (skipped cleanly when the rip output is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_shop_prop as psp


class _FakeSymbols(object):
    """Just the attribute the record decomposition consumes."""

    item_names = {0x0B: "REGAL_CUTLASS", 0x33: "TONIC", 0xE8: "TONIC_ALT",
                  0xFF: "EMPTY"}


SYM = _FakeSymbols()


def _record_bytes(config, items):
    return bytes([config] + items)


# --- Layer 1: pure helpers ---------------------------------------------------

class ConfigDecompositionTests(unittest.TestCase):

    def test_named_codes(self):
        self.assertEqual(psp.decompose_config(0x01, 0),
                         ("kShopTypeWeapon", "kPriceAdjustNone"))
        self.assertEqual(psp.decompose_config(0x33, 0),
                         ("kShopTypeItem", "kPriceAdjustEdgarMinus50"))
        self.assertEqual(psp.decompose_config(0x05, 0),
                         ("kShopTypeVendor", "kPriceAdjustNone"))
        self.assertEqual(psp.decompose_config(0x00, 0),
                         ("kShopTypeUnused", "kPriceAdjustNone"))

    def test_type_past_text_table_raises(self):
        for byte in (0x06, 0x07):
            with self.assertRaises(psp.ShopPropError):
                psp.decompose_config(byte, 0)

    def test_adjustment_off_dispatch_table_raises(self):
        with self.assertRaises(psp.ShopPropError):
            psp.decompose_config(0x39, 0)  # adjustment 7

    def test_high_bits_raise(self):
        for byte in (0x41, 0x81, 0xC1):
            with self.assertRaises(psp.ShopPropError):
                psp.decompose_config(byte, 0)


class BraceWrapTests(unittest.TestCase):

    def test_short_stays_single_line(self):
        self.assertEqual(psp._wrap_braces(".items  = ", ["ItemId::TONIC"],
                                          " " * 12),
                         ".items  = {ItemId::TONIC}")

    def test_long_wraps_within_width(self):
        tokens = ["ItemId::REGAL_CUTLASS"] * 8
        rendered = psp._wrap_braces(".items  = ", tokens, " " * 12)
        self.assertIn("\n", rendered)
        for line in rendered.splitlines():
            self.assertLessEqual(len(" " * 12 + line), psp._LINE_WIDTH + 12)
        self.assertEqual(rendered.count("ItemId::REGAL_CUTLASS"), 8)


# --- Layer 2: synthetic inputs -----------------------------------------------

class RecordStructuralTests(unittest.TestCase):

    def test_valid_record_decomposes(self):
        rec = psp.Record(0, _record_bytes(0x01, [0x0B, 0x33] + [0xFF] * 6),
                         SYM)
        self.assertEqual(rec.type_name, "kShopTypeWeapon")
        self.assertEqual(rec.item_names,
                         ["REGAL_CUTLASS", "TONIC"] + ["EMPTY"] * 6)

    def test_unused_record_with_item_raises(self):
        with self.assertRaises(psp.ShopPropError):
            psp.Record(0, _record_bytes(0x00, [0x0B] + [0xFF] * 7), SYM)

    def test_item_after_pad_raises(self):
        with self.assertRaises(psp.ShopPropError):
            psp.Record(0, _record_bytes(0x01,
                                        [0x0B, 0xFF, 0x33] + [0xFF] * 5),
                       SYM)

    def test_unnamed_item_raises(self):
        with self.assertRaises(psp.ShopPropError):
            psp.Record(0, _record_bytes(0x01, [0x12] + [0xFF] * 7), SYM)


class ReadRecordsTests(unittest.TestCase):

    def test_wrong_length_raises(self):
        # Length is checked before any record is decomposed, so no symbol
        # table is needed.
        for length in (0, 9, psp.EXPECTED_LEN - 1, psp.EXPECTED_LEN + 1):
            fd, path = tempfile.mkstemp(suffix=".dat")
            with os.fdopen(fd, "wb") as fh:
                fh.write(bytes(length))
            try:
                with self.assertRaises(psp.ShopPropError):
                    psp.read_records(path, None)
            finally:
                os.remove(path)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "menu", "shop_prop.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src shop_prop.dat not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.symbols = psp.Symbols(os.path.join(root, "include", "const.inc"))
        cls.records = psp.read_records(
            os.path.join(root, "src", "menu", "shop_prop.dat"), cls.symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.records), 128)
        for rec in self.records:
            self.assertEqual(len(rec.raw), 9)

    def test_boundary_records(self):
        # Shop 0 (the Narshe weapon shop) and shop 127 (an unused record) —
        # hand-traced.
        first = self.records[0]
        self.assertEqual(first.type_name, "kShopTypeWeapon")
        self.assertEqual(first.adjust_name, "kPriceAdjustNone")
        self.assertEqual(first.item_names[0], "REGAL_CUTLASS")
        self.assertEqual(first.item_names[7], "EMPTY")
        last = self.records[127]
        self.assertEqual(last.raw, [0x00] + [0xFF] * 8)
        self.assertEqual(last.type_name, "kShopTypeUnused")

    def test_edgar_discount_shops(self):
        # The Figaro Castle shops carry adjustment 6 (hand-traced: shops 4,
        # 47, 60-64, 82-84 — 10 records).
        edgar = [rec.index for rec in self.records
                 if rec.adjust_name == "kPriceAdjustEdgarMinus50"]
        self.assertEqual(edgar, [4, 47, 60, 61, 62, 63, 64, 82, 83, 84])
        self.assertEqual(self.records[4].type_name, "kShopTypeItem")
        self.assertEqual(self.records[4].raw[0], 0x33)

    def test_corpus_wide_properties(self):
        # The lone Vendor shop, and the 41 fully-empty unused records.
        vendors = [rec.index for rec in self.records
                   if rec.type_name == "kShopTypeVendor"]
        self.assertEqual(vendors, [39])
        unused = [rec for rec in self.records
                  if rec.type_name == "kShopTypeUnused"]
        self.assertEqual(len(unused), 41)
        for rec in unused:
            self.assertEqual(rec.raw[1:], [0xFF] * 8)


if __name__ == "__main__":
    unittest.main()
