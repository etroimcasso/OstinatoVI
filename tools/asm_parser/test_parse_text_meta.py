#!/usr/bin/env python3
"""Unit tests for parse_text_meta.py.

Three layers per the parser-test discipline:
  1. pure helpers (read_class_inc reader, row_for_class validation, offset
     capture/ordering, offset-table combine, renderer formatting);
  2. synthetic inputs (skeleton scope, fixed-with-offsets, pointer count
     mismatch, malformed ARRAY_LENGTH, stray scope line, malformed/duplicate
     offset symbols, dlg2 shift + dup-zero preservation);
  3. end-to-end against the real original-src include/text/*.inc (skipped
     cleanly when the rip output is absent) — metadata rows AND offset tables.

Python 3 stdlib only. Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import json
import shutil
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

    def test_pointer_captures_offset_values(self):
        # _pointer_inc lays records at i*4; the reader must capture the hex
        # offset value, not merely count the symbol.
        meta = _read_inc(_pointer_inc(3, 3))
        self.assertEqual(meta.offsets, {0: 0, 1: 4, 2: 8})

    def test_malformed_offset_symbol_raises(self):
        # `_0 :=` with no `<Label> + $hex` right side is a grammar deviation.
        text = (".scope X\n        ARRAY_LENGTH = 1\n"
                "        _0 := X\n.endscope\n")
        with self.assertRaises(ParseError):
            _read_inc(text)

    def test_duplicate_offset_symbol_raises(self):
        text = (".scope X\n        ARRAY_LENGTH = 2\n"
                "        _0 := X + $0000\n        _0 := X + $0004\n.endscope\n")
        with self.assertRaises(ParseError):
            _read_inc(text)

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
        offsets = {i: i * 4 for i in range(offset_count)}
        return ptm.ClassMeta(array_length, item_size, offsets, has)

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


def _ptr_row(stem, enum, offsets):
    return ptm.Row(enum, stem, ptm.POINTER, len(offsets), 0, offsets=list(offsets))


class OrderedOffsetsTests(unittest.TestCase):

    def test_gap_raises(self):
        with self.assertRaises(ParseError):
            ptm._ordered_offsets({0: 0, 2: 8}, 3, "x", "x.inc")  # _1 missing

    def test_decreasing_raises(self):
        with self.assertRaises(ParseError):
            ptm._ordered_offsets({0: 0, 1: 8, 2: 4}, 3, "x", "x.inc")

    def test_duplicate_offset_ok(self):
        # A zero-length record (dlg1 _0/_1) yields equal consecutive offsets.
        self.assertEqual(ptm._ordered_offsets({0: 0, 1: 0, 2: 5}, 3, "x", "x.inc"),
                         [0, 0, 5])


class OffsetTableTests(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        # dlg1_en.dat length is the concatenation base for the dlg2 shift.
        with open(os.path.join(self.tmp, "dlg1_en.dat"), "wb") as fh:
            fh.write(b"\x00" * 10)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def _rows(self):
        return [_ptr_row("dlg1", "DLG1", [0, 0, 5]),
                _ptr_row("dlg2", "DLG2", [0, 3, 7]),
                _ptr_row("attack_msg", "ATTACK_MSG", [0, 9, 20])]

    def test_dialogue_shifts_dlg2_by_dlg1_len(self):
        tables = ptm.build_offset_tables(self._rows(), self.tmp)
        dlg = next(t for t in tables if t.cpp_name == "kDialogueOffsets")
        # dlg1 [0,0,5] then dlg2 [0,3,7] + 10 -> concatenated stream offsets.
        self.assertEqual(dlg.offsets, [0, 0, 5, 10, 13, 17])

    def test_dialogue_preserves_dup_zero(self):
        tables = ptm.build_offset_tables(self._rows(), self.tmp)
        dlg = next(t for t in tables if t.cpp_name == "kDialogueOffsets")
        self.assertEqual((dlg.offsets[0], dlg.offsets[1]), (0, 0))

    def test_self_contained_class_camelcased(self):
        tables = ptm.build_offset_tables(self._rows(), self.tmp)
        self.assertIn("kAttackMsgOffsets", [t.cpp_name for t in tables])

    def test_missing_dlg1_dat_raises(self):
        os.remove(os.path.join(self.tmp, "dlg1_en.dat"))
        with self.assertRaises(ParseError):
            ptm.build_offset_tables(self._rows(), self.tmp)

    def test_dlg2_nonzero_first_offset_raises(self):
        rows = [_ptr_row("dlg1", "DLG1", [0, 0, 5]),
                _ptr_row("dlg2", "DLG2", [2, 3, 7]),
                _ptr_row("attack_msg", "ATTACK_MSG", [0, 9])]
        with self.assertRaises(ParseError):
            ptm.build_offset_tables(rows, self.tmp)


class OffsetRendererTests(unittest.TestCase):

    def _tables(self):
        return [ptm.OffsetTable("kDialogueOffsets", [0, 0, 5, 10], "d"),
                ptm.OffsetTable("kAttackMsgOffsets", [0, 9], "a")]

    def test_inc_emits_named_u32_arrays(self):
        inc = ptm.render_offsets_inc(self._tables())
        self.assertIn("constexpr std::uint32_t kDialogueOffsets[4] = {", inc)
        self.assertIn("constexpr std::uint32_t kAttackMsgOffsets[2] = {", inc)
        self.assertIn("0, 0, 5, 10,", inc)

    def test_fixture_prefixes_expected_and_namespaces(self):
        fixture = ptm.render_offsets_fixture(self._tables())
        self.assertIn("kExpectedDialogueOffsets[4]", fixture)
        self.assertIn("kExpectedAttackMsgOffsets[2]", fixture)
        self.assertIn("namespace ostinato::test", fixture)


def _write_char_tables(dirpath):
    tables = {
        "null_terminated_en": {"0x00": "{0}", "0x01": "{n}", "0xFF": " "},
        "text_en": {"0x9A": "a", "0x9E": "e", "0xC3": ["'", "’"]},
        "big_symbols_en": {"0xD6": "{holy}"},
    }
    for name, tbl in tables.items():
        with open(os.path.join(dirpath, name + ".json"), "w",
                  encoding="utf-8") as fh:
            json.dump(tbl, fh)


class GlyphMapTests(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        _write_char_tables(self.tmp)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_single_and_ambiguous_split(self):
        single, ambiguous = ptm.load_glyph_map(self.tmp)
        self.assertEqual(single[0x9A], "a")
        self.assertEqual(single[0xFF], " ")
        # A list-valued entry is ambiguous — not usable for an exact decode.
        self.assertIn(0xC3, ambiguous)
        self.assertNotIn(0xC3, single)

    def test_missing_table_raises(self):
        os.remove(os.path.join(self.tmp, "text_en.json"))
        with self.assertRaises(ParseError):
            ptm.load_glyph_map(self.tmp)


class DecodeMenuRecordTests(unittest.TestCase):

    def setUp(self):
        self.single = {0x9A: "a", 0x9E: "e", 0x01: "{n}", 0xFF: " "}
        self.amb = {0xC3}

    def test_stops_at_terminator(self):
        s, ok, term = ptm.decode_menu_record(b"\x9a\x9e\x00\x9a",
                                             self.single, self.amb)
        self.assertEqual((s, ok, term), ("ae", True, True))

    def test_ambiguous_byte_flags_not_ok(self):
        _s, ok, term = ptm.decode_menu_record(b"\x9a\xc3\x00",
                                              self.single, self.amb)
        self.assertFalse(ok)
        self.assertTrue(term)

    def test_unmapped_byte_flags_not_ok(self):
        _s, ok, _t = ptm.decode_menu_record(b"\x9a\x55\x00",
                                            self.single, self.amb)
        self.assertFalse(ok)

    def test_no_terminator_flagged(self):
        s, _ok, term = ptm.decode_menu_record(b"\x9a\x9e", self.single, self.amb)
        self.assertFalse(term)
        self.assertEqual(s, "ae")


class BuildMenuDescTests(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.ct = os.path.join(self.tmp, "char_table")
        self.st = os.path.join(self.tmp, "src_text")
        os.makedirs(self.ct)
        os.makedirs(self.st)
        _write_char_tables(self.ct)
        self._saved = ptm._MENU_DESC_STEMS
        ptm._MENU_DESC_STEMS = ["foo_desc"]

    def tearDown(self):
        ptm._MENU_DESC_STEMS = self._saved
        shutil.rmtree(self.tmp)

    def _corpus(self, dat, text):
        with open(os.path.join(self.st, "foo_desc_en.dat"), "wb") as fh:
            fh.write(dat)
        with open(os.path.join(self.st, "foo_desc_en.json"), "w",
                  encoding="utf-8") as fh:
            json.dump({"text": text}, fh)

    def test_happy_decode_matches_upstream(self):
        self._corpus(b"\x9a\x9e\x00\x9e\x00", ["ae", "e"])
        _s, _a, classes = ptm.build_menu_desc(
            [_ptr_row("foo_desc", "FOO_DESC", [0, 3])], self.st, self.ct)
        self.assertEqual(classes[0].records[0], ("ae", True))
        self.assertEqual(classes[0].records[1], ("e", True))

    def test_mismatch_raises(self):
        self._corpus(b"\x9a\x9e\x00", ["WRONG"])
        with self.assertRaises(ParseError):
            ptm.build_menu_desc([_ptr_row("foo_desc", "FOO_DESC", [0])],
                                self.st, self.ct)

    def test_shared_pointer_alias_not_checked(self):
        # rec0 and rec1 share offset 0 (rec0 is zero-length); the upstream text
        # duplicates the shared string. rec0 is not independently checkable.
        self._corpus(b"\x9a\x9e\x00", ["ae", "ae"])
        _s, _a, classes = ptm.build_menu_desc(
            [_ptr_row("foo_desc", "FOO_DESC", [0, 0])], self.st, self.ct)
        self.assertEqual(classes[0].records[0][1], False)
        self.assertEqual(classes[0].records[1], ("ae", True))

    def test_count_mismatch_raises(self):
        self._corpus(b"\x9a\x00", ["a", "extra"])  # 2 texts, 1 record
        with self.assertRaises(ParseError):
            ptm.build_menu_desc([_ptr_row("foo_desc", "FOO_DESC", [0])],
                                self.st, self.ct)


class MenuDescRendererTests(unittest.TestCase):

    def test_c_escape_handles_quotes_and_backslash(self):
        self.assertEqual(ptm._c_escape('a"b\\c'), 'a\\"b\\\\c')

    def test_renders_glyph_map_and_expected(self):
        classes = [ptm.MenuDescClass("foo_desc", "FooDesc",
                                     [("ae", True), ("x'", False)])]
        out = ptm.render_menu_desc_fixture({0x9A: "a", 0x01: "{n}"}, set(),
                                           classes)
        self.assertIn("const char* kEnGlyph[256]", out)
        self.assertIn("kFooDescExpected[2]", out)
        self.assertIn('{ "ae", true }', out)
        self.assertIn('{ "x\'", false }', out)


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
        cls.char_table = os.path.join(root, "tools", "char_table")
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

    def test_offset_tables_against_real_corpus(self):
        tables = ptm.build_offset_tables(self.rows, self.src_text)
        by = {t.cpp_name: t for t in tables}
        # dlg1 (1574) + dlg2 (1510) collapse into one combined array.
        dlg = by["kDialogueOffsets"].offsets
        self.assertEqual(len(dlg), 3084)
        self.assertEqual((dlg[0], dlg[1]), (0, 0))        # _0/_1 duplicate
        self.assertEqual(dlg[1573], 0xffed)               # last dlg1 offset
        self.assertEqual(dlg[1574], os.path.getsize(      # first dlg2 = dlg1 len
            os.path.join(self.src_text, "dlg1_en.dat")))
        self.assertTrue(all(dlg[i] <= dlg[i + 1] for i in range(len(dlg) - 1)))
        # A self-contained class: offsets into its own .dat, last < file size.
        am = by["kAttackMsgOffsets"].offsets
        self.assertEqual(len(am), 256)
        self.assertEqual(by["kMapTitleOffsets"].offsets[:3], [0, 1, 5])
        self.assertLess(am[-1], os.path.getsize(
            os.path.join(self.src_text, "attack_msg_en.dat")))

    def test_menu_desc_cross_check_against_upstream_json(self):
        # The eight description classes decode (through the char tables) to the
        # upstream reference text for every independently-checkable record.
        _single, _amb, classes = ptm.build_menu_desc(
            self.rows, self.src_text, self.char_table)
        self.assertEqual(len(classes), 8)
        total = sum(len(c.records) for c in classes)
        clean = sum(1 for c in classes for _t, ok in c.records if ok)
        self.assertEqual(total, 426)
        self.assertEqual(clean, 322)
        item = next(c for c in classes if c.stem == "item_desc")
        # Record 2 carries an apostrophe (a list-valued glyph) -> not checkable;
        # record 3 is punctuation-free and cross-checks exactly.
        self.assertFalse(item.records[2][1])
        self.assertEqual(item.records[3], ("Wind-elemental", True))


if __name__ == "__main__":
    unittest.main()
