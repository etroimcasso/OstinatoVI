#!/usr/bin/env python3
"""Unit tests for parse_text_meta.py.

Three layers per the parser-test discipline:
  1. pure helpers (read_class_inc reader, row_for_class validation, renderer
     formatting);
  2. synthetic inputs (skeleton scope, fixed-with-offsets, pointer count
     mismatch, malformed ARRAY_LENGTH, stray scope line);
  3. end-to-end against the real original-src include/text/*.inc (skipped
     cleanly when the rip output is absent).

Python 3 stdlib only. Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_text_meta as ptm
from common import ParseError


def _read_inc(text):
    fd, path = tempfile.mkstemp(suffix=".inc")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    try:
        return ptm.read_class_inc(path)
    finally:
        os.remove(path)


def _fixed_inc(array_length, item_size):
    return (".list off\n.ifndef X_INC\nX_INC = 1\n.global X\n.scope X\n"
            "        ARRAY_LENGTH = {}\n        ITEM_SIZE = {}\n"
            ".endscope\n.endif\n.list on\n".format(array_length, item_size))


def _pointer_inc(array_length, offset_count):
    body = ["        Start := bank_start X",
            "        ARRAY_LENGTH = {}".format(array_length)]
    for i in range(offset_count):
        body.append("        _{} := X + ${:04x}".format(i, i * 4))
    return (".list off\n.ifndef X_INC\nX_INC = 1\n.global X, XPtrs\n.scope X\n"
            + "\n".join(body) + "\n.endscope\n.endif\n.list on\n")


_SKELETON_INC = (".list off\n.ifndef X_INC\nX_INC = 1\n.global X\n.scope X\n"
                 "; only comments here\n.endscope\n.endif\n.list on\n")


class ReadClassIncTests(unittest.TestCase):

    def test_fixed_reads_length_and_size(self):
        meta = _read_inc(_fixed_inc(64, 6))
        self.assertTrue(meta.has_metadata)
        self.assertEqual(meta.array_length, 64)
        self.assertEqual(meta.item_size, 6)
        self.assertEqual(meta.offset_count, 0)

    def test_pointer_counts_offsets(self):
        meta = _read_inc(_pointer_inc(5, 5))
        self.assertTrue(meta.has_metadata)
        self.assertEqual(meta.array_length, 5)
        self.assertIsNone(meta.item_size)
        self.assertEqual(meta.offset_count, 5)

    def test_skeleton_has_no_metadata(self):
        meta = _read_inc(_SKELETON_INC)
        self.assertFalse(meta.has_metadata)
        self.assertIsNone(meta.array_length)
        self.assertEqual(meta.offset_count, 0)

    def test_start_bank_line_is_not_a_record_symbol(self):
        # `Start := bank_start X` must not be counted as an offset symbol.
        meta = _read_inc(_pointer_inc(3, 3))
        self.assertEqual(meta.offset_count, 3)

    def test_malformed_array_length_raises(self):
        text = _fixed_inc(64, 6).replace("ARRAY_LENGTH = 64",
                                         "ARRAY_LENGTH = notanumber")
        with self.assertRaises(ParseError):
            _read_inc(text)

    def test_stray_scope_line_raises(self):
        text = (".scope X\n        ARRAY_LENGTH = 8\n"
                "        WHAT IS THIS\n.endscope\n")
        with self.assertRaises(ParseError):
            _read_inc(text)

    def test_unterminated_scope_raises(self):
        with self.assertRaises(ParseError):
            _read_inc(".scope X\n        ARRAY_LENGTH = 8\n")


class RowForClassTests(unittest.TestCase):

    def _meta(self, array_length=None, item_size=None, offset_count=0):
        has = array_length is not None
        return ptm.ClassMeta(array_length, item_size, offset_count, has)

    def test_fixed_builds_row(self):
        row = ptm.row_for_class("char_name", "CHAR_NAME", ptm.FIXED,
                                self._meta(64, 6, 0), "x.inc")
        self.assertEqual((row.enum, row.kind, row.record_count, row.record_size),
                         ("CHAR_NAME", ptm.FIXED, 64, 6))

    def test_pointer_builds_row_with_zero_size(self):
        row = ptm.row_for_class("dlg1", "DLG1", ptm.POINTER,
                                self._meta(1574, None, 1574), "x.inc")
        self.assertEqual(row.record_size, 0)
        self.assertEqual(row.record_count, 1574)

    def test_skeleton_en_class_raises(self):
        with self.assertRaises(ParseError):
            ptm.row_for_class("char_name", "CHAR_NAME", ptm.FIXED,
                              self._meta(None, None, 0), "x.inc")

    def test_fixed_missing_item_size_raises(self):
        with self.assertRaises(ParseError):
            ptm.row_for_class("char_name", "CHAR_NAME", ptm.FIXED,
                              self._meta(64, None, 0), "x.inc")

    def test_fixed_with_offsets_raises(self):
        with self.assertRaises(ParseError):
            ptm.row_for_class("char_name", "CHAR_NAME", ptm.FIXED,
                              self._meta(64, 6, 3), "x.inc")

    def test_pointer_with_item_size_raises(self):
        with self.assertRaises(ParseError):
            ptm.row_for_class("dlg1", "DLG1", ptm.POINTER,
                              self._meta(1574, 4, 1574), "x.inc")

    def test_pointer_offset_count_mismatch_raises(self):
        with self.assertRaises(ParseError):
            ptm.row_for_class("dlg1", "DLG1", ptm.POINTER,
                              self._meta(1574, None, 1573), "x.inc")


class RendererFormattingTests(unittest.TestCase):

    def _rows(self):
        return [ptm.Row("CHAR_NAME", "char_name", ptm.FIXED, 64, 6),
                ptm.Row("DLG1", "dlg1", ptm.POINTER, 1574, 0)]

    def test_inc_rows_render_named_class_stem_kind_and_decimals(self):
        inc = ptm.render_inc(self._rows())
        self.assertIn(".id = TextClass::CHAR_NAME,", inc)
        self.assertIn('.fileStem = "char_name",', inc)
        self.assertIn(".kind = TextClassKind::FIXED,", inc)
        self.assertIn(".recordSize = 6 },", inc)
        self.assertIn(".id = TextClass::DLG1,", inc)
        self.assertIn('.fileStem = "dlg1",', inc)
        self.assertIn(".kind = TextClassKind::POINTER,", inc)
        self.assertIn(".recordSize = 0 },", inc)

    def test_fixture_rows_render_decimal_id_kind_and_values(self):
        fixture = ptm.render_fixture(self._rows())
        self.assertIn("std::array<ExpectedTextClassMetadata, 2>", fixture)
        # recordCount is right-padded to the widest value's width (4 for 1574).
        self.assertIn(".id =  0, .kind = 0, .recordCount =   64, .recordSize = 6 },",
                      fixture)
        self.assertIn(".id =  1, .kind = 1, .recordCount = 1574, .recordSize = 0 },",
                      fixture)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "include", "text", "char_name_en.inc")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src include/text not present (rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        cls.text_inc = os.path.join(root, "include", "text")
        cls.src_text = os.path.join(root, "src", "text")
        cls.rows = ptm.build_rows(cls.text_inc, src_text_dir=cls.src_text)
        cls.by_enum = {r.enum: r for r in cls.rows}

    def test_corpus_shape(self):
        self.assertEqual(len(self.rows), 30)
        self.assertEqual(sum(1 for r in self.rows if r.kind == ptm.FIXED), 16)
        self.assertEqual(sum(1 for r in self.rows if r.kind == ptm.POINTER), 14)

    def test_known_fixed_boundary_values(self):
        # Independent transcription check against the disassembly.
        self.assertEqual((self.by_enum["CHAR_NAME"].record_count,
                          self.by_enum["CHAR_NAME"].record_size), (64, 6))
        self.assertEqual((self.by_enum["ITEM_NAME"].record_count,
                          self.by_enum["ITEM_NAME"].record_size), (256, 13))
        self.assertEqual((self.by_enum["MONSTER_NAME"].record_count,
                          self.by_enum["MONSTER_NAME"].record_size), (384, 10))
        self.assertEqual((self.by_enum["STATUS_NAME"].record_count,
                          self.by_enum["STATUS_NAME"].record_size), (32, 10))

    def test_known_pointer_counts(self):
        self.assertEqual(self.by_enum["DLG1"].record_count, 1574)
        self.assertEqual(self.by_enum["DLG2"].record_count, 1510)
        self.assertEqual(self.by_enum["DLG1"].record_size, 0)

    def test_dte_table_row(self):
        dte = self.by_enum["DTE_TABLE"]
        self.assertEqual((dte.kind, dte.record_count, dte.record_size),
                         (ptm.FIXED, 128, 2))

    def test_fixed_dat_sizes_match_count_times_size(self):
        # Every fixed class's .dat is exactly recordCount * recordSize bytes —
        # the loader round-trip invariant.
        for r in self.rows:
            if r.kind != ptm.FIXED:
                continue
            basename = next(b for b, e, _k, _o in ptm._REGISTRY if e == r.enum) \
                if r.enum != "DTE_TABLE" else "dte_tbl"
            dat = os.path.join(self.src_text, "{}_en.dat".format(basename))
            if not os.path.isfile(dat):
                continue
            self.assertEqual(os.path.getsize(dat),
                             r.record_count * r.record_size,
                             "{} .dat size != count*size".format(r.enum))

    def test_jp_skeletons_verified(self):
        # 28 JP skeletons in the U-ROM rip (every JP name/desc except the two
        # EN-only classes; mte_tbl is real and checked separately).
        self.assertEqual(ptm.verify_jp_skeletons(self.text_inc), 28)


if __name__ == "__main__":
    unittest.main()
