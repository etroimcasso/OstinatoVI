#!/usr/bin/env python3
"""Unit tests for parse_char_prop.py.

Three layers per the parser-test discipline:
  1. pure helpers (arg-count guards, row/renderer formatting);
  2. synthetic char_prop DSL fragments exercising every macro form + every
     error path, against a miniature const.inc symbol set;
  3. end-to-end against the real original-src (skipped cleanly when the
     disassembly clone is absent).

Python 3 stdlib only (unittest, tempfile). Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_char_prop as pcp
from common import ParseError


# A miniature const.inc carrying every symbol scope the DSL resolves against.
MINI_CONST = """\
.enum BATTLE_CMD
        FIGHT = $00
        ITEM = $01
        MAGIC = $02
        MORPH = $03
        NONE = $ff
.endenum
.enum ITEM
        DIRK = $00
        BUCKLER = $5a
        EMPTY = $ff
.endenum
.enum CHAR_RUN_FACTOR
        HIGH = $00
        NORMAL = $01
        LOW = $02
        VERY_LOW = $03
        MASK = $03
.endenum
.enum CHAR_LEVEL_MOD
        NORMAL = $00
        HIGH = $04
        VERY_HIGH = $08
        LOW = $0c
        MASK = $0c
.endenum
CHAR_PROP_FIXED_EQUIP = $10
.enum CHAR_PROP
        ALPHA = $00
        BETA = $01
        GAMMA = $02
.endenum
"""

# One fully-overridden record, one all-defaults record, one empty record —
# matching MINI_CONST's 3-slot CHAR_PROP index space.
MINI_ASM = """\
.export CharProp
.segment "char_prop"
CharProp:
; 0: alpha
        make_char_prop
        set_char_prop_hp_mp 10, 5
        set_char_prop_cmds FIGHT, MORPH
        set_char_prop_stats 1, 2, 3, 4, 5, 6, 7, 8, 9
        set_char_prop_equip DIRK, BUCKLER
        set_char_prop_relics BUCKLER
        set_char_prop_run_factor LOW
        set_char_prop_level_mod HIGH
        set_char_prop_fixed_equip
        end_char_prop
; 1: beta (all defaults)
        make_char_prop
        end_char_prop
; 2: gamma (padding)
        empty_char_prop 1
