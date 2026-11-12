#!/usr/bin/env python3
"""Unit tests for parse_ai_scripts.py.

Three layers, matching the parser test discipline: the helpers (argument
splitting, the include file's enums and macro names, a C++ enum header, the
array names), synthetic fragments exercising the macro replay, the script file
walk, the reading of typed steps out of bytes and every error path, and an
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
import parse_ai_scripts as pas  # noqa: E402
from common import ParseError  # noqa: E402


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _source_root():
    candidate = os.path.join(REPO_ROOT, "original-src")
    if os.path.isfile(os.path.join(candidate, "src", "battle", "ai_script.asm")):
        return candidate
    return None


class FakeSymbols(object):
    """A small symbol table with the surface parse_ai_scripts.Symbols has."""

    ENUMS = {
        "ATTACK": {"FIRE": 0, "FIRE_2": 5, "BATTLE": 0xEE, "SPECIAL": 0xEF,
                   "RIOT_BLADE": 0xF0},
        "BATTLE_CMD": {"FIGHT": 0, "STEAL": 5, "CAPTURE": 6},
        "ITEM": {"DIRK": 0, "POTION": 0xE9, "DRIED_MEAT": 0xFE},
        "STATUS_ID": {"BLIND": 0, "HASTE": 19, "REFLECT": 23},
        "ELEMENT": {"FIRE": 1, "ICE": 2, "WATER": 0x80, "NONE": 0},
        "CHAR_PROP": {"TERRA": 0, "BANON": 14, "LATE": 60},
        "MONSTER": {"GUARD": 0, "SOLDIER": 1, "MAG_ROADER_4": 2},
        "AI_TARGET": {"MONSTER_SLOT_1": 48, "SELF": 54, "RAND_CHAR": 68,
                      "NOTHING": 71},
        "MONSTER_MASK": {"MONSTER_1": 1, "MONSTER_2": 2, "MONSTER_3": 4,
                         "MONSTER_6": 0x20, "ALL": 0xFF, "SELF": 0},
        "MONSTER_ENTRY_EXIT_ANIM": {"INSTANT": 0, "SIDE": 3, "BOSS_DEATH": 12,
                                    "SCROLL_BG": 16},
        "CHANGE_BATTLE_FLAGS_1": {"RESTORE_MONSTERS": 0, "SCROLL_BG": 0x80},
        "CHANGE_BATTLE_FLAGS_2": {"RESTORE_MONSTERS": 0x8000, "SCROLL_BG": 0},
    }

    def __init__(self):
        self.selectors = [("MONSTER_SLOT_1", 48), ("SELF", 54),
                          ("RAND_CHAR", 68), ("NOTHING", 71)]
        self.macro_names = sorted(pas.Assembler.replayed())
        self.formations = {7: "LEAFER_X2", 424: "ULTROS"}
        self.entrances = {0: "PRE_DRAWN", 3: "SLIDE", 12: "BOSS_DEATH",
                          16: "UNUSED_16"}
        self.animations = {0: "FlashRed", 3: "MoveBackQuickly",
                           5: "PartyRunsLeftToRight", 6: "PartyRunsRightToLeft",
                           9: "SoundEffect"}
        self.visibility = {0: "ReviveAtFullHp", 1: "Hide", 4: "HideAndWait"}

    def has(self, enum, member):
        return member in self.ENUMS[enum]

    def value(self, enum, member, path, lineno):
        if member not in self.ENUMS[enum]:
            raise ParseError(path, lineno, "unknown {}::{}".format(enum, member))
        return self.ENUMS[enum][member]

    def name(self, enum, value, what):
        for member, held in self.ENUMS[enum].items():
            if held == value:
                return member
        raise ParseError("ai scripts", 0,
                         "{}: {} has no {} member".format(what, value, enum))


def _assemble(name, *args):
    return pas.Assembler(FakeSymbols()).assemble(name, list(args), "t.asm", 1)


def _step(*raw):
    return pas.Decoder(FakeSymbols()).step(list(raw))


class _Scratch(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def write(self, name, text):
        path = os.path.join(self.dir, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(textwrap.dedent(text))
        return path


# --- helpers ---------------------------------------------------------------------

class ArgumentTest(unittest.TestCase):

    def test_plain_arguments_split_on_commas(self):
        self.assertEqual(pas.split_args("A, B,C"), ["A", "B", "C"])

    def test_a_brace_list_is_one_argument(self):
        self.assertEqual(pas.split_args("{M_1, M_2}, SIDE"),
                         ["{M_1, M_2}", "SIDE"])

    def test_no_arguments(self):
        self.assertEqual(pas.split_args(""), [])

    def test_an_unbalanced_brace_is_an_error(self):
        with self.assertRaises(ValueError):
            pas.split_args("{A, B")
        with self.assertRaises(ValueError):
            pas.split_args("A}, B")


class IncludeFileTest(_Scratch):

    def test_enum_members_count_up_and_take_values(self):
        path = self.write("a.inc", """\
            .enum AI_TARGET
                    FIRST = 48
                    SECOND
                    THIRD
            .endenum
            .enum MONSTER_MASK
                    MONSTER_1 = BIT_0
                    MONSTER_6 = BIT_5
                    ALL = $ff
                    SELF = 0
            .endenum
            .enum BARE
                    A
                    B
            .endenum
            """)
        enums = pas.read_inc_enums(path)
        self.assertEqual(enums["AI_TARGET"],
                         [("FIRST", 48), ("SECOND", 49), ("THIRD", 50)])
        self.assertEqual(enums["MONSTER_MASK"],
                         [("MONSTER_1", 1), ("MONSTER_6", 32), ("ALL", 255),
                          ("SELF", 0)])
        self.assertEqual(enums["BARE"], [("A", 0), ("B", 1)])

    def test_a_member_that_is_not_a_name_is_an_error(self):
        path = self.write("a.inc", """\
            .enum X
                    .byte 1
            .endenum
            """)
        with self.assertRaises(ParseError):
            pas.read_inc_enums(path)

    def test_an_enum_defined_twice_is_an_error(self):
        path = self.write("a.inc", """\
            .enum X
                    A
            .endenum
            .enum X
                    B
            .endenum
            """)
        with self.assertRaises(ParseError):
            pas.read_inc_enums(path)

    def test_macros_and_defines_are_both_named(self):
        path = self.write("a.inc", """\
            .mac attack a1, a2, a3
                    .byte $f0
            .endmac
            .define use_item _op_item 0,
            .define set_mode(mode) ::_m .set mode
            ; .mac commented_out
            """)
        self.assertEqual(pas.read_macro_names(path),
                         ["attack", "use_item", "set_mode"])

    def test_a_cpp_enum_is_read_by_value(self):
        path = self.write("e.h", """\
            enum class E : std::uint8_t {
                FlashRed = 0,
                MoveBack = 1,   // 8 pixels
                Far = 0x07,
            };
            """)
        self.assertEqual(pas.read_cpp_enum(path),
                         {0: "FlashRed", 1: "MoveBack", 7: "Far"})

    def test_a_header_without_enumerators_is_an_error(self):
        path = self.write("e.h", "#pragma once\n")
        with self.assertRaises(ParseError):
            pas.read_cpp_enum(path)


class NameTest(unittest.TestCase):

    def test_an_enumerator_becomes_an_array_name(self):
        self.assertEqual(pas.array_name("MAG_ROADER_4", "Turn"),
                         "kMagRoader4Turn")
        self.assertEqual(pas.array_name("NUMBER_024", "Counter"),
                         "kNumber024Counter")
        self.assertEqual(pas.array_name("LOBO", "Turn"), "kLoboTurn")

    def test_two_scripts_with_one_array_name_are_an_error(self):
        a = pas.Script(0, None)
        a.name = "MAG_ROADER"
        b = pas.Script(1, None)
        b.name = "MAG__ROADER"
        with self.assertRaises(ParseError):
            pas.check_names([a, b])


# --- the macro replay --------------------------------------------------------------

class AttackMacroTest(unittest.TestCase):

    def test_a_lone_attack_is_its_own_byte(self):
        self.assertEqual(_assemble("attack", "BATTLE"), [0xEE])

    def test_a_lone_attack_from_f0_up_is_three_equal_choices(self):
        self.assertEqual(_assemble("attack", "RIOT_BLADE"),
                         [0xF0, 0xF0, 0xF0, 0xF0])

    def test_a_lone_nothing_is_three_nothings(self):
        self.assertEqual(_assemble("attack", "NOTHING"),
                         [0xF0, 0xFE, 0xFE, 0xFE])

    def test_three_choices_with_nothing(self):
        self.assertEqual(_assemble("attack", "BATTLE", "SPECIAL", "NOTHING"),
                         [0xF0, 0xEE, 0xEF, 0xFE])

    def test_two_choices_are_an_error(self):
        with self.assertRaises(ParseError):
            _assemble("attack", "BATTLE", "SPECIAL")

    def test_an_unknown_attack_is_an_error(self):
        with self.assertRaises(ParseError):
            _assemble("attack", "NO_SUCH")

    def test_cmd_alone_is_three_equal_choices(self):
        self.assertEqual(_assemble("cmd", "STEAL"), [0xF4, 5, 5, 5])

    def test_cmd_three_with_nothing(self):
        self.assertEqual(_assemble("cmd", "STEAL", "NOTHING", "CAPTURE"),
                         [0xF4, 5, 0xFE, 6])

    def test_use_and_throw_copy_a_lone_item(self):
        self.assertEqual(_assemble("use_item", "POTION"), [0xF6, 0, 0xE9, 0xE9])
        self.assertEqual(_assemble("throw_item", "DIRK", "POTION"),
                         [0xF6, 1, 0, 0xE9])


class TargetMacroTest(unittest.TestCase):

    def test_a_selector(self):
        self.assertEqual(_assemble("set_target", "RAND_CHAR"), [0xF1, 68])

    def test_a_character_wins_over_a_selector(self):
        self.assertEqual(_assemble("set_target", "BANON"), [0xF1, 14])

    def test_a_character_the_game_would_read_as_a_selector_is_an_error(self):
        with self.assertRaises(ParseError):
            _assemble("set_target", "LATE")

    def test_an_unknown_target_is_an_error(self):
        with self.assertRaises(ParseError):
            _assemble("set_target", "WHO")


class ActionMacroTest(unittest.TestCase):

    def test_change_battle_without_flags(self):
        self.assertEqual(_assemble("change_battle", "424", "INSTANT"),
                         [0xF2, 0, 0xA8, 0x01])

    def test_change_battle_puts_each_flag_in_its_own_operand(self):
        self.assertEqual(
            _assemble("change_battle", "424", "INSTANT", "RESTORE_MONSTERS"),
            [0xF2, 0, 0xA8, 0x81])
        self.assertEqual(
            _assemble("change_battle", "512", "SCROLL_BG",
                      "{RESTORE_MONSTERS, SCROLL_BG}"),
            [0xF2, 0x90, 0x00, 0x82])

    def test_dlg_is_a_word(self):
        self.assertEqual(_assemble("dlg", "$23"), [0xF3, 0x23, 0x00])
        self.assertEqual(_assemble("dlg", "300"), [0xF3, 0x2C, 0x01])

    def test_battle_event(self):
        self.assertEqual(_assemble("battle_event", "$0d"), [0xF7, 13])

    def test_monster_entry_and_exit_defines(self):
        self.assertEqual(_assemble("restore_monsters", "{MONSTER_2, MONSTER_3}", "SIDE"),
                         [0xF5, 3, 0, 6])
        self.assertEqual(_assemble("kill_monsters", "SELF", "INSTANT"),
                         [0xF5, 0, 1, 0])
        self.assertEqual(_assemble("show_monsters", "MONSTER_1", "SIDE"),
                         [0xF5, 3, 2, 1])
        self.assertEqual(_assemble("hide_monsters", "MONSTER_1", "SIDE"),
                         [0xF5, 3, 3, 1])
        self.assertEqual(_assemble("kill_monsters_wait", "MONSTER_1", "SIDE"),
                         [0xF5, 3, 4, 1])
        self.assertEqual(_assemble("kill_monsters_debug", "MONSTER_1", "SIDE"),
                         [0xF5, 3, 5, 1])

    def test_boss_death_is_every_monster_killed_as_a_boss(self):
        self.assertEqual(_assemble("boss_death"), [0xF5, 12, 1, 0xFF])

    def test_variable_defines_put_the_operation_in_the_amount(self):
        self.assertEqual(_assemble("set_battle_var", "3", "0"), [0xF8, 3, 0x00])
        self.assertEqual(_assemble("add_battle_var", "3", "1"), [0xF8, 3, 0x81])
        self.assertEqual(_assemble("sub_battle_var", "1", "1"), [0xF8, 1, 0xC1])
        self.assertEqual(_assemble("set_monster_var", "0"), [0xF8, 0x24, 0x00])
        self.assertEqual(_assemble("add_monster_var", "1"), [0xF8, 0x24, 0x81])
        self.assertEqual(_assemble("sub_monster_var", "1"), [0xF8, 0x24, 0xC1])

    def test_switch_defines(self):
        self.assertEqual(_assemble("toggle_battle_switch", "0", "2"), [0xF9, 0, 0, 2])
        self.assertEqual(_assemble("set_battle_switch", "13", "1"), [0xF9, 1, 13, 1])
        self.assertEqual(_assemble("clr_battle_switch", "0", "5"), [0xF9, 2, 0, 5])
        self.assertEqual(_assemble("toggle_monster_switch", "3"), [0xF9, 0, 36, 3])
        self.assertEqual(_assemble("set_monster_switch", "7"), [0xF9, 1, 36, 7])
        self.assertEqual(_assemble("clr_monster_switch", "0"), [0xF9, 2, 36, 0])

    def test_monster_animations_hold_a_mask(self):
        self.assertEqual(_assemble("flash_red", "MONSTER_1"), [0xFA, 0, 1, 0])
        self.assertEqual(_assemble("move_back_fast", "MONSTER_3"), [0xFA, 3, 4, 0])
        self.assertEqual(_assemble("move_forward_64", "MONSTER_2"), [0xFA, 8, 2, 0])
        self.assertEqual(_assemble("kefka_death", "MONSTER_1"), [0xFA, 13, 1, 0])

    def test_party_animations_hold_nothing(self):
        self.assertEqual(_assemble("chars_left_to_right"), [0xFA, 5, 0, 0])
        self.assertEqual(_assemble("chars_right_to_left"), [0xFA, 6, 0, 0])

    def test_sfx_holds_its_pan_before_its_number(self):
        self.assertEqual(_assemble("sfx", "48", "241"), [0xFA, 9, 241, 48])

    def test_effects(self):
        self.assertEqual(_assemble("reset_monster_timer"), [0xFB, 0, 0])
        self.assertEqual(_assemble("invincible_on", "SELF"), [0xFB, 1, 54])
        self.assertEqual(_assemble("end_battle"), [0xFB, 2, 0])
        self.assertEqual(_assemble("recruit_gau"), [0xFB, 3, 0])
        self.assertEqual(_assemble("reset_battle_timer"), [0xFB, 4, 0])
        self.assertEqual(_assemble("invincible_off", "SELF"), [0xFB, 5, 54])
        self.assertEqual(_assemble("target_on", "MONSTER_SLOT_1"), [0xFB, 6, 48])
        self.assertEqual(_assemble("target_off", "SELF"), [0xFB, 7, 54])
        self.assertEqual(_assemble("fill_atb", "SELF"), [0xFB, 8, 54])
        self.assertEqual(_assemble("end_veldt"), [0xFB, 9, 0])
        self.assertEqual(_assemble("set_status", "HASTE"), [0xFB, 11, 19])
        self.assertEqual(_assemble("clr_status", "REFLECT"), [0xFB, 12, 23])
        self.assertEqual(_assemble("hide_piranha"), [0xFB, 13, 0])


class ConditionMacroTest(unittest.TestCase):

    def test_conditions_without_operands(self):
        for name, number in (("if_never", 0x00), ("if_hit", 0x05),
                             ("if_one_monster_type", 0x10),
                             ("if_self_alive", 0x11), ("if_self_dead", 0x12),
                             ("if_gau_present", 0x18), ("if_always", 0x1C)):
            self.assertEqual(_assemble(name), [0xFC, number, 0, 0], name)

    def test_a_lone_command_attack_or_item_is_written_twice(self):
        self.assertEqual(_assemble("if_cmd", "FIGHT"), [0xFC, 1, 0, 0])
        self.assertEqual(_assemble("if_cmd", "FIGHT", "CAPTURE"), [0xFC, 1, 0, 6])
        self.assertEqual(_assemble("if_attack", "FIRE_2"), [0xFC, 2, 5, 5])
        self.assertEqual(_assemble("if_item", "DRIED_MEAT"), [0xFC, 3, 0xFE, 0xFE])

    def test_element_conditions_are_masks(self):
        self.assertEqual(_assemble("if_element", "FIRE"), [0xFC, 4, 1, 0])
        self.assertEqual(_assemble("if_element", "{FIRE, WATER}"), [0xFC, 4, 0x81, 0])
        self.assertEqual(_assemble("if_weak_element", "SELF", "ICE"),
                         [0xFC, 0x1A, 54, 2])

    def test_hp_is_held_in_steps_of_128(self):
        self.assertEqual(_assemble("if_hp", "SELF", "10240"), [0xFC, 6, 54, 80])

    def test_an_hp_that_is_not_a_multiple_of_128_is_an_error(self):
        with self.assertRaises(ParseError):
            _assemble("if_hp", "SELF", "1000")

    def test_target_and_number_conditions(self):
        self.assertEqual(_assemble("if_mp", "SELF", "16"), [0xFC, 7, 54, 16])
        self.assertEqual(_assemble("if_level_less", "RAND_CHAR", "9"),
                         [0xFC, 0x0E, 68, 9])
        self.assertEqual(_assemble("if_level_greater", "TERRA", "0"),
                         [0xFC, 0x0F, 0, 0])
        self.assertEqual(_assemble("if_status_set", "SELF", "REFLECT"),
                         [0xFC, 8, 54, 23])
        self.assertEqual(_assemble("if_status_clr", "SELF", "HASTE"),
                         [0xFC, 9, 54, 19])
        self.assertEqual(_assemble("if_target_valid", "BANON"), [0xFC, 0x17, 14, 0])

    def test_timer_and_variable_conditions(self):
        self.assertEqual(_assemble("if_monster_timer", "30"), [0xFC, 0x0B, 30, 0])
        self.assertEqual(_assemble("if_battle_timer", "60"), [0xFC, 0x16, 60, 0])
        self.assertEqual(_assemble("if_battle_var_less", "2", "8"), [0xFC, 0x0C, 2, 8])
        self.assertEqual(_assemble("if_battle_var_greater", "36", "2"),
                         [0xFC, 0x0D, 36, 2])

    def test_monster_and_count_conditions(self):
        self.assertEqual(_assemble("if_monsters_alive", "{MONSTER_1, MONSTER_2}"),
                         [0xFC, 0x11, 3, 0])
        self.assertEqual(_assemble("if_monsters_dead", "MONSTER_6"),
                         [0xFC, 0x12, 0x20, 0])
        self.assertEqual(_assemble("if_num_chars", "2"), [0xFC, 0x13, 0, 2])
        self.assertEqual(_assemble("if_num_monsters", "1"), [0xFC, 0x13, 1, 1])
        self.assertEqual(_assemble("if_self_in_slot", "MONSTER_3"),
                         [0xFC, 0x19, 4, 0])

    def test_switch_conditions(self):
        self.assertEqual(_assemble("if_battle_switch_set", "12", "0"),
                         [0xFC, 0x14, 12, 0])
        self.assertEqual(_assemble("if_battle_switch_clr", "13", "1"),
                         [0xFC, 0x15, 13, 1])

    def test_a_monster_switch_number_is_taken_modulo_eight(self):
        self.assertEqual(_assemble("if_monster_switch_set", "7"),
                         [0xFC, 0x14, 0x24, 7])
        self.assertEqual(_assemble("if_monster_switch_clr", "9"),
                         [0xFC, 0x15, 0x24, 1])

    def test_a_battle_id_is_a_word(self):
        self.assertEqual(_assemble("if_battle_id", "$0182"), [0xFC, 0x1B, 0x82, 0x01])

    def test_wait_and_end_if(self):
        self.assertEqual(_assemble("wait"), [0xFD])
        self.assertEqual(_assemble("end_if"), [0xFE])


class ModeTest(unittest.TestCase):

    def setUp(self):
        self.assembler = pas.Assembler(FakeSymbols())

    def _do(self, name, *args):
        return self.assembler.assemble(name, list(args), "t.asm", 1)

    def test_end_then_end_retal(self):
        self.assertEqual(self._do("end"), [0xFF])
        self.assertEqual(self._do("end_retal"), [0xFF])
        self.assertEqual(self.assembler.mode, pas.Assembler.NORMAL)

    def test_two_ends_in_a_row_are_an_error(self):
        self._do("end")
        with self.assertRaises(ParseError):
            self._do("end")

    def test_end_retal_without_end_is_an_error(self):
        with self.assertRaises(ParseError):
            self._do("end_retal")

    def test_setting_the_mode_emits_nothing_and_allows_the_next_end(self):
        self._do("end")
        self.assertEqual(self._do("set_ai_script_mode", "NORMAL"), [])
        self.assertEqual(self._do("end"), [0xFF])

    def test_an_unknown_mode_is_an_error(self):
        with self.assertRaises(ParseError):
            self._do("set_ai_script_mode", "SIDEWAYS")


class ReplayErrorTest(unittest.TestCase):

    def test_an_unknown_macro_is_an_error(self):
        with self.assertRaises(ParseError):
            _assemble("dance")

    def test_a_helper_is_not_a_script_line(self):
        for name in ("make_ai_target", "_op_item", "_op_battle_var",
                     "_op_battle_switch", "_op_monster_entry_exit",
                     "assert_ai_script_mode"):
            with self.assertRaises(ParseError, msg=name):
                _assemble(name, "X")

    def test_a_wrong_argument_count_is_an_error(self):
        for name, args in (("wait", ["X"]), ("set_target", []),
                           ("if_hp", ["SELF"]), ("sfx", ["1"]),
                           ("boss_death", ["X"]), ("add_battle_var", ["1"])):
            with self.assertRaises(ParseError, msg=name):
                _assemble(name, *args)

    def test_a_number_past_a_byte_is_an_error(self):
        with self.assertRaises(ParseError):
            _assemble("battle_event", "256")
        with self.assertRaises(ParseError):
            _assemble("if_mp", "SELF", "abc")

    def test_an_unbalanced_brace_is_reported_with_its_macro(self):
        with self.assertRaises(ParseError):
            _assemble("if_element", "{FIRE")


class CoverageTest(unittest.TestCase):

    def test_matching_sets_pass(self):
        pas.check_macro_coverage(FakeSymbols(), "a.inc")

    def test_a_macro_the_parser_does_not_replay_is_an_error(self):
        symbols = FakeSymbols()
        symbols.macro_names = symbols.macro_names + ["brand_new"]
        with self.assertRaises(ParseError) as caught:
            pas.check_macro_coverage(symbols, "a.inc")
        self.assertIn("brand_new", str(caught.exception))

    def test_a_macro_the_include_file_lost_is_an_error(self):
        symbols = FakeSymbols()
        symbols.macro_names = [n for n in symbols.macro_names if n != "wait"]
        with self.assertRaises(ParseError) as caught:
            pas.check_macro_coverage(symbols, "a.inc")
        self.assertIn("wait", str(caught.exception))


# --- the script file ----------------------------------------------------------------

_ASM_HEAD = """\
.segment "ai_script"

