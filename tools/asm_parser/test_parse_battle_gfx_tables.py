#!/usr/bin/env python3
"""Unit tests for parse_battle_gfx_tables.py.

Five layers, matching the parser test discipline: the table reader against
synthetic assembly (language branches, macros, RAM buffers, labels, error
paths), the pure helpers, the decompressor's assembler against synthetic
routines (every operand shape, the macros, branches, every error path), an
end-to-end pass over the real disassembly and cartridge, and the structural
checks' error paths driven by editing one line of a temporary copy of the real
source.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import common  # noqa: E402
import parse_battle_gfx_tables as pbg  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "btlgfx", "btlgfx_main.asm")):
        return candidate
    return None


class _FakeSymbols(object):
    """Stands in for const.inc and btlgfx_ram.inc."""

    def __init__(self):
        self.constants = {"GAUGE_EMPTY_CHAR": 0xF0}
        self.values = {("BATTLE_CHAR_PAL", "TERRA"): 2, ("BATTLE_CHAR_PAL", "LOCKE"): 1}

    def member_value(self, scope, member, path, lineno):
        if (scope, member) not in self.values:
            raise ParseError(path, lineno, "unknown {}::{}".format(scope, member))
        return self.values[(scope, member)]


class ReaderTest(unittest.TestCase):

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.symbols = _FakeSymbols()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def read(self, text, label="Tbl", language=pbg.EN):
        path = os.path.join(self.dir, "x.asm")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        lines = pbg.active_lines(path, language)
        macros = pbg.read_macros(lines, path)
        return pbg.read_table(lines, macros, label, self.symbols, path)

    def numbers(self, text, **kwargs):
        return [item.number() for item in self.read(text, **kwargs)]

    def test_bytes_and_words(self):
        text = "Tbl:\n@1234:  .byte $01,2\n        .word $1234\nNext:\n        .byte 9\n"
        items = self.read(text)
        self.assertEqual([i.number() for i in items], [1, 2, 0x1234])
        self.assertEqual(pbg.table_bytes(items), [1, 2, 0x34, 0x12])

    def test_the_language_branch_is_taken(self):
        text = ("Tbl:\n.if LANG_EN\n        .byte 1\n.else\n        .byte 2\n.endif\n"
                "        .byte 3\n")
        self.assertEqual(self.numbers(text, language=pbg.EN), [1, 3])
        self.assertEqual(self.numbers(text, language=pbg.JP), [2, 3])

    def test_the_negated_language_branch(self):
        text = ".if !LANG_EN\nTbl:\n        .byte 7\n.endif\n.if LANG_EN\nTbl:\n        .byte 8\n.endif\n"
        self.assertEqual(self.numbers(text, language=pbg.EN), [8])
        self.assertEqual(self.numbers(text, language=pbg.JP), [7])

    def test_an_undecided_condition_inside_a_table_is_an_error(self):
        text = "Tbl:\n        .byte 1\n.if DEBUG\n        .byte 2\n.endif\n"
        with self.assertRaises(ParseError):
            self.read(text)

    def test_an_undecided_condition_outside_the_table_is_fine(self):
        text = ".if DEBUG\n        .byte 2\n.endif\nTbl:\n        .byte 1\n"
        self.assertEqual(self.numbers(text), [1])

    def test_an_endif_without_an_if_is_an_error(self):
        with self.assertRaises(ParseError):
            self.read("Tbl:\n        .byte 1\n.endif\n")

    def test_an_unclosed_if_is_an_error(self):
        with self.assertRaises(ParseError):
            self.read(".if LANG_EN\nTbl:\n        .byte 1\n")

    def test_a_macro_expands_with_its_arguments(self):
        text = (".mac gauge t1, t2\n        .byte GAUGE_EMPTY_CHAR + t1\n"
                "        .byte GAUGE_EMPTY_CHAR + t2\n.endmac\n"
                "Tbl:\n@68ac:  gauge 8, 1\n        gauge 0, 0\n")
        self.assertEqual(self.numbers(text), [0xF8, 0xF1, 0xF0, 0xF0])

    def test_a_macro_given_the_wrong_number_of_arguments_is_an_error(self):
        text = ".macro two a, b\n        .byte a, b\n.endmac\nTbl:\n        two 1\n"
        with self.assertRaises(ParseError):
            self.read(text)

    def test_a_word_naming_a_ram_buffer_keeps_its_buffer(self):
        items = self.read("Tbl:\n        .word w7e8d13 + 26, w7ea97f\n")
        self.assertIsInstance(items[0].value, pbg.RamRef)
        self.assertEqual(items[0].value.buffer, "w7e8d13")
        self.assertEqual(items[0].value.offset, 26)
        self.assertEqual(items[0].number(), 0x8D2D)
        self.assertEqual(items[1].number(), 0xA97F)

    def test_a_ram_buffer_in_a_byte_is_an_error(self):
        with self.assertRaises(ParseError):
            self.read("Tbl:\n        .byte w7e8d13\n")

    def test_two_ram_buffers_in_one_term_is_an_error(self):
        with self.assertRaises(ParseError):
            self.read("Tbl:\n        .word w7e8d13 + w7ea97f\n")

    def test_a_scoped_member_resolves(self):
        items = self.read("Tbl:\n        .byte BATTLE_CHAR_PAL::TERRA\n")
        self.assertEqual(items[0].number(), 2)
        self.assertEqual(items[0].text, "BATTLE_CHAR_PAL::TERRA")

    def test_a_second_label_before_data_names_the_same_table(self):
        text = "Tbl:\nalias_name:\n@e49a:  .byte $0b\n"
        self.assertEqual(self.numbers(text), [0x0B])

    def test_a_label_after_data_ends_the_table(self):
        text = "Tbl:\n        .byte 1\nOther:\n        .byte 2\n"
        self.assertEqual(self.numbers(text), [1])

    def test_an_unknown_term_is_an_error(self):
        with self.assertRaises(ParseError):
            self.read("Tbl:\n        .byte NOT_DEFINED\n")

    def test_a_value_wider_than_a_byte_is_an_error(self):
        with self.assertRaises(ParseError):
            self.read("Tbl:\n        .byte $100\n")

    def test_a_code_line_inside_a_table_is_an_error(self):
        with self.assertRaises(ParseError):
            self.read("Tbl:\n        .byte 1\n        lda #$00\n")

    def test_a_missing_table_is_an_error(self):
        with self.assertRaises(ParseError):
            self.read("Other:\n        .byte 1\n")


class HelperTest(unittest.TestCase):

    def test_window_tiles_count_from_the_battle_base(self):
        self.assertEqual(pbg.window_tile_name(0x20), "WALLPAPER_R0_C0")
        self.assertEqual(pbg.window_tile_name(0x23), "WALLPAPER_R0_C3")
        self.assertEqual(pbg.window_tile_name(0x30), "CORNER_TOP_LEFT")
        self.assertEqual(pbg.window_tile_name(0x3B), "CORNER_BOTTOM_RIGHT")
        with self.assertRaises(ValueError):
            pbg.window_tile_name(0x1F)

    def test_a_ram_offset_is_a_cell(self):
        self.assertEqual(pbg._cell(pbg.RamRef("w7ea97f", 66), "x", "w"), ("BATTLEFIELD", 1, 1))
        self.assertEqual(pbg._cell(pbg.RamRef("w7e8d13", 26), "x", "w"), ("MENU_WINDOWS", 13, 0))
        with self.assertRaises(ParseError):
            pbg._cell(pbg.RamRef("w7e8d13", 3), "x", "w")
        with self.assertRaises(ParseError):
            pbg._cell(pbg.RamRef("w7e1234", 0), "x", "w")

    def test_pose_frames_render_by_meaning(self):
        self.assertEqual(pbg._pose_frame(0x11), "PoseFrame::frame(17)")
        self.assertEqual(pbg._pose_frame(0x4A), "PoseFrame::mirrored(26)")
        self.assertEqual(pbg._pose_frame(0xFF), "PoseFrame::none()")

    def test_name_kinds_render_by_meaning(self):
        self.assertEqual(pbg._name_kind_text(0x0F),
                         "NameKindText::splicedBy(BattleTextCommand::ATTACK_NAME)")
        self.assertEqual(pbg._name_kind_text(0x80), "NameKindText::esperAttackName()")
        self.assertEqual(pbg._name_kind_text(0x81), "NameKindText::bushidoName()")
        self.assertEqual(pbg._name_kind_text(0x00), "NameKindText::nothing()")

    def test_signed_values(self):
        self.assertEqual(pbg._signed(0xFFF4, 16), -12)
        self.assertEqual(pbg._signed(0x000C, 16), 12)
        self.assertEqual(pbg._signed(0xD8, 8), -40)


class AssemblerTest(unittest.TestCase):
    """The decompressor's one-routine assembler against synthetic routines."""

    PORTS = {"hWMDATA": 0x2180, "hWMADDL": 0x2181, "hWMADDH": 0x2183}

    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def assemble(self, body, ports=None):
        path = os.path.join(self.dir, "x.asm")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("Other:\n        rtl\nDecompress_ext:\n" + body)
        lines = pbg.active_lines(path, pbg.EN)
        return pbg.assemble_decompressor(lines, self.PORTS if ports is None else ports, path)

    def test_the_shortest_routine(self):
        self.assertEqual(self.assemble("@ff6d:  phb\n        rtl\n"), (bytes([0x8B, 0x6B]), 0xFF6D))

    def test_every_operand_shape_encodes(self):
        body = ("@0100:  ldx     #$f800\n        rep     #$20\n        lda     #$7f\n"
                "        lda     $f8\n        lda     [$f3]\n        lda     [$f3],y\n"
                "        sta     f:hWMDATA\n        sta     a:$0000,x\n        lda     $0000,y\n"
                "        rtl\n")
        routine, _ = self.assemble(body.replace("rep     #$20", "sep     #$20"))
        self.assertEqual(list(routine), [0xA2, 0x00, 0xF8, 0xE2, 0x20, 0xA9, 0x7F, 0xA5, 0xF8,
                                         0xA7, 0xF3, 0xB7, 0xF3, 0x8F, 0x80, 0x21, 0x00,
                                         0x9D, 0x00, 0x00, 0xB9, 0x00, 0x00, 0x6B])

    def test_the_macros_expand_to_their_instructions(self):
        routine, _ = self.assemble("@0100:  longa\n        shorta\n        clr_a\n        lsr3\n"
                                   "        rtl\n")
        self.assertEqual(list(routine), [0xC2, 0x20, 0xE2, 0x20, 0x7B, 0x4A, 0x4A, 0x4A, 0x6B])

    def test_branches_resolve_backward_and_forward(self):
        routine, _ = self.assemble("@0100:  inx\n        bne     @0100\n        bra     @0106\n"
                                   "        iny\n@0106:  rtl\n")
        self.assertEqual(list(routine), [0xE8, 0xD0, 0xFD, 0x80, 0x01, 0xC8, 0x6B])

    def test_a_label_on_a_line_of_its_own(self):
        routine, _ = self.assemble("@0100:\n        inx\n@0101:\n        bne     @0101\n        rtl\n")
        self.assertEqual(list(routine), [0xE8, 0xD0, 0xFE, 0x6B])

    def test_a_wide_accumulator_immediate_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self.assemble("@0100:  longa\n        lda     #1\n        rtl\n")
        self.assertIn("16-bit accumulator immediate", str(caught.exception))

    def test_a_narrowed_accumulator_takes_immediates_again(self):
        routine, _ = self.assemble("@0100:  longa\n        shorta\n        lda     #1\n        rtl\n")
        self.assertEqual(list(routine), [0xC2, 0x20, 0xE2, 0x20, 0xA9, 0x01, 0x6B])

    def test_an_instruction_outside_the_set_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self.assemble("@0100:  nop\n        rtl\n")
        self.assertIn("not in the decompressor's instruction set", str(caught.exception))

    def test_an_operand_shape_outside_the_set_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self.assemble("@0100:  lda     ($10),y\n        rtl\n")
        self.assertIn("not a shape", str(caught.exception))

    def test_an_unknown_port_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self.assemble("@0100:  sta     f:hNOPE\n        rtl\n")
        self.assertIn("unknown port", str(caught.exception))

    def test_a_macro_given_an_operand_is_an_error(self):
        with self.assertRaises(ParseError):
            self.assemble("@0100:  longa   1\n        rtl\n")

    def test_a_branch_to_an_unknown_label_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self.assemble("@0100:  bne     @beef\n        rtl\n")
        self.assertIn("unknown label", str(caught.exception))

    def test_a_routine_with_no_local_label_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self.assemble("        phb\n        rtl\n")
        self.assertIn("no local label", str(caught.exception))

    def test_a_routine_running_into_another_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self.assemble("@0100:  phb\nNext:\n        rtl\n")
        self.assertIn("runs into another routine", str(caught.exception))

    def test_a_routine_reaching_an_undecided_condition_is_an_error(self):
        with self.assertRaises(ParseError) as caught:
            self.assemble("@0100:  phb\n.if DEBUG\n        inx\n.endif\n        rtl\n")
        self.assertIn("condition this reader does not decide", str(caught.exception))

    def test_a_missing_routine_is_an_error(self):
        path = os.path.join(self.dir, "x.asm")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("Other:\n        rtl\n")
        with self.assertRaises(ParseError) as caught:
            pbg.assemble_decompressor(pbg.active_lines(path, pbg.EN), self.PORTS, path)
        self.assertIn("not found", str(caught.exception))

    def test_the_calls_are_counted(self):
        path = os.path.join(self.dir, "x.asm")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("        jsl     Decompress_ext\n        jsr     Decompress_ext\n"
                     "        jsl     Decompress_extra\n        jsl     Decompress_ext ; again\n")
        self.assertEqual(pbg.count_decompressor_calls(pbg.active_lines(path, pbg.EN)), 2)


