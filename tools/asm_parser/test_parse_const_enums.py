#!/usr/bin/env python3
"""Unit tests for common.py + parse_const_enums.py.

Three layers per the parser-test discipline:
  1. pure-helper math (literals, comment split, version-axis classification);
  2. synthetic ca65 fragments exercising every grammar form + every error path;
  3. end-to-end against the real original-src/include/const.inc (skipped cleanly
     when the disassembly clone is absent, e.g. a contributor without it).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import common
import parse_const_enums as pce
from common import ParseError


def parse_fragment(text, skip_body_enums=None):
    """Parse a synthetic ca65 fragment (written to a temp file)."""
    fd, path = tempfile.mkstemp(suffix=".inc", text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text)
        return common.parse_ca65_constants(path, skip_body_enums=skip_body_enums)
    finally:
        os.remove(path)


# --- Layer 1: pure helpers --------------------------------------------------

class HelperTests(unittest.TestCase):

    def test_int_literal_hex(self):
        self.assertEqual(common.parse_int_literal("$0c"), 12)
        self.assertEqual(common.parse_int_literal("$FF"), 255)
        self.assertEqual(common.parse_int_literal("$0000"), 0)

    def test_int_literal_binary(self):
        self.assertEqual(common.parse_int_literal("%00000001"), 1)
        self.assertEqual(common.parse_int_literal("%10000000"), 128)

    def test_int_literal_decimal(self):
        self.assertEqual(common.parse_int_literal("0"), 0)
        self.assertEqual(common.parse_int_literal("9999999"), 9999999)

    def test_int_literal_non_literal(self):
        self.assertIsNone(common.parse_int_literal("FOO"))
        self.assertIsNone(common.parse_int_literal(""))
        self.assertIsNone(common.parse_int_literal("$zz"))
        self.assertIsNone(common.parse_int_literal("%12"))

    def test_strip_comment(self):
        self.assertEqual(common.strip_comment("FOO = 1  ; note"), ("FOO = 1", "note"))
        self.assertEqual(common.strip_comment("FOO"), ("FOO", None))
        self.assertEqual(common.strip_comment("   ; only"), ("", "only"))
        self.assertEqual(common.strip_comment("BAR ;= $02"), ("BAR", "= $02"))

    def test_version_variant_classification(self):
        self.assertTrue(common.is_version_variant_condition("LANG_EN"))
        self.assertTrue(common.is_version_variant_condition("(LANG_EN && ROM_VERSION)"))
        self.assertTrue(common.is_version_variant_condition("DEBUG"))
        self.assertFalse(common.is_version_variant_condition("CONST_INC"))
        self.assertFalse(common.is_version_variant_condition("_const"))


# --- Layer 2: synthetic grammar --------------------------------------------

class EnumGrammarTests(unittest.TestCase):

    def test_auto_increment(self):
        p = parse_fragment(".enum COLOR\n RED\n GREEN\n BLUE\n.endenum\n")
        e = p.enum("COLOR")
        self.assertEqual([(m.name, m.value) for m in e.members],
                         [("RED", 0), ("GREEN", 1), ("BLUE", 2)])

    def test_explicit_then_resume(self):
        # ca65: after an explicit value, auto-increment resumes at value+1.
        p = parse_fragment(".enum E\n A\n B = 200\n C\n.endenum\n")
        e = p.enum("E")
        self.assertEqual([m.value for m in e.members], [0, 200, 201])

    def test_hex_and_binary_and_bit(self):
        p = parse_fragment(
            "BIT_0 = %00000001\nBIT_7 = %10000000\n"
            ".enum F\n LO = $01\n HI = BIT_7\n.endenum\n")
        e = p.enum("F")
        self.assertEqual(e.value_of("LO"), 1)
        self.assertEqual(e.value_of("HI"), 128)

    def test_same_enum_alias_preserved(self):
        p = parse_fragment(".enum D\n UP\n UP_RIGHT\n RIGHT_UP = UP_RIGHT\n DN\n.endenum\n")
        e = p.enum("D")
        self.assertEqual(e.value_of("UP_RIGHT"), 1)
        self.assertEqual(e.value_of("RIGHT_UP"), 1)
        # counter resumes at alias value + 1
        self.assertEqual(e.value_of("DN"), 2)
        alias = [m for m in e.members if m.name == "RIGHT_UP"][0]
        self.assertEqual(alias.rhs_kind, "same_alias")
        self.assertEqual(alias.rhs_symbol, "UP_RIGHT")

    def test_cross_enum_reference(self):
        p = parse_fragment(
            ".enum CHAR\n TERRA\n LOCKE\n.endenum\n"
            ".enum OBJ\n TERRA = CHAR::TERRA\n LOCKE = CHAR::LOCKE\n.endenum\n")
        obj = p.enum("OBJ")
        self.assertEqual(obj.value_of("TERRA"), 0)
        self.assertEqual(obj.value_of("LOCKE"), 1)
        m = [x for x in obj.members if x.name == "TERRA"][0]
        self.assertEqual(m.rhs_kind, "cross_ref")
        self.assertEqual(m.rhs_symbol, "CHAR::TERRA")

    def test_doc_value_comment_matches(self):
        # A matching ';= N' comment is fine (extra structural check).
        p = parse_fragment(".enum G\n A ;= 0\n B ;= 1\n.endenum\n")
        self.assertEqual(p.enum("G").value_of("B"), 1)

    def test_doc_value_comment_mismatch_raises(self):
        with self.assertRaises(ParseError):
            parse_fragment(".enum G\n A\n B ;= 5\n.endenum\n")  # B computes 1, not 5

    def test_skip_body_enum_not_evaluated(self):
        # A skipped body may contain '<<'/'::' the single-term resolver rejects.
        p = parse_fragment(
            ".enum S1\n X = 1\n.endenum\n"
            ".enum S12\n X = S1::X << 8\n.endenum\n",
            skip_body_enums={"S12"})
        s12 = p.enum("S12")
        self.assertTrue(s12.skipped)
        self.assertEqual(s12.members, [])

    def test_macro_body_skipped(self):
        p = parse_fragment(
            ".macro def_config _c, _v\n _c = _v\n.endmacro\n"
            ".enum E\n A\n.endenum\n")
        self.assertIsNotNone(p.enum("E"))

    def test_benign_include_guard_allows_enum(self):
        p = parse_fragment(
            ".ifndef CONST_INC\nCONST_INC = 1\n.enum E\n A\n.endenum\n.endif\n")
        self.assertIsNotNone(p.enum("E"))

    # --- error paths ---

    def test_enum_inside_version_guard_raises(self):
        # An emitted enum inside a version-variant conditional is a hard error.
        with self.assertRaises(ParseError) as ctx:
            parse_fragment(".if LANG_EN\n.enum E\n A\n.endenum\n.endif\n")
        self.assertIn("VERSION-VARIANT", str(ctx.exception))

    def test_unknown_symbol_raises(self):
        with self.assertRaises(ParseError):
            parse_fragment(".enum E\n A = MISSING\n.endenum\n")

    def test_unterminated_enum_raises(self):
        with self.assertRaises(ParseError):
            parse_fragment(".enum E\n A\n")

    def test_endenum_without_enum_raises(self):
        with self.assertRaises(ParseError):
            parse_fragment(".endenum\n")

    def test_unrecognized_file_scope_line_raises(self):
        with self.assertRaises(ParseError):
            parse_fragment("this is not valid asm\n")


# --- Layer 2b: parse_const_enums structural asserts -------------------------

_STATUS_FRAGMENT_HEADER = "".join(
    "BIT_{} = {}\n".format(i, 1 << i) for i in range(8))


def _status_fragment(b4_override=None):
    """A minimal STATUS_ID + STATUS1..4 fragment for the status-layout assert.

    32 sequential ids in four banks of 8; each bank a BIT_0..7 set — aligned by
    default. Pass b4_override (e.g. "BIT_3") to force the B4 member (bank 2,
    bit 4) to a wrong bit and produce a misalignment.
    """
    names = [
        # bank 1
        "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
        # bank 2
        "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7",
        # bank 3
        "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7",
        # bank 4
        "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    ]
    out = [_STATUS_FRAGMENT_HEADER]
    out.append(".enum STATUS_ID\n")
    for n in names:
        out.append(" {}\n".format(n))
    out.append(".endenum\n")
    for bank in range(4):
        out.append(".enum STATUS{}\n".format(bank + 1))
        for bit in range(8):
            member = names[bank * 8 + bit]
            # inject the deliberate misalignment on B4 only if requested
            if member == "B4" and b4_override:
                value = b4_override
            else:
                value = "BIT_{}".format(bit)
            out.append(" {} = {}\n".format(member, value))
        out.append(" NONE = 0\n")
        out.append(".endenum\n")
    return "".join(out)


class StructuralAssertTests(unittest.TestCase):

    def test_status_layout_aligned_passes(self):
        p = parse_fragment(_status_fragment())  # B4 = BIT_4, correct
        pce.assert_status_layout(p)  # must not raise

    def test_status_layout_misaligned_raises(self):
        # B4 (STATUS_ID index 12, bank 2 bit 4) forced to BIT_3.
        p = parse_fragment(_status_fragment(b4_override="BIT_3"))
        with self.assertRaises(ParseError):
            pce.assert_status_layout(p)

    def test_coverage_unknown_enum_raises(self):
        p = parse_fragment(".enum TOTALLY_NEW\n A\n.endenum\n")
        with self.assertRaises(ParseError) as ctx:
            pce.assert_coverage(p)
        self.assertIn("disposition", str(ctx.exception))


# --- Layer 3: end-to-end against the real contract --------------------------

def _find_const_inc():
    here = os.path.dirname(os.path.abspath(__file__))
    for root in (
        os.path.join(here, "..", "..", "original-src"),  # in-repo submodule
    ):
        candidate = os.path.join(root, "include", "const.inc")
        if os.path.isfile(candidate):
            return candidate
    return None


@unittest.skipUnless(_find_const_inc(),
                     "original-src/include/const.inc not present (disassembly clone absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.parsed = common.parse_ca65_constants(
            _find_const_inc(), skip_body_enums=pce.SKIP)

    def test_counts(self):
        self.assertEqual(len(self.parsed.enum("ITEM").members), 256)
        self.assertEqual(len(self.parsed.enum("ATTACK").members), 256)
        self.assertEqual(len(self.parsed.enum("MONSTER").members), 384)
        self.assertEqual(len(self.parsed.enum("STATUS_ID").members), 32)
        # CHAR_PROP is the 64-value char_prop record index space,
        # emitted as CharacterPropId — one enumerator per record, incl. padding.
        self.assertEqual(len(self.parsed.enum("CHAR_PROP").members), 64)

    def test_boundary_values(self):
        self.assertEqual(self.parsed.enum("ITEM").value_of("DIRK"), 0x00)
        self.assertEqual(self.parsed.enum("ELEMENT").value_of("WATER"), 0x80)
        self.assertEqual(self.parsed.enum("ELEMENT").value_of("NONE"), 0)
        self.assertEqual(self.parsed.enum("STATUS_ID").value_of("FLOAT"), 31)
        self.assertEqual(self.parsed.enum("GENJU_BONUS").value_of("NONE"), 0xFF)
        self.assertEqual(self.parsed.enum("TARGET").value_of("MENU"), 0xFF)
        # CHAR_PROP record index boundaries: first record, first Kefka
        # variant, and the final beta slot — the sequential $00..$3f span.
        char_prop = self.parsed.enum("CHAR_PROP")
        self.assertEqual(char_prop.value_of("TERRA"), 0x00)
        self.assertEqual(char_prop.value_of("KEFKA_1"), 0x29)
        self.assertEqual(char_prop.value_of("HO"), 0x3F)

    def test_alias_and_cross_ref(self):
        d = self.parsed.enum("EVENT_DIR")
        self.assertEqual(d.value_of("RIGHT_UP"), d.value_of("UP_RIGHT"))
        obj = self.parsed.enum("EVENT_OBJ")
        char = self.parsed.enum("CHAR")
        self.assertEqual(obj.value_of("TERRA"), char.value_of("TERRA"))

    def test_coverage_and_status_layout(self):
        pce.assert_coverage(self.parsed)
        pce.assert_status_layout(self.parsed)


if __name__ == "__main__":
    unittest.main()
