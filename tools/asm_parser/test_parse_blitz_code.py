#!/usr/bin/env python3
"""Unit tests for parse_blitz_code.py.

Three layers, matching the parser test discipline: the helpers (scope
constants, JOY_* masks, mask decomposition, the record builder), synthetic
assembly fragments exercising the macro replay and its error paths, and an
end-to-end pass over the real disassembly and cartridge.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import textwrap
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_blitz_code as pbc  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "btlgfx",
                                   "btlgfx_main.asm")):
        return candidate
    return None


_BLITZ_INC = """\
.enum BLITZ_CODE
        NONE                            = 0
        A_BUTTON                        = 1
        LEFT                            = 2
        RIGHT                           = 3
.endenum

.scope BlitzCode
        ARRAY_LENGTH                    = 2
        ITEM_SIZE                       = 12
.endscope
"""

_HARDWARE_INC = """\
; [ joypad button masks ]

JOY_A           = %0000000010000000
JOY_LEFT        = %0000001000000000
JOY_RIGHT       = %0000000100000000
"""

_MACROS = """\
.macro blitz_code id
        .ident(.sprintf("_blitz_code_%d", id)) := *
.endmac

.macro end_blitz_code id
        .res 11 - _blitz_length, BLITZ_CODE::NONE
.endmac
"""


class _Files(object):
    """A scratch directory holding the two include files and one asm file."""

    def __init__(self, asm, blitz_inc=_BLITZ_INC, hardware_inc=_HARDWARE_INC):
        self.dir = tempfile.mkdtemp()
        self.blitz_inc = self._put("blitz_code.inc", blitz_inc)
        self.hardware_inc = self._put("hardware.inc", hardware_inc)
        self.asm = self._put("btlgfx.asm", textwrap.dedent(asm))

    def _put(self, name, text):
        path = os.path.join(self.dir, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        return path

    def symbols(self):
        return pbc.BlitzSymbols(self.blitz_inc, self.hardware_inc)

    def close(self):
        shutil.rmtree(self.dir)


def _codes(body):
    return _MACROS + ".pushseg\nBlitzCode:\n" + body + ".popseg\n"


_TWO_CODES = _codes("""\
        blitz_code 0
        .byte   BLITZ_CODE::LEFT
        .byte   BLITZ_CODE::A_BUTTON
        end_blitz_code 0
        blitz_code 1
        .byte   BLITZ_CODE::RIGHT
        .byte   BLITZ_CODE::LEFT
        .byte   BLITZ_CODE::A_BUTTON
        end_blitz_code 1
