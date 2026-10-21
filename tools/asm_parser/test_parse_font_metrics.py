#!/usr/bin/env python3
"""Unit tests for parse_font_metrics.py.

Three layers, matching the parser test discipline: synthetic sources exercising
the grammar and every error path, the placement checks against hand-made rows
and hand-made cartridge images, and an end-to-end pass over the real disassembly
and cartridge (skipped when either is absent) pinning where each family lives.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_font_metrics as pfm  # noqa: E402
import parse_rom_regions as prr  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    root = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(root, "src", "gfx", "font_gfx.asm")):
        return root
    return None


def _write(root, relative, text):
    path = os.path.join(root, relative)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


def _en_rows(count=pfm.EN_WIDTH_COUNT):
    """`count` widths as .byte rows of sixteen, every width 8."""
    rows = []
    for start in range(0, count, 16):
        rows.append("        .byte   " + ",".join(
            "8" for _ in range(min(16, count - start))))
    return "\n".join(rows) + "\n"


def _font_source(en_rows=None, jp=None, before="", between=""):
    if en_rows is None:
        en_rows = _en_rows()
    if jp is None:
        jp = ("        .res $0420, 12\n"
              "        .res $00b4, 8\n"
              "        .res $002c, 12\n")
    return ('.segment "font_gfx"\n'
            + before +
            "SmallFontGfx:\n"
            '        incbin_lang "small_font_%s.2bpp"\n'
            + between +
            "FontWidth:\n"
            ".if LANG_EN\n"
            + en_rows +
            ".else\n"
            + jp +
            ".endif\n"
            "\n"
            "LargeFontGfx:\n"
            '        incbin_lang "large_font_%s.1bpp"\n'
            "\n"
            '.segment "debug_font_gfx"\n'
            "DebugFontGfx:\n")


ARROW = [0x20, 0x30, 0x38, 0x3C, 0x3E, 0x3F, 0x3E, 0x3C, 0x38, 0x30, 0x20, 0x00]


def _cursor_source(rows=None, loads=1):
    if rows is None:
        rows = ARROW
    body = "".join("        .byte   %{:08b}\n".format(r) for r in rows)
    load = "        lda     f:DlgCursorGfx,x\n" * loads
    return (".proc DrawDlgCursor\n"
            + load +
            "        rts\n"
            "DlgCursorGfx:\n"
            + body +
            ".endproc\n")


class _Row(object):
    def __init__(self, asset, begin, end):
        self.asset = asset
        self.begin = begin
        self.end = end


def _bracketing_rows(gap_en=pfm.EN_WIDTH_COUNT, gap_jp=pfm.JP_WIDTH_COUNT):
    small_end = 0xC48FBF
    return {
        "en": [_Row("SMALL_FONT", 0xC47FC0, small_end),
               _Row("LARGE_FONT", small_end + 1 + gap_en, 0xC4A4BF)],
        "jp": [_Row("SMALL_FONT", 0xC47FC0, small_end),
               _Row("LARGE_FONT", small_end + 1 + gap_jp, 0xC4FFFF)],
    }


# --- Layer 1: the source grammar ----------------------------------------------

class FontWidthSourceTest(unittest.TestCase):

    def _read(self, text):
        with tempfile.TemporaryDirectory() as root:
            _write(root, "src/gfx/font_gfx.asm", text)
            return pfm.read_font_widths(root)

    def test_a_well_formed_table_parses(self):
        widths = self._read(_font_source())
        self.assertEqual(len(widths.en_widths), pfm.EN_WIDTH_COUNT)
        self.assertEqual(widths.jp_count, pfm.JP_WIDTH_COUNT)
        self.assertEqual(widths.line, 4)

    def test_a_label_between_the_fonts_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self._read(_font_source(between="Stray:\n"))
        self.assertIn("between the two fonts", str(caught.exception))

    def test_a_label_before_the_small_font_is_an_error(self):
        with self.assertRaises(ParseError):
            self._read(_font_source(before="Leading:\n"))

    def test_a_short_english_table_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self._read(_font_source(en_rows=_en_rows(255)))
        self.assertIn("255 widths", str(caught.exception))

    def test_a_wrong_japanese_reservation_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self._read(_font_source(jp="        .res $0500, 12\n"
                                       "        .res 1, 12\n"))
        self.assertIn("1281", str(caught.exception))

    def test_an_unexpected_line_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self._read(_font_source(jp="        .word   $1234\n"))
        self.assertIn("unexpected line", str(caught.exception))

    def test_a_table_without_a_language_branch_is_an_error(self):
        text = _font_source().replace(".else\n", "").replace(
            "        .res $0420, 12\n        .res $00b4, 8\n"
            "        .res $002c, 12\n", "")
        with self.assertRaises(ParseError):
            self._read(text)


class CursorSourceTest(unittest.TestCase):

    def _read(self, text):
        with tempfile.TemporaryDirectory() as root:
            _write(root, "src/field/text.asm", text)
            return pfm.read_cursor_art(root)

    def test_binary_rows_parse(self):
        art = self._read(_cursor_source())
        self.assertEqual(art.rows, ARROW)

    def test_a_short_arrow_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self._read(_cursor_source(rows=ARROW[:11]))
        self.assertIn("11 rows", str(caught.exception))

    def test_a_second_reader_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self._read(_cursor_source(loads=2))
        self.assertIn("2 instructions", str(caught.exception))

    def test_a_missing_label_is_an_error(self):
        with self.assertRaises(ParseError):
            self._read(_cursor_source().replace("DlgCursorGfx:", "Other:"))


class RevisionConditionalTest(unittest.TestCase):

    def _check(self, field_text, include_text=""):
        with tempfile.TemporaryDirectory() as root:
            _write(root, "src/field/text.asm", field_text)
            _write(root, "include/const.inc",
                   "def_config ROM_VERSION, 0\n"
                   ".if (LANG_EN && ROM_VERSION)\n"
                   "        LANG_EN_REV1 = 1\n"
                   ".endif\n")
            _write(root, "include/macros.inc", include_text)
            return pfm.assert_no_revision_conditionals(root)

    def test_language_branches_are_allowed(self):
        self.assertEqual(self._check(".if LANG_EN\n        nop\n.endif\n"), 2)

    def test_a_revision_branch_in_the_field_code_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self._check(".if LANG_EN_REV1\n        nop\n.endif\n")
        self.assertIn("US revision", str(caught.exception))

    def test_the_revision_byte_in_the_header_is_not_a_branch(self):
        self.assertEqual(self._check(".byte   ROM_VERSION\n"), 2)

    def test_an_include_using_a_revision_symbol_is_an_error(self):
        with self.assertRaises(ParseError):
            self._check("        nop\n", ".if ROM_VERSION\n.endif\n")


# --- Layer 2: placement --------------------------------------------------------

def _widths(values=None):
    values = values if values is not None else [8] * pfm.EN_WIDTH_COUNT
    return pfm.FontWidths(path="font_gfx.asm", line=14, en_widths=values,
                          jp_count=pfm.JP_WIDTH_COUNT)


def _rom_with(at, payload, size=0x400000):
    rom = bytearray(size)
    offset = common.hirom_file_offset(at)
    rom[offset:offset + len(payload)] = bytes(payload)
    return rom


class FontWidthPlacementTest(unittest.TestCase):

    def test_both_languages_sit_between_the_fonts(self):
        placed = pfm.place_font_widths(_widths(), _bracketing_rows())
        self.assertEqual([(p.language, p.begin, p.size) for p in placed],
                         [("en", 0xC48FC0, 256), ("jp", 0xC48FC0, 1280)])
        self.assertEqual([p.proved for p in placed], [0, 0])

    def test_a_gap_that_does_not_fit_the_table_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            pfm.place_font_widths(_widths(), _bracketing_rows(gap_jp=1279))
        self.assertIn("1279 bytes", str(caught.exception))

    def test_the_english_widths_are_compared_with_the_cartridge(self):
        values = [8] * pfm.EN_WIDTH_COUNT
        placed = pfm.place_font_widths(_widths(values), _bracketing_rows(),
                                       _rom_with(0xC48FC0, values))
        self.assertEqual(placed[0].proved, 256)

    def test_a_cartridge_mismatch_names_the_glyph(self):
        cartridge = [8] * pfm.EN_WIDTH_COUNT
        cartridge[0x7F] = 5
        with self.assertRaises(ParseError) as caught:
            pfm.place_font_widths(_widths(), _bracketing_rows(),
                                  _rom_with(0xC48FC0, cartridge))
        self.assertIn("glyph $7f", str(caught.exception))

    def test_a_missing_neighbor_is_an_error(self):
        rows = _bracketing_rows()
        rows["jp"] = rows["jp"][:1]
        with self.assertRaises(ParseError):
            pfm.place_font_widths(_widths(), rows)


class CursorPlacementTest(unittest.TestCase):

    ART = pfm.CursorArt(path="text.asm", line=1184, rows=ARROW)

    @staticmethod
    def _load_of(at):
        return [pfm.LDA_LONG_X, at & 0xFF, (at >> 8) & 0xFF, at >> 16]

    def test_the_arrow_and_its_reader_place_it(self):
        rom = _rom_with(0xC08832, ARROW)
        rom[0x1000:0x1004] = bytes(self._load_of(0xC08832))
        placed = pfm.place_cursor_art(self.ART, rom)
        self.assertEqual([(p.language, p.begin, p.size, p.proved)
                          for p in placed], [("en", 0xC08832, 12, 12)])

    def test_two_copies_of_the_arrow_are_an_error(self):
        rom = _rom_with(0xC08832, ARROW)
        rom[0x2000:0x200C] = bytes(ARROW)
        with self.assertRaises(ParseError) as caught:
            pfm.place_cursor_art(self.ART, rom)
        self.assertIn("2 times", str(caught.exception))

    def test_an_arrow_nothing_reads_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            pfm.place_cursor_art(self.ART, _rom_with(0xC08832, ARROW))
        self.assertIn("0 long loads", str(caught.exception))

    def test_the_arrow_outside_the_field_bank_is_not_found(self):
        with self.assertRaises(ParseError) as caught:
            pfm.place_cursor_art(self.ART, _rom_with(0xC18832, ARROW))
        self.assertIn("0 times", str(caught.exception))

    def test_no_cartridge_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            pfm.place_cursor_art(self.ART, None)
        self.assertIn("FF6_VANILLA_ROM", str(caught.exception))


# --- Layer 3: the real corpus and cartridge ------------------------------------

@unittest.skipIf(_source_root() is None, "no original-src checkout")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _source_root()
        size = prr.read_rom_size(cls.root)
        cls.rows = {language: prr.read_rip_list(cls.root, language, size)
                    for language in ("en", "jp")}
        cls.widths = pfm.read_font_widths(cls.root)

    def test_the_english_widths_hold_the_traced_values(self):
        widths = self.widths.en_widths
        # font_gfx.asm:16-31 — the control codes are 12 wide, the space 5, the
        # wide space 6.
        self.assertEqual(widths[0x00], 12)
        self.assertEqual(widths[0x7F], 5)
        self.assertEqual(widths[0xFE], 12)
        self.assertEqual(widths[0xFF], 6)
        self.assertEqual(max(widths), 12)

    def test_the_japanese_reservation(self):
        self.assertEqual(self.widths.jp_count, 1280)

    def test_the_tables_sit_between_the_fonts(self):
        placed = pfm.place_font_widths(self.widths, self.rows)
        self.assertEqual([(p.language, p.begin, p.size) for p in placed],
                         [("en", 0xC48FC0, 256), ("jp", 0xC48FC0, 1280)])

    def test_the_field_code_never_branches_on_the_revision(self):
        self.assertGreater(pfm.assert_no_revision_conditionals(self.root), 30)

    @unittest.skipIf(common.find_vanilla_rom(_source_root()) is None,
                     "no vanilla cartridge (set FF6_VANILLA_ROM)")
    def test_the_cartridge_holds_the_widths_and_the_arrow(self):
        rom = common.load_vanilla_rom(self.root)
        placed = pfm.place_font_widths(self.widths, self.rows, rom)
        self.assertEqual(placed[0].proved, 256)
        arrow = pfm.place_cursor_art(pfm.read_cursor_art(self.root), rom)
        self.assertEqual([(p.language, p.begin, p.size) for p in arrow],
                         [("en", 0xC08832, 12)])


if __name__ == "__main__":
    unittest.main()