"""


class _TempFiles(object):
    """Context helper writing const.inc / char_prop.asm fragments to disk."""

    def __init__(self, const_text=MINI_CONST, asm_text=MINI_ASM):
        self.const_text = const_text
        self.asm_text = asm_text

    def __enter__(self):
        self.dir = tempfile.TemporaryDirectory()
        self.const_path = os.path.join(self.dir.name, "const.inc")
        self.asm_path = os.path.join(self.dir.name, "char_prop.asm")
        with open(self.const_path, "w", encoding="utf-8") as fh:
            fh.write(self.const_text)
        with open(self.asm_path, "w", encoding="utf-8") as fh:
            fh.write(self.asm_text)
        return self

    def __exit__(self, *exc):
        self.dir.cleanup()

    def parse(self):
        symbols = pcp._Symbols(self.const_path)
        return pcp.parse_char_prop(self.asm_path, symbols)


def parse_mini(asm_text=MINI_ASM):
    with _TempFiles(asm_text=asm_text) as tf:
        return tf.parse()


# --- Layer 1: pure helpers ---------------------------------------------------

class HelperTests(unittest.TestCase):

    def test_require_exact_ignores_blank_args(self):
        # 'end_char_prop' style: a lone '' from split counts as zero args.
        pcp._require_exact([""], 0, "f", 1, "x")  # must not raise

    def test_require_exact_wrong_count_raises(self):
        with self.assertRaises(ParseError):
            pcp._require_exact(["a", "b"], 1, "f", 1, "x")

    def test_require_max_bounds(self):
        pcp._require_max(["a"], 4, "f", 1, "x")           # 1..4 ok
        with self.assertRaises(ParseError):
            pcp._require_max([], 4, "f", 1, "x")          # zero args
        with self.assertRaises(ParseError):
            pcp._require_max(["a"] * 5, 4, "f", 1, "x")   # too many

    def test_fixture_row_empty_record(self):
        rec = pcp.Record(3, empty=True)
        rec.name = "PADDING"
        rec.bytes = [0] * 22
        # Identity is a typed field even for padding records; the empty packed
        # record renders as {}.
        self.assertEqual(
            pcp._fixture_row(rec),
            "    { .id =  3,  // $03 PADDING  (empty)\n"
            "      .record = {} },\n")

    def test_inc_row_open_forms(self):
        rec = pcp.Record(0x1D, empty=True)
        rec.name = "TERRA_INTRO"
        self.assertEqual(pcp._inc_row_open(rec),
                         "    CharacterBaseStatsEntry{  // [$1D]  (empty)\n")
        rec2 = pcp.Record(0, empty=False)
        rec2.name = "TERRA"
        self.assertEqual(pcp._inc_row_open(rec2),
                         "    CharacterBaseStatsEntry{  // [$00]\n")


# --- Layer 2: synthetic DSL --------------------------------------------------

class DslTests(unittest.TestCase):

    def test_overridden_record_bytes(self):
        recs = parse_mini()
        alpha = recs[0]
        self.assertEqual(alpha.name, "ALPHA")
        self.assertFalse(alpha.empty)
        # hp, mp / FIGHT, MORPH, NONE, NONE / stats 1..9 /
        # DIRK, BUCKLER, EMPTY, EMPTY / BUCKLER, EMPTY / LOW|HIGH|FIXED
        self.assertEqual(alpha.bytes, [
            10, 5,
            0x00, 0x03, 0xFF, 0xFF,
            1, 2, 3, 4, 5, 6, 7, 8, 9,
            0x00, 0x5A, 0xFF, 0xFF,
            0x5A, 0xFF,
            0x02 | 0x04 | 0x10,
        ])

    def test_default_record_bytes(self):
        recs = parse_mini()
        beta = recs[1]
        # make_char_prop defaults: zero hp/mp/stats, NONE cmds, EMPTY equip,
        # NORMAL run factor (0x01) | NORMAL level mod (0x00), no fixed-equip.
        self.assertEqual(beta.bytes, [
            0, 0,
            0xFF, 0xFF, 0xFF, 0xFF,
            0, 0, 0, 0, 0, 0, 0, 0, 0,
            0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF,
            0x01,
        ])

    def test_empty_record(self):
        recs = parse_mini()
        gamma = recs[2]
        self.assertTrue(gamma.empty)
        self.assertEqual(gamma.name, "GAMMA")
        self.assertEqual(gamma.bytes, [0] * 22)

    def test_all_records_are_22_bytes(self):
        for rec in parse_mini():
            self.assertEqual(len(rec.bytes), 22)

    # --- error paths ---

    def test_record_count_mismatch_raises(self):
        # 2 records against a 3-slot CHAR_PROP index space.
        asm = "CharProp:\n make_char_prop\n end_char_prop\n empty_char_prop 1\n"
        with self.assertRaises(ParseError) as ctx:
            parse_mini(asm)
        self.assertIn("index space", str(ctx.exception))

    def test_unknown_item_symbol_raises(self):
        asm = ("CharProp:\n make_char_prop\n set_char_prop_equip SWORD\n"
               " end_char_prop\n empty_char_prop 2\n")
        with self.assertRaises(ParseError) as ctx:
            parse_mini(asm)
        self.assertIn("ITEM::SWORD", str(ctx.exception))

    def test_set_outside_record_raises(self):
        asm = "CharProp:\n set_char_prop_hp_mp 1, 2\n"
        with self.assertRaises(ParseError) as ctx:
            parse_mini(asm)
        self.assertIn("outside", str(ctx.exception))

    def test_double_make_raises(self):
        asm = "CharProp:\n make_char_prop\n make_char_prop\n"
        with self.assertRaises(ParseError):
            parse_mini(asm)

    def test_unterminated_record_raises(self):
        asm = "CharProp:\n make_char_prop\n set_char_prop_hp_mp 1, 2\n"
        with self.assertRaises(ParseError) as ctx:
            parse_mini(asm)
        self.assertIn("unterminated", str(ctx.exception))

    def test_unknown_macro_raises(self):
        asm = "CharProp:\n make_char_prop\n set_char_prop_hats 1\n"
        with self.assertRaises(ParseError) as ctx:
            parse_mini(asm)
        self.assertIn("unknown char_prop macro", str(ctx.exception))

    def test_wrong_stats_arity_raises(self):
        asm = ("CharProp:\n make_char_prop\n"
               " set_char_prop_stats 1, 2, 3, 4, 5, 6, 7, 8\n end_char_prop\n")
        with self.assertRaises(ParseError):
            parse_mini(asm)

    def test_non_literal_empty_count_raises(self):
        asm = "CharProp:\n empty_char_prop MANY\n"
        with self.assertRaises(ParseError):
            parse_mini(asm)

    def test_unexpected_directive_raises(self):
        asm = "CharProp:\n .byte 1, 2\n"
        with self.assertRaises(ParseError) as ctx:
            parse_mini(asm)
        self.assertIn("escalate", str(ctx.exception))

    def test_macro_definitions_skipped(self):
        asm = (".mac empty_char_prop count\n .res 22 * count, 0\n.endmac\n"
               "CharProp:\n make_char_prop\n end_char_prop\n"
               " make_char_prop\n end_char_prop\n empty_char_prop 1\n")
        recs = parse_mini(asm)
        self.assertEqual(len(recs), 3)


# --- Layer 2b: renderers -----------------------------------------------------

class RendererTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.records = parse_mini()

    def test_inc_header_and_rows(self):
        text = pcp._render_inc(self.records)
        self.assertIn("AUTO-GENERATED by tools/asm_parser/parse_char_prop.py", text)
        self.assertIn("pinned at 1ea47b5", text)
        self.assertIn("DO NOT EDIT BY HAND", text)
        # Identity is a typed field: the CharacterPropId enumerator, one per row.
        self.assertIn(".id = CharacterPropId::ALPHA", text)
        self.assertIn(".id = CharacterPropId::GAMMA", text)
        self.assertIn("CharacterBaseStatsEntry{  // [$00]\n", text)
        self.assertIn("CharacterBaseStatsEntry{  // [$02]  (empty)\n", text)
        self.assertIn(".record = CharacterBaseStats{},", text)
        self.assertIn(".hp = 10", text)
        self.assertIn("BattleCommandId::MORPH", text)
        self.assertIn("RunFactor::LOW", text)
        self.assertIn("{ RunFactor::LOW, LevelMod::HIGH, true }", text)

    def test_fixture_shape(self):
        text = pcp._render_fixture(self.records)
        self.assertIn("namespace ostinato::test", text)
        self.assertIn("struct ExpectedCharacterRecord", text)
        self.assertIn("static_assert(sizeof(ExpectedCharacterRecord) == 22", text)
        self.assertIn("struct ExpectedCharacterEntry", text)
        # Identity is a typed field (decimal id) on every fixture entry.
        self.assertIn(".id =  0,  // $00 ALPHA", text)
        self.assertIn(".id =  2,  // $02 GAMMA  (empty)", text)
        self.assertIn(".traits = 0x16", text)          # LOW|HIGH|FIXED
        self.assertIn(
            "inline constexpr std::array<ExpectedCharacterEntry, 3>", text)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "field", "char_prop.asm")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src char_prop.asm not present (rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        root = _find_source_root()
        symbols = pcp._Symbols(os.path.join(root, "include", "const.inc"))
        cls.records = pcp.parse_char_prop(
            os.path.join(root, "src", "field", "char_prop.asm"), symbols)

    def test_corpus_shape(self):
        self.assertEqual(len(self.records), 64)
        populated = [r for r in self.records if not r.empty]
        self.assertEqual(len(populated), 40)
        for rec in self.records:
            self.assertEqual(len(rec.bytes), 22)

    def test_record_names(self):
        self.assertEqual(self.records[0x00].name, "TERRA")
        self.assertEqual(self.records[0x21].name, "VICKS")
        self.assertEqual(self.records[0x3F].name, "HO")

    def test_terra_boundary_bytes(self):
        terra = self.records[0]
        self.assertEqual(terra.bytes[0], 40)    # hp
        self.assertEqual(terra.bytes[1], 16)    # mp
        self.assertEqual(terra.bytes[2:6], [0x00, 0x03, 0x02, 0x01])  # cmds
        self.assertEqual(terra.bytes[21], 0x01)  # NORMAL run | NORMAL level

    def test_banon_packed_traits(self):
        banon = self.records[0x0E]
        # VERY_LOW ($03) | LOW ($0c) | FIXED_EQUIP ($10)
        self.assertEqual(banon.bytes[21], 0x1F)

    def test_padding_slots_are_zero_filled(self):
        for index in [29] + list(range(34, 41)) + list(range(48, 64)):
            rec = self.records[index]
            self.assertTrue(rec.empty, "record {} should be empty".format(index))
            self.assertEqual(rec.bytes, [0] * 22)


if __name__ == "__main__":
    unittest.main()