""")


class SymbolTest(unittest.TestCase):

    def setUp(self):
        self.files = _Files(_TWO_CODES)

    def tearDown(self):
        self.files.close()

    def test_scope_constants_are_read(self):
        self.assertEqual(self.files.symbols().scope,
                         {"ARRAY_LENGTH": 2, "ITEM_SIZE": 12})

    def test_joypad_masks_keep_source_order(self):
        self.assertEqual(self.files.symbols().pads,
                         [("A", 0x80), ("LEFT", 0x200), ("RIGHT", 0x100)])

    def test_a_multi_bit_joypad_mask_is_rejected(self):
        files = _Files(_TWO_CODES, hardware_inc="JOY_A = %11\n")
        try:
            with self.assertRaises(ParseError) as ctx:
                files.symbols()
            self.assertIn("single bit", str(ctx.exception))
        finally:
            files.close()

    def test_a_scope_without_item_size_is_rejected(self):
        inc = _BLITZ_INC.replace("        ITEM_SIZE                       = 12\n",
                                 "")
        files = _Files(_TWO_CODES, blitz_inc=inc)
        try:
            with self.assertRaises(ParseError) as ctx:
                files.symbols()
            self.assertIn("no ITEM_SIZE", str(ctx.exception))
        finally:
            files.close()


class MaskDecompositionTest(unittest.TestCase):
    PADS = [("A", 0x80), ("DOWN", 0x400), ("LEFT", 0x200)]

    def test_a_diagonal_splits_into_its_directions(self):
        self.assertEqual(pbc.decompose_mask(0x0600, self.PADS, "x"),
                         ["DOWN", "LEFT"])

    def test_all_bits_is_the_special_case(self):
        self.assertIsNone(pbc.decompose_mask(0xFFFF, self.PADS, "x"))

    def test_an_unnamed_bit_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            pbc.decompose_mask(0x0601, self.PADS, "x")
        self.assertIn("exact OR", str(ctx.exception))

    def test_zero_is_an_error(self):
        with self.assertRaises(ParseError):
            pbc.decompose_mask(0, self.PADS, "x")


class RecordTest(unittest.TestCase):

    def test_the_record_pads_and_doubles_the_count(self):
        blitz = pbc.Blitz(0, [2, 3, 1])
        self.assertEqual(blitz.bytes, [2, 3, 1] + [0] * 8 + [6])
        self.assertEqual(len(blitz.bytes), 12)


class MacroReplayTest(unittest.TestCase):

    def parse(self, body):
        files = _Files(_codes(body))
        try:
            return pbc.parse_blitz_codes(files.asm, files.symbols())
        finally:
            files.close()

    def test_two_codes_replay(self):
        files = _Files(_TWO_CODES)
        try:
            blitzes = pbc.parse_blitz_codes(files.asm, files.symbols())
        finally:
            files.close()
        self.assertEqual([b.sequence for b in blitzes], [[2, 1], [3, 2, 1]])
        self.assertEqual(blitzes[1].bytes[-1], 6)

    def _error(self, body, text):
        with self.assertRaises(ParseError) as ctx:
            self.parse(body)
        self.assertIn(text, str(ctx.exception))

    def test_a_missing_confirm_is_an_error(self):
        self._error("""\
        blitz_code 0
        .byte   BLITZ_CODE::LEFT
        end_blitz_code 0
""", "not A_BUTTON")

    def test_an_out_of_order_block_is_an_error(self):
        self._error("""\
        blitz_code 1
        .byte   BLITZ_CODE::A_BUTTON
        end_blitz_code 1
""", "out of order")

    def test_a_mismatched_close_is_an_error(self):
        self._error("""\
        blitz_code 0
        .byte   BLITZ_CODE::A_BUTTON
        end_blitz_code 1
""", "does not close")

    def test_a_non_enum_byte_is_an_error(self):
        self._error("""\
        blitz_code 0
        .byte   $01
        end_blitz_code 0
""", "not a BLITZ_CODE reference")

    def test_an_unknown_member_is_an_error(self):
        self._error("""\
        blitz_code 0
        .byte   BLITZ_CODE::UP
        end_blitz_code 0
""", "unknown BLITZ_CODE::UP")

    def test_none_inside_a_sequence_is_an_error(self):
        self._error("""\
        blitz_code 0
        .byte   BLITZ_CODE::NONE
        .byte   BLITZ_CODE::A_BUTTON
        end_blitz_code 0
""", "writes NONE")

    def test_a_sequence_longer_than_the_record_is_an_error(self):
        body = ("        blitz_code 0\n"
                + "        .byte   BLITZ_CODE::LEFT\n" * 11
                + "        .byte   BLITZ_CODE::A_BUTTON\n"
                + "        end_blitz_code 0\n")
        self._error(body, "the record holds 11")

    def test_a_count_other_than_the_scope_is_an_error(self):
        self._error("""\
        blitz_code 0
        .byte   BLITZ_CODE::A_BUTTON
        end_blitz_code 0
