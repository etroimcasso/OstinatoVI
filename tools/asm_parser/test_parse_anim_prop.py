#!/usr/bin/env python3
"""Unit tests for parse_anim_prop.py.

Three layers per the parser-test discipline:
  1. pure emitters (the typed-surface renderers + the word-expression eval);
  2. synthetic inputs (every structural assert: record widths, pad byte,
     thrown-type range, frame-index range, ItemAnimPtrs grammar);
  3. end-to-end against the real original-src .dat binaries + btlgfx_main.asm
     (skipped cleanly when the rip output is absent).

Python 3 stdlib only. Run:
    python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'
"""

from __future__ import annotations

import os
import tempfile
import unittest

import parse_anim_prop as pap
from common import ParseError


def _write_dat(data):
    fd, path = tempfile.mkstemp(suffix=".dat")
    with os.fdopen(fd, "wb") as fh:
        fh.write(bytes(data))
    return path


def _write_asm(text):
    fd, path = tempfile.mkstemp(suffix=".asm")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


# --- Layer 1: pure emitters --------------------------------------------------

class EmitterTests(unittest.TestCase):

    def test_anim_ref(self):
        self.assertEqual(pap._anim_ref(0xFFFF), "AnimationRef::NONE")
        self.assertEqual(pap._anim_ref(0x0192), "AnimationRef::of(402)")
        self.assertEqual(pap._anim_ref(0x8012),
                         "AnimationRef::withHighBit(18)")

    def test_tile_offset(self):
        self.assertEqual(pap._tile_offset(0x0064),
                         "AnimationTileOffset::of(100)")
        self.assertEqual(pap._tile_offset(0x8100),
                         "AnimationTileOffset::of2bpp(256)")

    def test_init_function(self):
        self.assertEqual(pap._init_function(0x18),
                         "AnimationInitFunction::of(24)")
        self.assertEqual(pap._init_function(0x82),
                         "AnimationInitFunction::withHighBit(2)")

    def test_thrown_names_types_and_thrown_flag(self):
        self.assertEqual(
            pap._thrown(0x00),
            "ThrownAnimationFlags::of(WeaponAnimationType::UNKNOWN_0)")
        self.assertEqual(
            pap._thrown(0x81),
            "ThrownAnimationFlags::thrown(WeaponAnimationType::STAR_OR_GAMBLER)")
        self.assertEqual(
            pap._thrown(0x82),
            "ThrownAnimationFlags::thrown(WeaponAnimationType::UNKNOWN_2)")

    def test_item_throw_decodes_classes(self):
        self.assertEqual(
            pap._item_throw(0x00),
            "ItemThrowAnimation::of(JumpAnimationClass::UNARMED, "
            "ThrowAnimationClass::THICK_KNIFE)")
        self.assertEqual(
            pap._item_throw(0x6E),
            "ItemThrowAnimation::of(JumpAnimationClass::SPEAR, "
            "ThrowAnimationClass::BOOMERANG)")
        self.assertEqual(
            pap._item_throw(0x80),
            "ItemThrowAnimation::fightAnimation(JumpAnimationClass::UNARMED, "
            "ThrowAnimationClass::THICK_KNIFE)")

    def test_eval_word_expr(self):
        self.assertEqual(pap._eval_word_expr("402*14", "x", 1), 5628)
        self.assertEqual(pap._eval_word_expr("$ffff", "x", 1), 0xFFFF)
        self.assertEqual(pap._eval_word_expr("$1234", "x", 1), 0x1234)
        with self.assertRaises(ParseError):
            pap._eval_word_expr("foo*14", "x", 1)
        with self.assertRaises(ParseError):
            pap._eval_word_expr("garbage", "x", 1)


# --- Layer 2: synthetic structural guards ------------------------------------

class WeaponRecordTests(unittest.TestCase):

    def _read_one(self, rec):
        path = _write_dat(rec)
        try:
            return pap._read_weapon_records(path, 1)
        finally:
            os.remove(path)

    def test_pad7_nonzero_raises(self):
        rec = [0] * 8
        rec[7] = 1
        with self.assertRaises(ParseError) as ctx:
            self._read_one(rec)
        self.assertIn("pad byte", str(ctx.exception))

    def test_thrown_type_out_of_range_raises(self):
        rec = [0] * 8
        rec[5] = 5  # low-7 weapon-animation type 5 is outside the 0..4 corpus
        with self.assertRaises(ParseError) as ctx:
            self._read_one(rec)
        self.assertIn("weapon-animation type", str(ctx.exception))

    def test_valid_record_ok(self):
        rec = [1, 2, 3, 4, 5, 0x81, 7, 0]  # thrown = star/gambler, thrown flag
        recs = self._read_one(rec)
        self.assertEqual(len(recs), 1)

    def test_wrong_length_raises(self):
        path = _write_dat([0] * 7)
        try:
            with self.assertRaises(ParseError):
                pap._read_weapon_records(path, 1)
        finally:
            os.remove(path)