@unittest.skipUnless(_source_root(), "original-src not present")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.root = _source_root()
        cls.tables = {}
        for language in pbg.LANGUAGES:
            cls.paths, cls.symbols, cls.tables[language] = pbg.read_tables(cls.root, language)

    def test_every_table_reads_in_both_builds(self):
        for language in pbg.LANGUAGES:
            self.assertEqual(set(self.tables[language].raw), set(pbg.ADDRESSES))

    def test_the_english_build_matches_the_cartridge(self):
        if common.find_vanilla_rom(self.root) is None:
            self.fail("no vanilla cartridge: set FF6_VANILLA_ROM")
        pbg.assert_rom(common.load_vanilla_rom(self.root), self.tables[pbg.EN],
                       self.paths["btlgfx_asm"])

    def test_four_tables_differ_by_language(self):
        variant = sorted(label for label in pbg.ADDRESSES
                         if self.tables[pbg.EN].raw[label] != self.tables[pbg.JP].raw[label])
        self.assertEqual(variant, sorted(["GaugeTextTbl", "MenuWindowTbl", "_c23fcb", "_c2df9e"]))
        self.assertEqual(self.tables[pbg.EN].gauge_fills, self.tables[pbg.JP].gauge_fills)

    def test_hand_traced_windows(self):
        windows = pbg.windows_of(self.tables[pbg.EN].items["MenuWindowTbl"], "x")
        self.assertEqual(windows[0], ("MENU_WINDOWS", 1, 0, 12, 8))
        self.assertEqual(windows[11], ("BATTLEFIELD", 1, 1, 30, 4))
        self.assertEqual(windows[12], ("BATTLEFIELD", 7, 1, 18, 4))
        jp = pbg.windows_of(self.tables[pbg.JP].items["MenuWindowTbl"], "x")
        self.assertEqual(jp[11], ("BATTLEFIELD", 2, 1, 28, 4))

    def test_hand_traced_text_placements(self):
        placements = pbg.placements_of(self.tables[pbg.EN].items["_c2df9e"], "x")
        self.assertEqual(placements[0], ("MENU_TEXT", 2, 0, 12, 8))
        self.assertEqual(placements[2], ("MENU_TEXT", 21, 0, 4, 8))
        self.assertEqual(placements[17][3], 10)
        jp = pbg.placements_of(self.tables[pbg.JP].items["_c2df9e"], "x")
        self.assertEqual(jp[17][3], 7)

    def test_thirty_commands_are_named(self):
        named = self.tables[pbg.EN].named_commands
        self.assertEqual(len(named), 30)
        self.assertEqual(named[0], "FIGHT")
        self.assertEqual(named[0x1D], "MAGITEK")

    def test_the_character_sheets_lie_end_to_end(self):
        sheets = self.tables[pbg.EN].sheets
        self.assertEqual(len(sheets), 24)
        self.assertEqual([s.slot for s in sheets], list(range(24)))
        self.assertEqual([s.item for s in sheets], list(range(23)) + [sheets[23].item])
        self.assertEqual(sheets[0].name, "TERRA")
        self.assertEqual(sheets[0].offset, 0)
        self.assertEqual(sheets[23].name, "SOLDIER")
        self.assertEqual(sheets[23].offset, sheets[sheets[23].item].offset)
        for before, after in zip(sheets[:22], sheets[1:23]):
            self.assertEqual(before.offset + before.size, after.offset)
        gestahl = [s for s in sheets if s.name == "GESTAHL"]
        self.assertEqual(len(gestahl), 1)
        self.assertEqual(gestahl[0].size, 1760)
        self.assertEqual(len(pbg.sheet_pointer_bytes(sheets)), 72)
        self.assertEqual(self.tables[pbg.EN].raw["CharGfxPtrs"], self.tables[pbg.JP].raw["CharGfxPtrs"])

    def test_the_decompressor_assembles_from_its_source(self):
        path = self.paths["btlgfx_asm"]
        lines = pbg.active_lines(path, pbg.EN)
        routine, in_bank = pbg.assemble_decompressor(lines, pbg.read_hardware_ports(self.root), path)
        self.assertEqual(in_bank, 0xFF6D)
        self.assertEqual(len(routine), 147)
        self.assertEqual(list(routine[:5]), [0x8B, 0x0B, 0xA2, 0x00, 0x00])  # phb, phd, ldx #$0000
        self.assertEqual(routine[-1], 0x6B)                                   # rtl
        self.assertEqual(pbg.count_decompressor_calls(lines), 7)

    def test_the_decompressor_is_placed_once_in_the_cartridge(self):
        if common.find_vanilla_rom(self.root) is None:
            self.fail("no vanilla cartridge: set FF6_VANILLA_ROM")
        rom = common.load_vanilla_rom(self.root)
        placed = pbg.place_decompressor(self.root, rom)
        self.assertEqual([(p.language, p.begin, p.size, p.proved) for p in placed],
                         [("en", 0xC2FF6D, 147, 147)])
        at = common.hirom_file_offset(0xC2FF6D)
        self.assertEqual(rom[at], 0x8B)
        self.assertEqual(rom[at + 146], 0x6B)
        self.assertEqual(at + 147, common.hirom_file_offset(0xC30000))

    def test_the_decompressor_is_not_placed_without_a_cartridge(self):
        with self.assertRaises(ParseError) as caught:
            pbg.place_decompressor(self.root, None)
        self.assertIn("FF6_VANILLA_ROM", str(caught.exception))

    def test_the_gauge_fills_from_the_empty_glyph(self):
        self.assertEqual(self.tables[pbg.EN].gauge_empty, 0xF0)
        self.assertEqual(self.tables[pbg.JP].gauge_empty, 0x10)
        self.assertEqual(self.tables[pbg.EN].gauge_fills[:4], [1, 0, 0, 0])
        self.assertEqual(self.tables[pbg.EN].gauge_fills[-4:], [8, 8, 8, 8])


