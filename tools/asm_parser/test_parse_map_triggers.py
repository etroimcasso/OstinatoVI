#!/usr/bin/env python3
"""Unit tests for parse_map_triggers.py.

Three layers per the parser-test discipline:
  1. pure helper — read_trigger_inc: ITEM_SIZE / ARRAY_LENGTH capture, `_N :=`
     offset collection, field-label skipping, and the malformed / duplicate /
     unexpected-line error paths;
  2. synthetic inputs exercising every structural assert in _resolve_table
     (item-size / array-length mismatch, incomplete offset space, wrong .dat
     size, non-multiple offset, non-monotonic offsets);
  3. end-to-end against the real original-src trigger files (skipped cleanly
     when the rip output is absent).

Python 3 stdlib only. Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import shutil
import tempfile
import unittest

import parse_map_triggers as pmt
from common import ParseError


def _write_inc(text):
    fd, path = tempfile.mkstemp(suffix=".inc")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


def _make_source(inc_text, dat_bytes, inc_name, dat_name):
    """Build a temp source tree (include/field + src/field/trigger) and return
    its root; caller removes it."""
    root = tempfile.mkdtemp()
    inc_dir = os.path.join(root, "include", "field")
    dat_dir = os.path.join(root, "src", "field", "trigger")
    os.makedirs(inc_dir)
    os.makedirs(dat_dir)
    with open(os.path.join(inc_dir, inc_name), "w", encoding="utf-8") as fh:
        fh.write(inc_text)
    with open(os.path.join(dat_dir, dat_name), "wb") as fh:
        fh.write(bytes(dat_bytes))
    return root


# A minimal 2-byte-record, 3-map-slot trigger scope (2 distinct records).
_GOOD_INC = (
    ".list off\n"
    ".ifndef FOO_INC\n"
    "FOO_INC = 1\n"
    ".global Foo, FooPtrs\n"
    ".scope Foo\n"
    "        ITEM_SIZE = 2\n"
    "        Start := FooPtrs\n"
    "        PosX := Start\n"
    "        _0 := Foo + $0000\n"
    "        _1 := Foo + $0000\n"
    "        _2 := Foo + $0002\n"
    "        ARRAY_LENGTH = 3\n"
    ".endscope\n"
    ".endif\n"
    ".list on\n")


# --- Layer 1: pure helper ----------------------------------------------------

class ReadTriggerIncTests(unittest.TestCase):

    def _read(self, text):
        path = _write_inc(text)
        try:
            return pmt.read_trigger_inc(path)
        finally:
            os.remove(path)

    def test_captures_metadata_and_offsets(self):
        item_size, array_length, offsets = self._read(_GOOD_INC)
        self.assertEqual(item_size, 2)
        self.assertEqual(array_length, 3)
        self.assertEqual(offsets, {0: 0x0000, 1: 0x0000, 2: 0x0002})

    def test_field_labels_skipped(self):
        # PosX := Start and Start := FooPtrs must not be read as offsets.
        _item, _len, offsets = self._read(_GOOD_INC)
        self.assertEqual(sorted(offsets), [0, 1, 2])

    def test_malformed_offset_raises(self):
        text = (".scope Foo\n"
                "        _0 := Foo + xyz\n"
                ".endscope\n")
        with self.assertRaises(ParseError) as ctx:
            self._read(text)
        self.assertIn("malformed offset", str(ctx.exception))

    def test_duplicate_offset_raises(self):
        text = (".scope Foo\n"
                "        _0 := Foo + $0000\n"
                "        _0 := Foo + $0002\n"
                ".endscope\n")
        with self.assertRaises(ParseError) as ctx:
            self._read(text)
        self.assertIn("duplicate", str(ctx.exception))

    def test_unexpected_line_raises(self):
        text = (".scope Foo\n"
                "        .byte $00\n"
                ".endscope\n")
        with self.assertRaises(ParseError) as ctx:
            self._read(text)
        self.assertIn("unexpected line", str(ctx.exception))


# --- Layer 2: synthetic structural guards ------------------------------------

class ResolveTableTests(unittest.TestCase):

    def _resolve(self, inc_text, dat_bytes, item_size, map_slots, record_count):
        root = _make_source(inc_text, dat_bytes, "foo.inc", "foo.dat")
        try:
            return pmt._resolve_table(root, item_size, map_slots, record_count,
                                      "foo.inc", "foo.dat")
        finally:
            shutil.rmtree(root)

    def test_good_case(self):
        records, offsets = self._resolve(
            _GOOD_INC, [0xAA, 0xBB, 0xCC, 0xDD], 2, 3, 2)
        self.assertEqual(records, [bytes([0xAA, 0xBB]), bytes([0xCC, 0xDD])])
        # record-index offsets: _0/_1 -> 0, _2 -> 1, end -> 2.
        self.assertEqual(offsets, [0, 0, 1, 2])

    def test_item_size_mismatch_raises(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve(_GOOD_INC, [0] * 4, 3, 3, 2)  # expect 3, inc has 2
        self.assertIn("ITEM_SIZE", str(ctx.exception))

    def test_array_length_mismatch_raises(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve(_GOOD_INC, [0] * 4, 2, 4, 2)  # expect 4, inc has 3
        self.assertIn("ARRAY_LENGTH", str(ctx.exception))

    def test_incomplete_offset_space_raises(self):
        inc = _GOOD_INC.replace("        _1 := Foo + $0000\n", "")
        with self.assertRaises(ParseError) as ctx:
            self._resolve(inc, [0] * 4, 2, 3, 2)
        self.assertIn("offset symbol space", str(ctx.exception))

    def test_wrong_dat_size_raises(self):
        with self.assertRaises(ParseError) as ctx:
            self._resolve(_GOOD_INC, [0] * 3, 2, 3, 2)  # 3 != 2 records * 2
        self.assertIn("wrong artifact", str(ctx.exception))

    def test_offset_not_multiple_raises(self):
        inc = _GOOD_INC.replace("        _2 := Foo + $0002\n",
                                "        _2 := Foo + $0001\n")  # not a mult of 2
        with self.assertRaises(ParseError) as ctx:
            self._resolve(inc, [0] * 4, 2, 3, 2)
        self.assertIn("not a multiple", str(ctx.exception))

    def test_non_monotonic_raises(self):
        # _1 -> record 1, _2 -> record 0: decreasing.
        inc = (_GOOD_INC
               .replace("        _1 := Foo + $0000\n",
                        "        _1 := Foo + $0002\n")
               .replace("        _2 := Foo + $0002\n",
                        "        _2 := Foo + $0000\n"))
        with self.assertRaises(ParseError) as ctx:
            self._resolve(inc, [0] * 4, 2, 3, 2)
        self.assertIn("monotonic", str(ctx.exception))


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    dat = os.path.join(root, "src", "field", "trigger", "treasure_prop.dat")
    if os.path.isfile(dat):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src trigger .dat files not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _find_source_root()

    def _resolve(self, item_size, map_slots, record_count, inc, dat):
        return pmt._resolve_table(self.root, item_size, map_slots, record_count,
                                  inc, dat)

    def test_record_counts_and_extents(self):
        for slug, inc, dat, item_size, slots, count in pmt.TABLES:
            records, offsets = pmt._resolve_table(
                self.root, item_size, slots, count, inc, dat)
            self.assertEqual(len(records), count)
            self.assertEqual(len(offsets), slots + 1)
            self.assertEqual(offsets[-1], count)   # end marker == record count

    def test_table_shapes(self):
        # (slug, inc, dat, item_size, slots, record_count)
        specs = {t[0]: t for t in pmt.TABLES}
        _s, _inc, _dat, isz, slots, cnt = specs["treasure"]
        records, offsets = pmt._resolve_table(
            self.root, isz, slots, cnt, "treasure_prop.inc", "treasure_prop.dat")
        self.assertEqual((isz, slots, cnt), (5, 415, 286))
        # Map 75 carries 8 treasures (offset[75]=43, offset[76]=51).
        self.assertEqual(offsets[75], 43)
        self.assertEqual(offsets[76], 51)

    def test_parent_sentinel_present(self):
        _s, _inc, _dat, isz, slots, cnt = \
            {t[0]: t for t in pmt.TABLES}["long_entrance"]
        records, _offsets = pmt._resolve_table(
            self.root, isz, slots, cnt, "long_entrance.inc", "long_entrance.dat")
        # At least one entrance returns to the parent map (dest word & $1ff==$1ff).
        self.assertTrue(any((r[3] | (r[4] << 8)) & 0x01FF == 0x01FF
                            for r in records))

    def test_run_check_only_ok(self):
        self.assertEqual(pmt.run(self.root, ".", check_only=True), 0)


if __name__ == "__main__":
    unittest.main()