""", "ARRAY_LENGTH is 2")


class ButtonMaskTest(unittest.TestCase):

    def parse(self, words):
        asm = "BlitzButtonMaskTbl:\n@6f6a:  .word   {}\n".format(words[0])
        asm += "".join("        .word   {}\n".format(w) for w in words[1:])
        asm += "\nNextLabel:\n"
        files = _Files(asm)
        try:
            return pbc.parse_button_masks(files.asm, files.symbols())
        finally:
            files.close()

    def test_one_word_per_input(self):
        self.assertEqual(self.parse(["$ffff", "$0080", "$0200", "$0100"]),
                         [0xFFFF, 0x80, 0x200, 0x100])

    def test_a_short_table_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.parse(["$ffff", "$0080"])
        self.assertIn("BLITZ_CODE has 4 members", str(ctx.exception))

    def test_an_undecomposable_word_is_an_error(self):
        with self.assertRaises(ParseError) as ctx:
            self.parse(["$ffff", "$0080", "$0200", "$0001"])
        self.assertIn("exact OR", str(ctx.exception))


class RomAssertTest(unittest.TestCase):

    def _rom(self, code_bytes, mask_bytes):
        size = common.hirom_file_offset(pbc.BLITZ_CODE_SNES) + len(code_bytes)
        rom = bytearray(size)
        base = common.hirom_file_offset(pbc.BLITZ_CODE_SNES)
        rom[base:base + len(code_bytes)] = code_bytes
        base = common.hirom_file_offset(pbc.BUTTON_MASK_SNES)
        rom[base:base + len(mask_bytes)] = mask_bytes
        return bytes(rom)

    def setUp(self):
        self.blitzes = [pbc.Blitz(0, [2, 1])]
        self.words = [0xFFFF, 0x0080]

    def test_identical_bytes_pass(self):
        pbc.assert_rom(self._rom(bytes(self.blitzes[0].bytes),
                                 bytes([0xFF, 0xFF, 0x80, 0x00])),
                       self.blitzes, self.words, "x")

    def test_a_differing_code_byte_is_reported(self):
        code = bytearray(self.blitzes[0].bytes)
        code[11] = 5
        with self.assertRaises(ParseError) as ctx:
            pbc.assert_rom(self._rom(bytes(code), bytes([0xFF, 0xFF, 0x80, 0])),
                           self.blitzes, self.words, "x")
        self.assertIn("BlitzCode", str(ctx.exception))

    def test_a_byte_swapped_mask_is_reported(self):
        with self.assertRaises(ParseError) as ctx:
            pbc.assert_rom(self._rom(bytes(self.blitzes[0].bytes),
                                     bytes([0xFF, 0xFF, 0x00, 0x80])),
                           self.blitzes, self.words, "x")
        self.assertIn("BlitzButtonMaskTbl", str(ctx.exception))


@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        (cls.paths, cls.symbols, cls.blitzes,
         cls.words) = pbc.read_all(_source_root())

    def test_eight_blitzes_named_by_the_blitz_command(self):
        self.assertEqual([b.name for b in self.blitzes],
                         ["PUMMEL", "AURABOLT", "SUPLEX", "FIRE_DANCE",
                          "MANTRA", "AIR_BLADE", "SPIRALER", "BUM_RUSH"])

    def test_sequence_lengths_are_pinned(self):
        self.assertEqual([len(b.sequence) for b in self.blitzes],
                         [4, 4, 5, 6, 7, 8, 7, 10])

    def test_pummel_is_hand_traced(self):
        self.assertEqual(self.blitzes[0].bytes,
                         [14, 10, 14, 1, 0, 0, 0, 0, 0, 0, 0, 8])

    def test_the_masks_are_hand_traced(self):
        self.assertEqual(len(self.words), 15)
        self.assertEqual(self.words[0], 0xFFFF)
        self.assertEqual(self.words[7], 0x0600)
        self.assertEqual(self.words[14], 0x0200)

    def test_twelve_controller_inputs(self):
        self.assertEqual([n for n, _ in self.symbols.pads],
                         ["A", "X", "L", "R", "B", "Y", "SELECT", "START",
                          "UP", "DOWN", "LEFT", "RIGHT"])

    def test_both_tables_match_the_cartridge(self):
        pbc.assert_rom(common.load_vanilla_rom(_source_root()),
                       self.blitzes, self.words, self.paths["btlgfx_asm"])


if __name__ == "__main__":
    unittest.main()