@unittest.skipUnless(_source_root(), "original-src not present")
class CheckTest(unittest.TestCase):
    """Each structural check, tripped by editing one line of a copy of the source."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        root = _source_root()
        for part in (("include", "const.inc"), ("src", "btlgfx", "btlgfx_ram.inc"),
                     ("src", "btlgfx", "btlgfx_main.asm")):
            os.makedirs(os.path.join(self.dir, *part[:-1]), exist_ok=True)
            shutil.copy(os.path.join(root, *part), os.path.join(self.dir, *part))
        # The sheet reader reads these too; they are never edited, so they are linked.
        for part in (("include", "hardware.inc"), ("src", "gfx", "map_sprite_gfx.asm"),
                     ("src", "gfx", "map_sprite_gfx"), ("tools", "rip_list_en.json"),
                     ("cfg", "ff6-en.cfg")):
            os.makedirs(os.path.join(self.dir, *part[:-1]), exist_ok=True)
            os.symlink(os.path.join(root, *part), os.path.join(self.dir, *part))
        self.asm = os.path.join(self.dir, "src", "btlgfx", "btlgfx_main.asm")

    def tearDown(self):
        shutil.rmtree(self.dir)

    def edit(self, old, new):
        with open(self.asm, "r", encoding="utf-8") as fh:
            text = fh.read()
        self.assertEqual(text.count(old), 1, old)
        with open(self.asm, "w", encoding="utf-8") as fh:
            fh.write(text.replace(old, new))

    def expect_error(self, fragment):
        with self.assertRaises(ParseError) as caught:
            pbg.read_tables(self.dir, pbg.EN)
        self.assertIn(fragment, str(caught.exception))

    def test_the_unedited_copy_passes(self):
        pbg.read_tables(self.dir, pbg.EN)
        pbg.read_tables(self.dir, pbg.JP)

    def test_a_frame_tile_off_a_tile_boundary(self):
        self.edit("@c745:  .word   $0ae0", "@c745:  .word   $0ae1")
        self.expect_error("neither blank nor on a tile")

    def test_a_pose_step_that_is_not_a_frame(self):
        self.edit("        .byte   $34,$35,$36,$35", "        .byte   $2a,$35,$36,$35")
        self.expect_error("is not a frame")

    def test_a_gauge_step_with_the_wrong_fill(self):
        self.edit("@68ac:  make_gauge_text 1, 0, 0, 0", "@68ac:  make_gauge_text 2, 0, 0, 0")
        self.expect_error("gauge step 0 fills 2 pixels")

    def test_a_flip_byte_that_is_not_the_flip_bit(self):
        self.edit("@e43e:  .byte   $00,$00,$00,$00", "@e43e:  .byte   $00,$00,$00,$41")
        self.expect_error("CharFlipTbl holds $41")

    def test_a_renamed_window(self):
        self.edit(";      $08: esper", ";      $08: espers")
        self.expect_error("window list no longer reads")

    def test_a_name_kind_that_names_no_name_command(self):
        self.edit("@97a1:  .byte   $0f", "@97a1:  .byte   $0d")
        self.expect_error("not a name command")

    def test_a_window_moved_off_a_cell(self):
        self.edit("def_menu_window w7e8d13 + 26, 18, 8", "def_menu_window w7e8d13 + 27, 18, 8")
        self.expect_error("is not on a cell")

    def test_a_sheet_pointer_table_of_the_wrong_length(self):
        self.edit(".repeat 23, i\n        .faraddr array_item MapSpriteGfx, i",
                  ".repeat 22, i\n        .faraddr array_item MapSpriteGfx, i")
        self.expect_error("CharGfxPtrs holds 23 pointers")

    def test_a_sheet_pointer_naming_an_unknown_sheet(self):
        self.edit(".faraddr array_item MapSpriteGfx, MAP_SPRITE_GFX::SOLDIER",
                  ".faraddr array_item MapSpriteGfx, MAP_SPRITE_GFX::NOBODY")
        self.expect_error("unknown sheet NOBODY")

    def test_a_sheet_pointer_that_is_not_a_pointer(self):
        self.edit(".faraddr array_item MapSpriteGfx, MAP_SPRITE_GFX::SOLDIER",
                  ".word $1234")
        self.expect_error("is not a CharGfxPtrs entry")

    def test_an_instruction_the_decompressors_assembler_does_not_know(self):
        self.edit("        lsr3\n        clc", "        lsr4\n        clc")
        lines = pbg.active_lines(self.asm, pbg.EN)
        with self.assertRaises(ParseError) as caught:
            pbg.assemble_decompressor(lines, pbg.read_hardware_ports(self.dir), self.asm)
        self.assertIn("not in the decompressor's instruction set", str(caught.exception))

    def test_a_decompressor_missing_its_return_runs_off_its_end(self):
        # Without the rtl the assembler reads on into what follows the routine — in the real
        # source a segment directive — and stops at the first line that is not an instruction.
        self.edit("        pld\n        plb\n        rtl\n\n; ---", "        pld\n        plb\n\n; ---")
        lines = pbg.active_lines(self.asm, pbg.EN)
        with self.assertRaises(ParseError) as caught:
            pbg.assemble_decompressor(lines, pbg.read_hardware_ports(self.dir), self.asm)
        self.assertIn("is not an instruction", str(caught.exception))

    def test_a_differing_cartridge_byte_is_reported(self):
        paths, _, tables = pbg.read_tables(self.dir, pbg.EN)
        rom = bytearray(0x300000)
        for label, snes in pbg.ADDRESSES.items():
            base = common.hirom_file_offset(snes)
            rom[base:base + len(tables.raw[label])] = bytes(tables.raw[label])
        pbg.assert_rom(bytes(rom), tables, paths["btlgfx_asm"])
        rom[common.hirom_file_offset(pbg.ADDRESSES["MsgSpeedTbl"])] ^= 1
        with self.assertRaises(ParseError) as caught:
            pbg.assert_rom(bytes(rom), tables, paths["btlgfx_asm"])
        self.assertIn("MsgSpeedTbl", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