class AttackGfxTests(unittest.TestCase):

    def test_frame_index_out_of_range_raises(self):
        data = bytearray(pap.ATTACK_GFX_ROWS * pap.ATTACK_GFX_WIDTH)
        data[2] = 0x84  # record 0 frame-data index = 0x0B84 = 2948 (>= 2948)
        data[3] = 0x0B
        path = _write_dat(data)
        try:
            with self.assertRaises(ParseError) as ctx:
                pap.read_attack_gfx(path)
            self.assertIn("frame-data index", str(ctx.exception))
        finally:
            os.remove(path)

    def test_wrong_length_raises(self):
        path = _write_dat([0] * 6)
        try:
            with self.assertRaises(ParseError):
                pap.read_attack_gfx(path)
        finally:
            os.remove(path)


class ItemAnimPtrsTests(unittest.TestCase):

    def _read(self, body):
        path = _write_asm("ItemAnimPtrs:\n" + body)
        try:
            return pap.read_item_anim_ptrs(path)
        finally:
            os.remove(path)

    def test_all_none_ok(self):
        rows = self._read("        .word   $ffff\n" * 32)
        self.assertEqual(len(rows), 32)
        self.assertTrue(all(r[1] is None for r in rows))

    def test_multiplied_word_demultiplies(self):
        body = "        .word   402*14\n" + "        .word   $ffff\n" * 31
        rows = self._read(body)
        self.assertEqual(rows[0], (5628, 402))

    def test_wrong_count_raises(self):
        with self.assertRaises(ParseError):
            self._read("        .word   $ffff\n" * 31)

    def test_remainder_raises(self):
        body = "        .word   $0005\n" + "        .word   $ffff\n" * 31
        with self.assertRaises(ParseError) as ctx:
            self._read(body)
        self.assertIn("multiple of", str(ctx.exception))

    def test_row_out_of_range_raises(self):
        body = "        .word   406*14\n" + "        .word   $ffff\n" * 31
        with self.assertRaises(ParseError) as ctx:
            self._read(body)
        self.assertIn("406", str(ctx.exception))

    def test_missing_label_raises(self):
        path = _write_asm("        .word   $ffff\n")
        try:
            with self.assertRaises(ParseError) as ctx:
                pap.read_item_anim_ptrs(path)
            self.assertIn("label not found", str(ctx.exception))
        finally:
            os.remove(path)


# --- Layer 3: end-to-end against the real contract ---------------------------

def _find_source_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "original-src")
    if os.path.isfile(os.path.join(root, "src", "btlgfx",
                                   "attack_anim_prop_en.dat")):
        return root
    return None


@unittest.skipUnless(_find_source_root(),
                     "original-src btlgfx .dat files not present "
                     "(rip output absent)")
class EndToEndTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _find_source_root()
        cls.p = pap._paths(cls.root)
        cls.symbols = pap.Symbols(cls.p["const_inc"])

    def test_record_counts(self):
        self.assertEqual(len(pap.read_attack_anim(self.p["attack_anim"])), 406)
        self.assertEqual(len(pap.read_attack_gfx(self.p["attack_gfx"])), 650)
        self.assertEqual(
            len(pap._read_weapon_records(self.p["weapon"], pap.WEAPON_ROWS)), 93)
        self.assertEqual(
            len(pap._read_weapon_records(self.p["monster_attack"],
                                         pap.MONSTER_ATTACK_ROWS)), 35)
        self.assertEqual(
            len(pap.read_item_jump_throw(self.p["item_jump_throw"])), 257)

    def test_item_anim_ptrs_hand_traced(self):
        rows = pap.read_item_anim_ptrs(self.p["btlgfx_main"])
        self.assertEqual(len(rows), 32)
        self.assertEqual(rows[0][1], None)      # $e0 MARVEL_SHOES: no animation
        self.assertEqual(rows[7], (5628, 402))  # $e7 RENAME_CARD -> row 402
        self.assertEqual(rows[8][1], 337)       # $e8 TONIC -> row 337
        self.assertEqual(rows[31][1], None)     # $ff EMPTY: no animation

    def test_monster_anim_names_count(self):
        self.assertEqual(len(self.symbols.monster_anim_names), 35)
        self.assertEqual(self.symbols.monster_anim_names[0], "HIT")

    def test_weapon_ids_resolve(self):
        self.assertEqual(self.symbols.item(0), "DIRK")
        self.assertEqual(self.symbols.item(0xE7), "RENAME_CARD")


if __name__ == "__main__":
    unittest.main()