.scope AIScript
        ARRAY_LENGTH = {count}
        Start := AIScript
.endscope

AIScriptPtrs:
        ptr_tbl AIScript

AIScript:

.if LANG_EN
        fixed_block $40
.else
        fixed_block $50
.endif
"""

_ASM_TAIL = """\
        end_fixed_block

.delmac attack
"""


class ScriptFileTest(_Scratch):

    def _assemble(self, body, count=2, tail=_ASM_TAIL):
        path = self.write("ai_script.asm", "")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(_ASM_HEAD.format(count=count) + textwrap.dedent(body) + tail)
        return pas.assemble_scripts(path, FakeSymbols())

    _TWO = """\
        ; guard
        AIScript::_0:
                attack BATTLE
                end

                end_retal

        AIScript::_1:
                attack BATTLE, BATTLE, SPECIAL
                wait
                end

                if_hit
                        attack SPECIAL
                        end_retal
        """

    def test_two_scripts_assemble_with_their_offsets(self):
        out = self._assemble(self._TWO)
        self.assertEqual(out.count, 2)
        self.assertEqual(out.offsets, {0: 0, 1: 3})
        self.assertEqual(out.block, [0xEE, 0xFF, 0xFF,
                                     0xF0, 0xEE, 0xEE, 0xEF, 0xFD, 0xFF,
                                     0xFC, 0x05, 0, 0, 0xEF, 0xFF])
        self.assertEqual(out.block_sizes, [0x40, 0x50])

    def test_scripts_may_stand_in_any_order(self):
        body = self._TWO.replace("_0:", "_9:").replace("_1:", "_0:").replace("_9:", "_1:")
        out = self._assemble(body)
        self.assertEqual(out.offsets, {1: 0, 0: 3})

    def test_a_missing_script_is_an_error(self):
        with self.assertRaises(ParseError):
            self._assemble(self._TWO, count=3)

    def test_a_script_defined_twice_is_an_error(self):
        with self.assertRaises(ParseError):
            self._assemble(self._TWO.replace("_1:", "_0:"))

    def test_a_line_that_is_no_macro_is_an_error(self):
        with self.assertRaises(ParseError):
            self._assemble(self._TWO.replace("wait", ".byte $fd"))

    def test_a_block_never_closed_is_an_error(self):
        with self.assertRaises(ParseError):
            self._assemble(self._TWO, tail="")

    def test_a_last_script_without_its_second_end_is_an_error(self):
        body = self._TWO.replace("                        end_retal\n", "")
        with self.assertRaises(ParseError):
            self._assemble(body)

    def test_scripts_larger_than_the_block_are_an_error(self):
        body = self._TWO.replace("wait\n", "wait\n" + "                dlg 1\n" * 20)
        with self.assertRaises(ParseError):
            self._assemble(body)

    def test_a_delmac_of_an_unknown_macro_is_an_error(self):
        with self.assertRaises(ParseError):
            self._assemble(self._TWO, tail=_ASM_TAIL + ".delmac nonesuch\n")


# --- typed steps out of bytes ---------------------------------------------------------

class StepTest(unittest.TestCase):

    def test_attacks(self):
        self.assertEqual(_step(0xEE), "AiStep::attack(AttackId::BATTLE)")
        self.assertEqual(_step(0xF0, 0xEE, 0xFE, 0xEF),
                         "AiStep::attackOneOf({ AttackId::BATTLE, kNoAttack, "
                         "AttackId::SPECIAL })")
        self.assertEqual(_step(0xF0, 0xF0, 0xF0, 0xF0),
                         "AiStep::attackOneOf({ AttackId::RIOT_BLADE, "
                         "AttackId::RIOT_BLADE, AttackId::RIOT_BLADE })")

    def test_targets(self):
        self.assertEqual(_step(0xF1, 68),
                         "AiStep::setTarget(AiTargetSelector::RAND_CHAR)")
        self.assertEqual(_step(0xF1, 14),
                         "AiStep::setTarget(CharacterPropId::BANON)")

    def test_change_battle_names_only_the_flags_that_are_set(self):
        self.assertEqual(_step(0xF2, 0, 0xA8, 0x01),
                         "AiStep::changeBattle({ .formation = FormationId::ULTROS, "
                         ".exit = MonsterEntranceType::PRE_DRAWN })")
        self.assertEqual(_step(0xF2, 0x90, 0xA8, 0x81),
                         "AiStep::changeBattle({ .formation = FormationId::ULTROS, "
                         ".exit = MonsterEntranceType::UNUSED_16, "
                         ".restoreMonsters = true, .scrollBackground = true })")

    def test_dialog_commands_items_and_events(self):
        self.assertEqual(_step(0xF3, 35, 0), "AiStep::dialog(35)")
        self.assertEqual(_step(0xF4, 5, 5, 0xFE),
                         "AiStep::commandOneOf({ BattleCommandId::STEAL, "
                         "BattleCommandId::STEAL, kNoCommand })")
        self.assertEqual(_step(0xF6, 0, 0xE9, 0xE9),
                         "AiStep::useItemOneOf({ .twoInThree = ItemId::POTION, "
                         ".oneInThree = ItemId::POTION })")
        self.assertEqual(_step(0xF6, 1, 0, 0xFE),
                         "AiStep::throwItemOneOf({ .twoInThree = ItemId::DIRK, "
                         ".oneInThree = kNoItem })")
        self.assertEqual(_step(0xF7, 13), "AiStep::battleEvent(13)")

    def test_monsters_shown_or_hidden(self):
        self.assertEqual(_step(0xF5, 12, 1, 0xFF),
                         "AiStep::showOrHideMonsters({ .mode = "
                         "MonsterVisibilityMode::Hide, .monsters = AiMonsters::all(), "
                         ".type = MonsterEntranceType::BOSS_DEATH })")
        self.assertEqual(_step(0xF5, 3, 0, 0x21),
                         "AiStep::showOrHideMonsters({ .mode = "
                         "MonsterVisibilityMode::ReviveAtFullHp, .monsters = "
                         "AiMonsters::of(AiTargetSelector::MONSTER_SLOT_1, "
                         "AiTargetSelector::MONSTER_SLOT_6), .type = "
                         "MonsterEntranceType::SLIDE })")
        self.assertIn("AiMonsters::self()", _step(0xF5, 0, 1, 0))

    def test_variables(self):
        self.assertEqual(_step(0xF8, 3, 0),
                         "AiStep::setVariable({ .variable = AiVariable::battle(3), "
                         ".value = 0 })")
        self.assertEqual(_step(0xF8, 36, 0x81),
                         "AiStep::addToVariable({ .variable = AiVariable::own(), "
                         ".amount = 1 })")
        self.assertEqual(_step(0xF8, 1, 0xC4),
                         "AiStep::subtractFromVariable({ .variable = "
                         "AiVariable::battle(1), .amount = 4 })")

    def test_switches_by_where_they_live(self):
        self.assertEqual(_step(0xF9, 0, 0, 2),
                         "AiStep::toggleSwitch(AiSwitch::battleVariable(0, 2))")
        self.assertEqual(_step(0xF9, 1, 13, 1),
                         "AiStep::setSwitch(AiSwitch::battleSwitch(73))")
        self.assertEqual(_step(0xF9, 2, 36, 7),
                         "AiStep::clearSwitch(AiSwitch::own(7))")
        self.assertEqual(_step(0xF9, 1, 4, 0),
                         "AiStep::setSwitch(AiSwitch::battleSwitch(0))")
        self.assertEqual(_step(0xF9, 1, 23, 7),
                         "AiStep::setSwitch(AiSwitch::battleSwitch(159))")

    def test_animations(self):
        self.assertEqual(_step(0xFA, 3, 4, 0),
                         "AiStep::animate({ .animation = "
                         "MonsterAnimation::MoveBackQuickly, .monsters = "
                         "AiMonsters::of(AiTargetSelector::MONSTER_SLOT_3) })")
        self.assertEqual(_step(0xFA, 5, 0, 0), "AiStep::partyRunsLeftToRight()")
        self.assertEqual(_step(0xFA, 6, 0, 0), "AiStep::partyRunsRightToLeft()")
        self.assertEqual(_step(0xFA, 9, 241, 48),
                         "AiStep::soundEffect({ .sound = 48, .pan = 241 })")

    def test_effects(self):
        self.assertEqual(_step(0xFB, 0, 0), "AiStep::resetOwnTimer()")
        self.assertEqual(_step(0xFB, 1, 54),
                         "AiStep::makeInvincible(AiTargetSelector::SELF)")
        self.assertEqual(_step(0xFB, 2, 0), "AiStep::endBattle()")
        self.assertEqual(_step(0xFB, 3, 0), "AiStep::recruitGau()")
        self.assertEqual(_step(0xFB, 4, 0), "AiStep::resetBattleTimer()")
        self.assertEqual(_step(0xFB, 5, 54),
                         "AiStep::endInvincible(AiTargetSelector::SELF)")
        self.assertEqual(_step(0xFB, 6, 48),
                         "AiStep::makeTargetable(AiTargetSelector::MONSTER_SLOT_1)")
        self.assertEqual(_step(0xFB, 7, 54),
                         "AiStep::makeUntargetable(AiTargetSelector::SELF)")
        self.assertEqual(_step(0xFB, 8, 54),
                         "AiStep::fillGauge(AiTargetSelector::SELF)")
        self.assertEqual(_step(0xFB, 9, 0), "AiStep::endBattleLearningRages()")
        self.assertEqual(_step(0xFB, 11, 19), "AiStep::setStatus(StatusId::HASTE)")
        self.assertEqual(_step(0xFB, 12, 23),
                         "AiStep::clearStatus(StatusId::REFLECT)")
        self.assertEqual(_step(0xFB, 13, 0), "AiStep::hideSelf()")

    def test_wait_and_ends(self):
        self.assertEqual(_step(0xFD), "AiStep::wait()")
        self.assertEqual(_step(0xFE), "AiStep::endIf()")
        self.assertEqual(_step(0xFF), "AiStep::end()")


class ConditionStepTest(unittest.TestCase):

    def test_conditions_are_named_for_what_the_game_tests(self):
        expected = (
            ((0x00, 0, 0), "AiStep::never()"),
            ((0x0A, 0, 0), "AiStep::never()"),
            ((0x01, 0, 0), "AiStep::ifHitByCommand(BattleCommandId::FIGHT)"),
            ((0x01, 0, 6), "AiStep::ifHitByCommand(BattleCommandId::FIGHT, "
                           "BattleCommandId::CAPTURE)"),
            ((0x02, 5, 5), "AiStep::ifHitByAttack(AttackId::FIRE_2)"),
            ((0x03, 0xFE, 0xFE), "AiStep::ifHitByItem(ItemId::DRIED_MEAT)"),
            ((0x04, 0x81, 0), "AiStep::ifHitByElement(ElementSet::of("
                              "Element::FIRE, Element::WATER))"),
            ((0x05, 0, 0), "AiStep::ifDamaged()"),
            ((0x06, 54, 80), "AiStep::ifHpAtMost({ .target = "
                             "AiTargetSelector::SELF, .hp = 10240 })"),
            ((0x07, 54, 16), "AiStep::ifMpAtMost({ .target = "
                             "AiTargetSelector::SELF, .mp = 16 })"),
            ((0x08, 54, 23), "AiStep::ifStatus({ .target = "
                             "AiTargetSelector::SELF, .status = StatusId::REFLECT })"),
            ((0x09, 54, 19), "AiStep::ifNoStatus({ .target = "
                             "AiTargetSelector::SELF, .status = StatusId::HASTE })"),
            ((0x0B, 30, 0), "AiStep::ifHalfOwnTimerAtLeast(30)"),
            ((0x0C, 2, 8), "AiStep::ifVariableBelow({ .variable = "
                           "AiVariable::battle(2), .value = 8 })"),
            ((0x0D, 36, 2), "AiStep::ifVariableAtLeast({ .variable = "
                            "AiVariable::own(), .value = 2 })"),
            ((0x0E, 68, 9), "AiStep::ifLevelBelow({ .target = "
                            "AiTargetSelector::RAND_CHAR, .level = 9 })"),
            ((0x0F, 0, 0), "AiStep::ifLevelAtLeast({ .target = "
                           "CharacterPropId::TERRA, .level = 0 })"),
            ((0x10, 0, 0), "AiStep::ifOneKindOfMonster()"),
            ((0x11, 3, 0), "AiStep::ifMonstersAlive(AiMonsters::of("
                           "AiTargetSelector::MONSTER_SLOT_1, "
                           "AiTargetSelector::MONSTER_SLOT_2))"),
            ((0x11, 0, 0), "AiStep::ifMonstersAlive(AiMonsters::self())"),
            ((0x12, 0, 0), "AiStep::ifMonstersDead(AiMonsters::self())"),
            ((0x13, 0, 2), "AiStep::ifPartyAtLeast(2)"),
            ((0x13, 1, 1), "AiStep::ifMonstersAtMost(1)"),
            ((0x14, 36, 0), "AiStep::ifSwitchSet(AiSwitch::own(0))"),
            ((0x15, 13, 1), "AiStep::ifSwitchClear(AiSwitch::battleSwitch(73))"),
            ((0x16, 60, 0), "AiStep::ifHalfBattleTimerAtLeast(60)"),
            ((0x17, 14, 0), "AiStep::ifTargetValid(CharacterPropId::BANON)"),
            ((0x18, 0, 0), "AiStep::ifGauNotRecruited()"),
            ((0x19, 4, 0), "AiStep::ifInSlot(AiMonsters::of("
                           "AiTargetSelector::MONSTER_SLOT_3))"),
            ((0x1A, 54, 2), "AiStep::ifWeakTo({ .target = AiTargetSelector::SELF, "
                            ".elements = ElementSet::of(Element::ICE) })"),
            ((0x1B, 0xA8, 0x01), "AiStep::ifFormation(FormationId::ULTROS)"),
            ((0x1C, 0, 0), "AiStep::always()"),
        )
        for (number, a, b), text in expected:
            self.assertEqual(_step(0xFC, number, a, b), text, hex(number))

    def test_no_row_holds_a_hexadecimal_number(self):
        for raw in ((0xFC, 0x06, 54, 80), (0xF9, 1, 13, 1), (0xF3, 140, 0),
                    (0xFA, 9, 241, 48), (0xFC, 0x1B, 0xA8, 0x01)):
            self.assertNotIn("0x", _step(*raw))


class StepErrorTest(unittest.TestCase):
    """The values no script holds, each refused rather than given a form."""

    def _refused(self, *raw):
        with self.assertRaises(ParseError, msg=str(raw)):
            _step(*raw)

    def test_a_variable_that_is_neither_a_battle_variable_nor_the_monsters_own(self):
        self._refused(0xF8, 4, 0)
        self._refused(0xF8, 35, 0x81)
        self._refused(0xF8, 37, 0)
        self._refused(0xFC, 0x0D, 20, 1)

    def test_a_set_amount_past_63(self):
        self._refused(0xF8, 0, 0x40)

    def test_a_switch_in_a_variable_with_no_typed_form(self):
        self._refused(0xF9, 1, 24, 0)
        self._refused(0xF9, 1, 35, 0)
        self._refused(0xFC, 0x14, 37, 0)

    def test_a_switch_bit_past_7(self):
        self._refused(0xF9, 1, 0, 8)
        self._refused(0xFC, 0x15, 36, 9)

    def test_a_switch_operation_past_clear(self):
        self._refused(0xF9, 3, 0, 0)

    def test_an_effect_or_condition_the_game_has_no_handler_for(self):
        self._refused(0xFB, 14, 0)
        self._refused(0xFC, 0x1D, 0, 0)

    def test_the_effect_no_script_holds_has_no_builder(self):
        self._refused(0xFB, 10, 0)

    def test_a_target_that_is_no_character_and_no_selector(self):
        self._refused(0xF1, 78)
        self._refused(0xF1, 49)
        self._refused(0xF1, 1)

    def test_a_dialog_past_255(self):
        self._refused(0xF3, 0x2C, 0x01)

    def test_an_item_step_that_is_neither_use_nor_throw(self):
        self._refused(0xF6, 2, 0, 0)

    def test_a_count_condition_with_an_operand_past_1(self):
        self._refused(0xFC, 0x13, 2, 1)

    def test_an_element_condition_naming_no_element(self):
        self._refused(0xFC, 0x04, 0, 0)
        self._refused(0xFC, 0x1A, 54, 0)

    def test_a_monster_mask_outside_the_six_slots(self):
        self._refused(0xFC, 0x11, 0x40, 0)
        self._refused(0xFC, 0x19, 0, 0)
        self._refused(0xFA, 0, 0, 0)

    def test_an_operand_byte_the_game_never_reads_holding_something(self):
        self._refused(0xFC, 0x05, 1, 0)
        self._refused(0xFC, 0x0B, 30, 1)
        self._refused(0xFC, 0x17, 54, 1)
        self._refused(0xFB, 0, 5)
        self._refused(0xFA, 5, 1, 0)
        self._refused(0xFA, 0, 1, 1)

    def test_an_unnamed_attack_entrance_animation_or_formation(self):
        self._refused(0x77)
        self._refused(0xF5, 9, 0, 1)
        self._refused(0xF5, 0, 3, 1)
        self._refused(0xFA, 2, 1, 0)
        self._refused(0xF2, 0, 0x00, 0x00)


# --- parts and tiling ------------------------------------------------------------------

def _scripts(block, offsets):
    assembled = pas.Assembled()
    assembled.block = list(block)
    assembled.offsets = dict(offsets)
    assembled.count = len(offsets)
    return pas.read_scripts(assembled, FakeSymbols())


class PartTest(unittest.TestCase):

    def test_a_script_with_no_counter_part(self):
        scripts = _scripts([0xEE, 0xFF, 0xFF], {0: 0})
        self.assertEqual(scripts[0].turn.steps,
                         ["AiStep::attack(AttackId::BATTLE)", "AiStep::end()"])
        self.assertIsNone(scripts[0].counter)
        self.assertIsNone(scripts[0].counter_of)
        self.assertEqual(scripts[0].name, "GUARD")

    def test_a_script_with_a_counter_part(self):
        scripts = _scripts([0xEE, 0xFF, 0xFC, 0x05, 0, 0, 0xEF, 0xFF], {0: 0})
        self.assertEqual(scripts[0].counter.offset, 2)
        self.assertEqual(scripts[0].counter.steps,
                         ["AiStep::ifDamaged()", "AiStep::attack(AttackId::SPECIAL)",
                          "AiStep::end()"])

    def test_an_end_if_does_not_end_a_part(self):
        scripts = _scripts([0xFC, 0x05, 0, 0, 0xEE, 0xFE, 0xEF, 0xFF, 0xFF], {0: 0})
        self.assertEqual(len(scripts[0].turn.steps), 5)

    def test_a_counter_part_running_into_the_next_script_borrows_its_turn_part(self):
        # Script 2 has no second end: the byte after its end starts script 0.
        block = [0xEE, 0xFF,              # script 2's turn part
                 0xEF, 0xFD, 0xFF, 0xFF]  # script 0, turn part and second end
        scripts = _scripts(block + [0xEE, 0xFF, 0xFF], {2: 0, 0: 2, 1: 6})
        self.assertIsNone(scripts[2].counter)
        self.assertEqual(scripts[2].counter_of, 0)
        self.assertIsNone(scripts[0].counter_of)
        self.assertEqual(pas.counter_reference(scripts[2], scripts), "kGuardTurn")

    def test_a_gap_between_scripts_is_an_error(self):
        with self.assertRaises(ParseError):
            _scripts([0xEE, 0xFF, 0xFF, 0x00, 0xEE, 0xFF, 0xFF], {0: 0, 1: 4})

    def test_scripts_that_overlap_any_other_way_are_an_error(self):
        # Script 1 starts inside script 0's counter part.
        with self.assertRaises(ParseError):
            _scripts([0xEE, 0xFF, 0xEF, 0xEE, 0xFF, 0xFF], {0: 0, 1: 3})

    def test_a_block_that_does_not_start_with_a_script_is_an_error(self):
        with self.assertRaises(ParseError):
            _scripts([0xFF, 0xEE, 0xFF, 0xFF], {0: 1})

    def test_a_part_with_no_end_is_an_error(self):
        with self.assertRaises(ParseError):
            _scripts([0xEE, 0xEE], {0: 0})

    def test_a_step_cut_off_by_the_end_of_the_block_is_an_error(self):
        with self.assertRaises(ParseError):
            _scripts([0xFC, 0x05], {0: 0})


# --- the cartridge ---------------------------------------------------------------------

class RomAssertTest(unittest.TestCase):

    def _assembled(self):
        assembled = pas.Assembled()
        assembled.block = [0xEE, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF]
        assembled.offsets = {0: 0, 1: 3}
        assembled.count = 2
        return assembled

    def _rom(self, pointers, block):
        rom = bytearray(0x300000)
        at = common.hirom_file_offset(pas.POINTERS_SNES)
        rom[at:at + len(pointers)] = bytes(pointers)
        at = common.hirom_file_offset(pas.SCRIPTS_SNES)
        rom[at:at + len(block)] = bytes(block)
        return rom

    def test_identical_bytes_pass(self):
        pas.assert_rom(self._rom([0, 0, 3, 0], [0xEE, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF]),
                       self._assembled(), "t.asm")

    def test_a_differing_pointer_is_reported(self):
        with self.assertRaises(ParseError) as caught:
            pas.assert_rom(self._rom([0, 0, 4, 0],
                                     [0xEE, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF]),
                           self._assembled(), "t.asm")
        self.assertIn("AIScriptPtrs", str(caught.exception))

    def test_a_differing_script_byte_is_reported_with_its_place(self):
        with self.assertRaises(ParseError) as caught:
            pas.assert_rom(self._rom([0, 0, 3, 0],
                                     [0xEE, 0xFF, 0xFF, 0xEE, 0xFF, 0xFF]),
                           self._assembled(), "t.asm")
        self.assertIn("AIScript at $CF8700+3", str(caught.exception))


# --- rendering -------------------------------------------------------------------------

class RenderTest(unittest.TestCase):

    def setUp(self):
        block = [0xEE, 0xFF,
                 0xEF, 0xFD, 0xFF, 0xFF,
                 0xEE, 0xFF, 0xFC, 0x05, 0, 0, 0xEF, 0xFF]
        self.scripts = _scripts(block, {2: 0, 0: 2, 1: 6})

    def test_the_steps_file_has_one_array_per_part(self):
        text = pas.render_steps_inc(self.scripts)
        self.assertIn("constexpr AiStep kGuardTurn[] = {\n"
                      "    AiStep::attack(AttackId::SPECIAL),\n"
                      "    AiStep::wait(),\n"
                      "    AiStep::end(),\n"
                      "};\n", text)
        self.assertIn("constexpr AiStep kSoldierCounter[] = {", text)
        self.assertNotIn("kGuardCounter", text)
        self.assertNotIn("kMagRoader4Counter", text)
        self.assertIn("AUTO-GENERATED by tools/asm_parser/parse_ai_scripts.py", text)
        self.assertIn("DO NOT EDIT BY HAND", text)

    def test_the_table_names_each_scripts_parts(self):
        text = pas.render_table_inc(self.scripts)
        rows = [line.split() for line in text.splitlines()
                if line.strip().startswith("{ .monster")]
        self.assertEqual(len(rows), 3)
        self.assertIn("{ .monster = MonsterId::GUARD,", text)
        self.assertRegex(text, r"MonsterId::GUARD,\s+\.turn = kGuardTurn,\s+"
                               r"\.counter = \{\} \},")
        self.assertRegex(text, r"MonsterId::SOLDIER,\s+\.turn = kSoldierTurn,\s+"
                               r"\.counter = kSoldierCounter \},")
        self.assertRegex(text, r"MonsterId::MAG_ROADER_4,\s+\.turn = "
                               r"kMagRoader4Turn,\s+\.counter = kGuardTurn \},")

    def test_the_fixture_gives_each_parts_place_and_length(self):
        text = pas.render_fixture(self.scripts, 14)
        self.assertIn("kExpectedAiScriptBytes = 14;", text)
        self.assertIn("{ .id = 0, .turnOffset = 2, .counterOffset = std::nullopt, "
                      ".turnSteps = 3, .counterSteps = 0 },", text)
        self.assertIn("{ .id = 1, .turnOffset = 6, .counterOffset = 8, "
                      ".turnSteps = 2, .counterSteps = 3 },", text)
        self.assertIn("{ .id = 2, .turnOffset = 0, .counterOffset = 2, "
                      ".turnSteps = 2, .counterSteps = 3 },", text)

    def test_the_selector_header_keeps_the_source_values(self):
        text = pas.render_selector_h(FakeSymbols())
        self.assertIn("enum class AiTargetSelector : std::uint8_t {", text)
        self.assertRegex(text, r"SELF\s+= 0x36,")
        self.assertRegex(text, r"MONSTER_SLOT_1\s+= 0x30,")


# --- the real disassembly and cartridge ---------------------------------------------------

@unittest.skipIf(_source_root() is None, "disassembly not present")
class EndToEndTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.paths, cls.symbols, cls.assembled = pas.read_all(_source_root(), REPO_ROOT)
        cls.scripts = pas.read_scripts(cls.assembled, cls.symbols)

    def test_every_macro_of_the_include_file_is_replayed(self):
        pas.check_macro_coverage(self.symbols, self.paths["ai_inc"])
        self.assertEqual(len(self.symbols.macro_names), 98)

    def test_384_scripts_fill_14665_bytes(self):
        self.assertEqual(self.assembled.count, 384)
        self.assertEqual(len(self.assembled.block), 14665)
        self.assertEqual(self.assembled.block_sizes, [0x3950, 0x4100])
        self.assertEqual(self.assembled.offsets[0], 0)

    def test_210_scripts_answer_attacks_and_one_borrows_its_answer(self):
        own = sum(1 for s in self.scripts if s.counter is not None)
        borrowed = [s for s in self.scripts if s.counter_of is not None]
        self.assertEqual(own, 209)
        self.assertEqual([(s.number, s.counter_of) for s in borrowed], [(243, 119)])
        self.assertEqual(self.scripts[243].name, "MAG_ROADER_4")
        self.assertEqual(self.scripts[119].name, "WILD_CAT")

    def test_5492_steps(self):
        steps = sum(len(s.turn.steps) + (len(s.counter.steps) if s.counter else 0)
                    for s in self.scripts)
        self.assertEqual(steps, 5492)

    def test_lobo_is_hand_traced(self):
        lobo = self.scripts[25]
        self.assertEqual(lobo.name, "LOBO")
        self.assertEqual(lobo.turn.steps, [
            "AiStep::ifLevelAtLeast({ .target = AiTargetSelector::RAND_CHAR, "
            ".level = 7 })",
            "AiStep::attackOneOf({ AttackId::BATTLE, AttackId::SPECIAL, kNoAttack })",
            "AiStep::attackOneOf({ AttackId::SPECIAL, kNoAttack, kNoAttack })",
            "AiStep::endIf()",
            "AiStep::attack(AttackId::BATTLE)",
            "AiStep::end()",
        ])
        self.assertIsNone(lobo.counter)

    def test_gaus_script_names_the_battle_switch_it_tests(self):
        gau = self.scripts[370]
        self.assertEqual(gau.turn.steps[0],
                         "AiStep::ifSwitchClear(AiSwitch::battleSwitch(73))")
        self.assertEqual(gau.turn.steps[3], "AiStep::recruitGau()")
        self.assertEqual(gau.counter.steps[0],
                         "AiStep::ifHitByItem(ItemId::DRIED_MEAT)")

    def test_no_emitted_step_holds_a_hexadecimal_number(self):
        self.assertNotIn("0x", pas.render_steps_inc(self.scripts))

    def test_the_selector_enum_is_the_30_ai_targets_from_48(self):
        self.assertEqual(len(self.symbols.selectors), 30)
        self.assertEqual(self.symbols.selectors[0], ("MONSTER_SLOT_1", 48))
        self.assertEqual(self.symbols.selectors[-1], ("TARGETTING_TARGET", 77))

    def test_the_pointer_table_and_the_block_match_the_cartridge(self):
        rom = common.load_vanilla_rom(_source_root())
        pas.assert_rom(rom, self.assembled, self.paths["ai_asm"])

    def test_a_changed_script_byte_fails_the_cartridge_compare(self):
        rom = bytearray(common.load_vanilla_rom(_source_root()))
        rom[common.hirom_file_offset(pas.SCRIPTS_SNES) + 7] ^= 0x01
        with self.assertRaises(ParseError):
            pas.assert_rom(rom, self.assembled, self.paths["ai_asm"])

    def test_the_committed_files_are_what_the_parser_emits(self):
        emitted = (
            ("include/ostinato/ai_target_selector.h",
             pas.render_selector_h(self.symbols)),
            ("src/data/generated/ai_script_steps_data.inc",
             pas.render_steps_inc(self.scripts)),
            ("src/data/generated/ai_scripts_data.inc",
             pas.render_table_inc(self.scripts)),
            ("tests/fixtures/ai_scripts_expected.h",
             pas.render_fixture(self.scripts, len(self.assembled.block))),
        )
        for relative, text in emitted:
            with open(os.path.join(REPO_ROOT, relative), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), text, relative)


if __name__ == "__main__":
    unittest.main()
