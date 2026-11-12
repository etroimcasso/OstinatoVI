#!/usr/bin/env python3
"""Emit the monster AI scripts from original-src battle/ai_script.asm.

Port-time tooling (NOT a build/CI dependency): reads the 384 scripts in
src/battle/ai_script.asm, written in the macros of include/battle/ai_script.inc,
and emits:

  * include/ostinato/ai_target_selector.h        — AI_TARGET
  * src/data/generated/ai_script_steps_data.inc  — one named array of AiStep
    builder calls per script part (a turn part for every script, a counter part
    for the scripts that have one).
  * src/data/generated/ai_scripts_data.inc       — one AiScript row per script,
    keyed by MonsterId, pointing at its parts.
  * tests/fixtures/ai_scripts_expected.h         — where the cartridge holds
    each part and how many steps it has.

How it works. Every macro and `.define` of ai_script.inc is replayed exactly as
the file defines it, giving the bytes the assembler would produce and the
pointer table over them. Both are compared with the vanilla cartridge over
their whole extent. The typed steps are then read back out of those bytes by
the rules the game's own script reader follows, so a step names what the game
does with it rather than what the source's macro is called.

Structural guarantees, hard-errored at parse/emit time:
  * the set of macros this file replays is exactly the set ai_script.inc
    defines, and every script line is one of them;
  * `end` and `end_retal` alternate as the source's own mode assertion requires;
  * the scripts are numbered 0..ARRAY_LENGTH-1, one label each, and tile the
    block in cartridge order with no gap; the one script whose counter part
    runs into the next script's bytes must end exactly where that script's turn
    part ends, and is emitted as a reference to it;
  * every operand is a value the typed steps can hold: a named attack, command,
    item, status, element, character, selector, formation, entrance, animation
    and visibility mode; a variable 0-3 or the monster's own; a switch bit 0-7;
    an amount of at most 63; an HP that is a multiple of 128; a dialog below
    256; no operand byte the game ignores holding anything but 0;
  * the pointer table and the script bytes are identical to the vanilla
    cartridge.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_ai_scripts.py --source-root PATH
    parse_ai_scripts.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_const_enums as pce
from common import ParseError, strip_comment

# --- where the cartridge holds the scripts -------------------------------------

POINTERS_SNES = 0xCF8400   # AIScriptPtrs (ai_script.asm:12-14)
SCRIPTS_SNES = 0xCF8700    # AIScript     (ai_script.asm:18-19)

# The byte the script reader takes as "no choice" among three (AIRand3).
NOTHING = 0xFE

# A target below this is a character by its CHAR_PROP value; from here up it is
# an AI_TARGET selector (CheckAITarget).
FIRST_SELECTOR = 48

# The variable index at and above which a variable is the monster's own
# (GetBattleVar), and the one index the scripts write for it.
OWN_VARIABLE = 36
# Variable indices 4-23 are the bytes of the game's battle switches.
FIRST_SWITCH_VARIABLE = 4
LAST_SWITCH_VARIABLE = 23
BATTLE_VARIABLES = 4

HP_STEP = 128
LARGEST_AMOUNT = 63

# How many bytes each command from $f0 up occupies (AICmdSizeTbl); an attack
# below $f0 is one byte.
COMMAND_SIZES = (4, 2, 4, 3, 4, 4, 4, 2, 3, 4, 4, 3, 4, 1, 1, 1)

_RE_SCRIPT_LABEL = re.compile(r"^AIScript::_(\d+):$")
_RE_ENUM_OPEN = re.compile(r"^\.enum\s+([A-Za-z_][A-Za-z0-9_]*)$")
_RE_MEMBER = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(?:\s*=\s*(\S+))?$")
_RE_MAC = re.compile(r"^\.mac(?:ro)?\s+([A-Za-z_][A-Za-z0-9_]*)")
_RE_DEFINE = re.compile(r"^\.define\s+([A-Za-z_][A-Za-z0-9_]*)(\(?)")
_RE_CPP_MEMBER = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*,")
_RE_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def _paths(source_root, repo_root):
    return {
        "const_inc": os.path.join(source_root, "include", "const.inc"),
        "ai_inc": os.path.join(source_root, "include", "battle", "ai_script.inc"),
        "ai_asm": os.path.join(source_root, "src", "battle", "ai_script.asm"),
        "formation_id_h": os.path.join(repo_root, "include", "ostinato",
                                       "formation_id.h"),
        "entrance_h": os.path.join(repo_root, "include", "ostinato",
                                   "monster_entrance_type.h"),
        "animation_h": os.path.join(repo_root, "include", "ostinato",
                                    "monster_animation.h"),
        "visibility_h": os.path.join(repo_root, "include", "ostinato",
                                     "monster_visibility_mode.h"),
    }


# --- symbols -------------------------------------------------------------------

def read_inc_enums(path):
    """The `.enum` blocks of ai_script.inc: {name: [(member, value)]}.

    A member is bare (one more than the member before it, 0 for the first) or
    `NAME = value` with a literal or BIT_n."""
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    enums = {}
    current = None
    following = 0
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        opened = _RE_ENUM_OPEN.match(s)
        if opened:
            if current is not None:
                raise ParseError(path, lineno, ".enum inside .enum")
            current = opened.group(1)
            if current in enums:
                raise ParseError(path, lineno,
                                 "enum {} defined twice".format(current))
            enums[current] = []
            following = 0
            continue
        if s.lower().startswith(".endenum"):
            current = None
            continue
        if current is None:
            continue
        member = _RE_MEMBER.match(s)
        if not member:
            raise ParseError(path, lineno,
                             "unexpected line in .enum {}: '{}'".format(current, s))
        value = following
        if member.group(2) is not None:
            value = _enum_value(member.group(2), path, lineno)
        enums[current].append((member.group(1), value))
        following = value + 1
    return enums


def _enum_value(text, path, lineno):
    bit = re.match(r"^BIT_(\d+)$", text)
    if bit:
        return 1 << int(bit.group(1))
    value = common.parse_int_literal(text)
    if value is None:
        raise ParseError(path, lineno, "'{}' is not a literal or BIT_n".format(text))
    return value


def read_macro_names(path):
    """Every `.mac` and `.define` name of ai_script.inc, in source order."""
    names = []
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            code, _ = strip_comment(raw)
            s = code.strip()
            mac = _RE_MAC.match(s)
            if mac:
                names.append(mac.group(1))
                continue
            define = _RE_DEFINE.match(s)
            if define:
                names.append(define.group(1))
    return names


def read_cpp_enum(path):
    """The `NAME = value,` members of a C++ enum header: {value: name}."""
    members = {}
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            m = _RE_CPP_MEMBER.match(raw.split("//")[0])
            if m:
                members.setdefault(int(m.group(2), 0), m.group(1))
    if not members:
        raise ParseError(path, 0, "no enumerators found")
    return members


class Symbols(object):
    """The enums a script's operands are written in, by name and by value."""

    CONST_ENUMS = ("ATTACK", "BATTLE_CMD", "ITEM", "STATUS_ID", "ELEMENT",
                   "CHAR_PROP", "MONSTER")
    INC_ENUMS = ("AI_TARGET", "MONSTER_MASK", "MONSTER_ENTRY_EXIT_ANIM",
                 "CHANGE_BATTLE_FLAGS_1", "CHANGE_BATTLE_FLAGS_2")

    def __init__(self, paths):
        parsed = common.parse_ca65_constants(paths["const_inc"],
                                             skip_body_enums=pce.SKIP)
        self._by_name = {}
        self._by_value = {}
        for name in self.CONST_ENUMS:
            enum = parsed.enum(name)
            if enum is None:
                raise ParseError(paths["const_inc"], 0,
                                 "expected enum '{}' not found".format(name))
            self._add(name, [(m.name, m.value) for m in enum.members])
        inc = read_inc_enums(paths["ai_inc"])
        for name in self.INC_ENUMS:
            if name not in inc:
                raise ParseError(paths["ai_inc"], 0,
                                 "expected enum '{}' not found".format(name))
            self._add(name, inc[name])
        self.selectors = list(inc["AI_TARGET"])
        self.macro_names = read_macro_names(paths["ai_inc"])
        self.formations = read_cpp_enum(paths["formation_id_h"])
        self.entrances = read_cpp_enum(paths["entrance_h"])
        self.animations = read_cpp_enum(paths["animation_h"])
        self.visibility = read_cpp_enum(paths["visibility_h"])
        self._check_selectors(paths["ai_inc"])

    def _add(self, name, members):
        self._by_name[name] = dict(members)
        by_value = {}
        for member, value in members:
            by_value.setdefault(value, member)
        self._by_value[name] = by_value

    def _check_selectors(self, path):
        values = [v for _, v in self.selectors]
        if values != list(range(FIRST_SELECTOR, FIRST_SELECTOR + len(values))):
            raise ParseError(path, 0,
                             "AI_TARGET is not one run of values from {} — the "
                             "game reads a target below {} as a character"
                             .format(FIRST_SELECTOR, FIRST_SELECTOR))

    def has(self, enum, member):
        return member in self._by_name[enum]

    def value(self, enum, member, path, lineno):
        if member not in self._by_name[enum]:
            raise ParseError(path, lineno,
                             "unknown {}::{}".format(enum, member))
        return self._by_name[enum][member]

    def name(self, enum, value, what):
        if value not in self._by_value[enum]:
            raise ParseError("ai scripts", 0,
                             "{}: {} has no {} member".format(what, value, enum))
        return self._by_value[enum][value]


# --- the macros, replayed ------------------------------------------------------

def split_args(text):
    """The comma-separated arguments of a macro line; a `{a, b}` list is one."""
    args = []
    depth = 0
    current = ""
    for ch in text:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth < 0:
                raise ValueError("unbalanced '}'")
        if ch == "," and depth == 0:
            args.append(current.strip())
            current = ""
        else:
            current += ch
    if depth != 0:
        raise ValueError("unbalanced '{'")
    if current.strip() or args:
        args.append(current.strip())
    return args


def _list_of(arg):
    """The names in a `{a, b}` argument, or the one name of a bare argument."""
    arg = arg.strip()
    if arg.startswith("{"):
        if not arg.endswith("}"):
            raise ValueError("unbalanced '{'")
        return [part.strip() for part in arg[1:-1].split(",") if part.strip()]
    return [arg]


class Assembler(object):
    """Replays ai_script.inc: one method per `.mac` or `.define`, each returning
    the bytes the macro assembles to. `mode` follows the file's own NORMAL /
    RETALIATION assertion."""

    NORMAL = "NORMAL"
    RETALIATION = "RETALIATION"

    # The `.define`s that pass a fixed first argument to a helper macro.
    _ENTRY_EXIT = {"restore_monsters": 0, "kill_monsters": 1, "show_monsters": 2,
                   "hide_monsters": 3, "kill_monsters_wait": 4,
                   "kill_monsters_debug": 5}
    _ITEM = {"use_item": 0, "throw_item": 1}
    _BATTLE_VAR = {"set_battle_var": 0x00, "add_battle_var": 0x80,
                   "sub_battle_var": 0xC0}
    _MONSTER_VAR = {"set_monster_var": 0x00, "add_monster_var": 0x80,
                    "sub_monster_var": 0xC0}
    _BATTLE_SWITCH = {"toggle_battle_switch": 0, "set_battle_switch": 1,
                      "clr_battle_switch": 2}
    _MONSTER_SWITCH = {"toggle_monster_switch": 0, "set_monster_switch": 1,
                       "clr_monster_switch": 2}
    # `.byte $fa, n, MONSTER_MASK::slot, 0`
    _MONSTER_ANIMATION = {"flash_red": 0, "move_back_slow": 1,
                          "move_forward_slow": 2, "move_back_fast": 3,
                          "move_forward_fast": 4, "move_back_64": 7,
                          "move_forward_64": 8, "kefka_head": 10,
                          "long_glow": 11, "short_glow": 12, "kefka_death": 13}
    # `.byte $fa, n, 0, 0`
    _PARTY_ANIMATION = {"chars_left_to_right": 5, "chars_right_to_left": 6}
    # `.byte $fb, n, 0`
    _PLAIN_EFFECT = {"reset_monster_timer": 0, "end_battle": 2, "recruit_gau": 3,
                     "reset_battle_timer": 4, "end_veldt": 9, "hide_piranha": 13}
    # `.byte $fb, n` then the target
    _TARGET_EFFECT = {"invincible_on": 1, "invincible_off": 5, "target_on": 6,
                      "target_off": 7, "fill_atb": 8}
    # `.byte $fb, n, STATUS_ID::status`
    _STATUS_EFFECT = {"set_status": 11, "clr_status": 12}
    # `.byte $fc, n, 0, 0`
    _PLAIN_CONDITION = {"if_never": 0x00, "if_hit": 0x05,
                        "if_one_monster_type": 0x10, "if_self_alive": 0x11,
                        "if_self_dead": 0x12, "if_gau_present": 0x18,
                        "if_always": 0x1C}
    # Helpers and file plumbing that are never written in a script.
    _INTERNAL = ("make_ai_target", "_op_monster_entry_exit", "_op_item",
                 "_op_battle_var", "_op_battle_switch", "set_ai_script_mode",
                 "assert_ai_script_mode")

    def __init__(self, symbols):
        self.symbols = symbols
        self.mode = self.NORMAL
        self._path = ""
        self._lineno = 0

    @classmethod
    def replayed(cls):
        """Every ai_script.inc name this class accounts for."""
        names = set(cls._INTERNAL) | {"boss_death"}
        for table in (cls._ENTRY_EXIT, cls._ITEM, cls._BATTLE_VAR,
                      cls._MONSTER_VAR, cls._BATTLE_SWITCH, cls._MONSTER_SWITCH,
                      cls._MONSTER_ANIMATION, cls._PARTY_ANIMATION,
                      cls._PLAIN_EFFECT, cls._TARGET_EFFECT, cls._STATUS_EFFECT,
                      cls._PLAIN_CONDITION):
            names |= set(table)
        names |= {name[4:] for name in dir(cls) if name.startswith("_do_")}
        return names

    def assemble(self, name, args, path, lineno):
        self._path = path
        self._lineno = lineno
        try:
            return self._assemble(name, args)
        except ValueError as exc:
            raise ParseError(path, lineno, "{}: {}".format(name, exc))

    # -- helpers --

    def _error(self, message):
        raise ParseError(self._path, self._lineno, message)

    def _value(self, enum, member):
        return self.symbols.value(enum, member, self._path, self._lineno)

    def _int(self, text):
        value = common.parse_int_literal(text)
        if value is None:
            self._error("'{}' is not a number".format(text))
        return value

    def _byte(self, text):
        value = self._int(text)
        if not 0 <= value <= 0xFF:
            self._error("'{}' does not fit a byte".format(text))
        return value

    def _word(self, text):
        value = self._int(text)
        if not 0 <= value <= 0xFFFF:
            self._error("'{}' does not fit a word".format(text))
        return [value & 0xFF, value >> 8]

    def _count(self, name, args, low, high=None):
        high = low if high is None else high
        if not low <= len(args) <= high:
            self._error("{} takes {} argument(s), got {}".format(
                name, low if low == high else "{}-{}".format(low, high),
                len(args)))

    def _target(self, name):
        # make_ai_target: a CHAR_PROP name wins over an AI_TARGET name.
        if self.symbols.has("CHAR_PROP", name):
            value = self._value("CHAR_PROP", name)
            if value >= FIRST_SELECTOR:
                self._error("CHAR_PROP::{} is {}: the game would read it as a "
                            "selector, not a character".format(name, value))
            return value
        return self._value("AI_TARGET", name)

    def _mask(self, enum, arg, base=0):
        # make_bitmask: `base` OR-ed with every named member.
        value = base
        for member in _list_of(arg):
            value |= self._value(enum, member)
        return value

    def _choice(self, enum, name):
        return NOTHING if name == "NOTHING" else self._value(enum, name)

    # -- the macros --

    def _assemble(self, name, args):
        if name in self._INTERNAL and name != "set_ai_script_mode":
            self._error("{} is a helper, not a script line".format(name))
        if name in self._ENTRY_EXIT:
            self._count(name, args, 1, 2)
            return self._entry_exit(self._ENTRY_EXIT[name], args)
        if name == "boss_death":
            self._count(name, args, 0)
            return self._entry_exit(1, ["ALL", "BOSS_DEATH"])
        if name in self._ITEM:
            self._count(name, args, 1, 2)
            first = self._value("ITEM", args[0])
            second = self._value("ITEM", args[1]) if len(args) == 2 else first
            return [0xF6, self._ITEM[name], first, second]
        if name in self._BATTLE_VAR:
            self._count(name, args, 2)
            return [0xF8, self._byte(args[0]),
                    self._byte(args[1]) | self._BATTLE_VAR[name]]
        if name in self._MONSTER_VAR:
            self._count(name, args, 1)
            return [0xF8, 0x24, self._byte(args[0]) | self._MONSTER_VAR[name]]
        if name in self._BATTLE_SWITCH:
            self._count(name, args, 2)
            return [0xF9, self._BATTLE_SWITCH[name], self._byte(args[0]),
                    self._byte(args[1])]
        if name in self._MONSTER_SWITCH:
            self._count(name, args, 1)
            return [0xF9, self._MONSTER_SWITCH[name], 36, self._byte(args[0])]
        if name in self._MONSTER_ANIMATION:
            self._count(name, args, 1)
            return [0xFA, self._MONSTER_ANIMATION[name],
                    self._value("MONSTER_MASK", args[0]), 0]
        if name in self._PARTY_ANIMATION:
            self._count(name, args, 0)
            return [0xFA, self._PARTY_ANIMATION[name], 0, 0]
        if name in self._PLAIN_EFFECT:
            self._count(name, args, 0)
            return [0xFB, self._PLAIN_EFFECT[name], 0]
        if name in self._TARGET_EFFECT:
            self._count(name, args, 1)
            return [0xFB, self._TARGET_EFFECT[name], self._target(args[0])]
        if name in self._STATUS_EFFECT:
            self._count(name, args, 1)
            return [0xFB, self._STATUS_EFFECT[name],
                    self._value("STATUS_ID", args[0])]
        if name in self._PLAIN_CONDITION:
            self._count(name, args, 0)
            return [0xFC, self._PLAIN_CONDITION[name], 0, 0]
        handler = getattr(self, "_do_" + name, None)
        if handler is None:
            self._error("'{}' is not an ai_script.inc macro (grammar not "
                        "covered — escalate, never guess)".format(name))
        return handler(args)

    def _entry_exit(self, op, args):
        # _op_monster_entry_exit op, monster_mask, anim — called as
        # `<define> monster_mask, anim`.
        if len(args) != 2:
            self._error("a monster entry or exit takes a mask and an animation")
        mask = self._mask("MONSTER_MASK", args[0]) if args[0] else 0
        return [0xF5, self._value("MONSTER_ENTRY_EXIT_ANIM", args[1]), op, mask]

    def _do_attack(self, args):
        self._count("attack", args, 1, 3)
        if len(args) == 1:
            if args[0] == "NOTHING":
                return [0xF0, NOTHING, NOTHING, NOTHING]
            attack = self._value("ATTACK", args[0])
            if attack < 0xF0:
                return [attack]
            return [0xF0, attack, attack, attack]
        if len(args) != 3:
            self._error("attack takes one attack or three")
        return [0xF0] + [self._choice("ATTACK", a) for a in args]

    def _do_set_target(self, args):
        self._count("set_target", args, 1)
        return [0xF1, self._target(args[0])]

    def _do_change_battle(self, args):
        self._count("change_battle", args, 2, 3)
        battle = self._int(args[0])
        exit_type = self._value("MONSTER_ENTRY_EXIT_ANIM", args[1])
        if len(args) == 3:
            exit_type = self._mask("CHANGE_BATTLE_FLAGS_1", args[2], exit_type)
            battle = self._mask("CHANGE_BATTLE_FLAGS_2", args[2], battle)
        if not 0 <= exit_type <= 0xFF or not 0 <= battle <= 0xFFFF:
            self._error("change_battle operands out of range")
        return [0xF2, exit_type, battle & 0xFF, battle >> 8]

    def _do_dlg(self, args):
        self._count("dlg", args, 1)
        return [0xF3] + self._word(args[0])

    def _do_cmd(self, args):
        self._count("cmd", args, 1, 3)
        if len(args) == 1:
            command = self._value("BATTLE_CMD", args[0])
            return [0xF4, command, command, command]
        if len(args) != 3:
            self._error("cmd takes one command or three")
        return [0xF4] + [self._choice("BATTLE_CMD", c) for c in args]

    def _do_battle_event(self, args):
        self._count("battle_event", args, 1)
        return [0xF7, self._byte(args[0])]

    def _do_sfx(self, args):
        self._count("sfx", args, 2)
        return [0xFA, 0x09, self._byte(args[1]), self._byte(args[0])]

    def _pair(self, name, args, number, enum):
        self._count(name, args, 1, 2)
        first = self._value(enum, args[0])
        second = self._value(enum, args[1]) if len(args) == 2 else first
        return [0xFC, number, first, second]

    def _do_if_cmd(self, args):
        return self._pair("if_cmd", args, 0x01, "BATTLE_CMD")

    def _do_if_attack(self, args):
        return self._pair("if_attack", args, 0x02, "ATTACK")

    def _do_if_item(self, args):
        return self._pair("if_item", args, 0x03, "ITEM")

    def _do_if_element(self, args):
        self._count("if_element", args, 1)
        return [0xFC, 0x04, self._mask("ELEMENT", args[0]), 0]

    def _do_if_hp(self, args):
        self._count("if_hp", args, 2)
        hp = self._int(args[1])
        if hp % HP_STEP:
            self._error("hp {} is not a multiple of {}".format(hp, HP_STEP))
        if hp // HP_STEP > 0xFF:
            self._error("hp {} does not fit".format(hp))
        return [0xFC, 0x06, self._target(args[0]), hp // HP_STEP]

    def _target_and_byte(self, name, args, number):
        self._count(name, args, 2)
        return [0xFC, number, self._target(args[0]), self._byte(args[1])]

    def _do_if_mp(self, args):
        return self._target_and_byte("if_mp", args, 0x07)

    def _status(self, name, args, number):
        self._count(name, args, 2)
        return [0xFC, number, self._target(args[0]),
                self._value("STATUS_ID", args[1])]

    def _do_if_status_set(self, args):
        return self._status("if_status_set", args, 0x08)

    def _do_if_status_clr(self, args):
        return self._status("if_status_clr", args, 0x09)

    def _do_if_monster_timer(self, args):
        self._count("if_monster_timer", args, 1)
        return [0xFC, 0x0B, self._byte(args[0]), 0]

    def _two_bytes(self, name, args, number):
        self._count(name, args, 2)
        return [0xFC, number, self._byte(args[0]), self._byte(args[1])]

    def _do_if_battle_var_less(self, args):
        return self._two_bytes("if_battle_var_less", args, 0x0C)

    def _do_if_battle_var_greater(self, args):
        return self._two_bytes("if_battle_var_greater", args, 0x0D)

    def _do_if_level_less(self, args):
        return self._target_and_byte("if_level_less", args, 0x0E)

    def _do_if_level_greater(self, args):
        return self._target_and_byte("if_level_greater", args, 0x0F)

    def _monsters(self, name, args, number):
        self._count(name, args, 1)
        return [0xFC, number, self._mask("MONSTER_MASK", args[0]), 0]

    def _do_if_monsters_alive(self, args):
        return self._monsters("if_monsters_alive", args, 0x11)

    def _do_if_monsters_dead(self, args):
        return self._monsters("if_monsters_dead", args, 0x12)

    def _do_if_num_chars(self, args):
        self._count("if_num_chars", args, 1)
        return [0xFC, 0x13, 0, self._byte(args[0])]

    def _do_if_num_monsters(self, args):
        self._count("if_num_monsters", args, 1)
        return [0xFC, 0x13, 1, self._byte(args[0])]

    def _do_if_battle_switch_set(self, args):
        return self._two_bytes("if_battle_switch_set", args, 0x14)

    def _do_if_battle_switch_clr(self, args):
        return self._two_bytes("if_battle_switch_clr", args, 0x15)

    def _do_if_monster_switch_set(self, args):
        self._count("if_monster_switch_set", args, 1)
        return [0xFC, 0x14, 0x24, self._int(args[0]) % 8]

    def _do_if_monster_switch_clr(self, args):
        self._count("if_monster_switch_clr", args, 1)
        return [0xFC, 0x15, 0x24, self._int(args[0]) % 8]

    def _do_if_battle_timer(self, args):
        self._count("if_battle_timer", args, 1)
        return [0xFC, 0x16, self._byte(args[0]), 0]

    def _do_if_target_valid(self, args):
        self._count("if_target_valid", args, 1)
        return [0xFC, 0x17, self._target(args[0]), 0]

    def _do_if_self_in_slot(self, args):
        self._count("if_self_in_slot", args, 1)
        return [0xFC, 0x19, self._value("MONSTER_MASK", args[0]), 0]

    def _do_if_weak_element(self, args):
        self._count("if_weak_element", args, 2)
        return [0xFC, 0x1A, self._target(args[0]),
                self._mask("ELEMENT", args[1])]

    def _do_if_battle_id(self, args):
        self._count("if_battle_id", args, 1)
        return [0xFC, 0x1B] + self._word(args[0])

    def _do_wait(self, args):
        self._count("wait", args, 0)
        return [0xFD]

    def _do_end_if(self, args):
        self._count("end_if", args, 0)
        return [0xFE]

    def _do_end(self, args):
        self._count("end", args, 0)
        if self.mode != self.NORMAL:
            self._error("Invalid A.I. script mode: `end` inside a counter part")
        self.mode = self.RETALIATION
        return [0xFF]

    def _do_end_retal(self, args):
        self._count("end_retal", args, 0)
        if self.mode != self.RETALIATION:
            self._error("Invalid A.I. script mode: `end_retal` with no `end` "
                        "before it")
        self.mode = self.NORMAL
        return [0xFF]

    def _do_set_ai_script_mode(self, args):
        self._count("set_ai_script_mode", args, 1)
        if args[0] not in (self.NORMAL, self.RETALIATION):
            self._error("unknown script mode '{}'".format(args[0]))
        self.mode = args[0]
        return []


def check_macro_coverage(symbols, path):
    """The macros replayed are exactly the macros ai_script.inc defines."""
    defined = set(symbols.macro_names)
    replayed = Assembler.replayed()
    missing = sorted(defined - replayed)
    extra = sorted(replayed - defined)
    if missing:
        raise ParseError(path, 0, "ai_script.inc defines macros this parser "
                         "does not replay: {}".format(", ".join(missing)))
    if extra:
        raise ParseError(path, 0, "this parser replays macros ai_script.inc "
                         "does not define: {}".format(", ".join(extra)))


# --- the script file -----------------------------------------------------------

class Assembled(object):
    """ai_script.asm assembled: the block's bytes and each script's offset."""

    def __init__(self):
        self.block = []
        self.offsets = {}        # script number -> offset in the block
        self.count = None        # AIScript::ARRAY_LENGTH
        self.block_sizes = []    # the fixed_block sizes, EN first


def assemble_scripts(asm_path, symbols):
    """Walk ai_script.asm, replaying every script line."""
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    assembler = Assembler(symbols)
    out = Assembled()
    in_scope = False
    in_block = False
    closed = False
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()
        if low.startswith(".scope"):
            in_scope = True
            continue
        if low.startswith(".endscope"):
            in_scope = False
            continue
        if in_scope:
            m = re.match(r"^ARRAY_LENGTH\s*=\s*(\d+)$", s)
            if m:
                out.count = int(m.group(1))
            elif not s.startswith("Start"):
                raise ParseError(asm_path, lineno,
                                 "unexpected line in .scope AIScript: '{}'".format(s))
            continue
        if low.startswith((".segment", ".if", ".else", ".endif")):
            continue
        if low.startswith(".delmac"):
            name = s.split()[1]
            if name not in symbols.macro_names:
                raise ParseError(asm_path, lineno,
                                 ".delmac of '{}', which ai_script.inc does not "
                                 "define".format(name))
            continue
        if s in ("AIScriptPtrs:", "AIScript:"):
            continue
        if s == "ptr_tbl AIScript":
            continue
        fixed = re.match(r"^fixed_block\s+(\S+)$", s)
        if fixed:
            size = common.parse_int_literal(fixed.group(1))
            if size is None:
                raise ParseError(asm_path, lineno, "fixed_block size is not a number")
            out.block_sizes.append(size)
            in_block = True
            continue
        if s == "end_fixed_block":
            in_block = False
            closed = True
            continue
        label = _RE_SCRIPT_LABEL.match(s)
        if label:
            number = int(label.group(1))
            if number in out.offsets:
                raise ParseError(asm_path, lineno,
                                 "script {} is defined twice".format(number))
            out.offsets[number] = len(out.block)
            continue
        if not in_block:
            raise ParseError(asm_path, lineno,
                             "unexpected line outside the script block: '{}'".format(s))
        parts = s.split(None, 1)
        name = parts[0]
        if not _RE_IDENT.match(name):
            raise ParseError(asm_path, lineno, "unexpected line: '{}'".format(s))
        try:
            args = split_args(parts[1]) if len(parts) > 1 else []
        except ValueError as exc:
            raise ParseError(asm_path, lineno, str(exc))
        out.block += assembler.assemble(name, args, asm_path, lineno)

    if out.count is None:
        raise ParseError(asm_path, 0, "AIScript::ARRAY_LENGTH not found")
    if not closed:
        raise ParseError(asm_path, 0, "the script block is never closed")
    if assembler.mode != Assembler.NORMAL:
        raise ParseError(asm_path, 0,
                         "the last script has no counter part terminator")
    if sorted(out.offsets) != list(range(out.count)):
        raise ParseError(asm_path, 0,
                         "the scripts are not numbered 0..{}".format(out.count - 1))
    if not out.block_sizes or len(out.block) > min(out.block_sizes):
        raise ParseError(asm_path, 0,
                         "the scripts ({} bytes) do not fit the block".format(
                             len(out.block)))
    return out


def assert_rom(rom, assembled, path):
    pointers = []
    for number in range(assembled.count):
        offset = assembled.offsets[number]
        pointers += [offset & 0xFF, offset >> 8]
    _compare(rom, POINTERS_SNES, pointers, "AIScriptPtrs", path)
    _compare(rom, SCRIPTS_SNES, assembled.block, "AIScript", path)


def _compare(rom, snes, want, label, path):
    base = common.hirom_file_offset(snes)
    got = list(rom[base:base + len(want)])
    if got != want:
        at = next((i for i, (g, w) in enumerate(zip(got, want)) if g != w),
                  min(len(got), len(want)))
        raise ParseError(path, 0,
                         "ROM MISMATCH in {} at ${:06X}+{}: the cartridge "
                         "differs from the assembled source"
                         .format(label, snes, at))


# --- the typed steps -----------------------------------------------------------

class Part(object):
    """One part of a script: where the cartridge holds it and its steps."""

    def __init__(self, offset, steps, end):
        self.offset = offset
        self.steps = steps   # the C++ builder calls, in order
        self.end = end       # the offset just past its `end`


class Script(object):
    def __init__(self, number, turn):
        self.number = number
        self.turn = turn
        self.counter = None        # a Part, when the script owns one
        self.counter_of = None     # the script whose turn part it borrows
        self.name = None           # its MonsterId enumerator


class Decoder(object):
    """Reads typed steps out of script bytes by the game's own reading rules."""

    def __init__(self, symbols):
        self.symbols = symbols
        self._at = 0

    def _error(self, message):
        raise ParseError("ai scripts", 0,
                         "at offset ${:04X}: {}".format(self._at, message))

    def part(self, block, offset):
        """The steps from `offset` through the first `end`."""
        steps = []
        while True:
            if offset >= len(block):
                self._at = offset
                self._error("a script part runs off the end of the block")
            self._at = offset
            first = block[offset]
            size = 1 if first < 0xF0 else COMMAND_SIZES[first - 0xF0]
            raw = block[offset:offset + size]
            if len(raw) != size:
                self._error("a step runs off the end of the block")
            steps.append(self.step(raw))
            offset += size
            if first == 0xFF:
                return Part(None, steps, offset)

    # -- operands --

    def _attack(self, value):
        return "AttackId::" + self.symbols.name("ATTACK", value, "attack")

    def _command(self, value):
        return "BattleCommandId::" + self.symbols.name("BATTLE_CMD", value, "command")

    def _item(self, value):
        return "ItemId::" + self.symbols.name("ITEM", value, "item")

    def _status(self, value):
        return "StatusId::" + self.symbols.name("STATUS_ID", value, "status")

    def _choice(self, value, render, none):
        return none if value == NOTHING else render(value)

    def _elements(self, mask):
        names = []
        for bit in range(8):
            if mask & (1 << bit):
                names.append("Element::" + self.symbols.name("ELEMENT", 1 << bit,
                                                             "element"))
        if not names:
            self._error("an element condition names no element")
        return "ElementSet::of({})".format(", ".join(names))

    def _target(self, value):
        if value < FIRST_SELECTOR:
            return "CharacterPropId::" + self.symbols.name("CHAR_PROP", value,
                                                            "target")
        selectors = dict((v, n) for n, v in self.symbols.selectors)
        if value not in selectors:
            self._error("target {} is not a selector".format(value))
        return "AiTargetSelector::" + selectors[value]

    def _slots(self, mask):
        if mask == 0xFF:
            return "AiMonsters::all()"
        if mask == 0 or mask & 0xC0:
            self._error("monster mask ${:02X} is not a set of the six slots".format(mask))
        return "AiMonsters::of({})".format(", ".join(
            "AiTargetSelector::MONSTER_SLOT_{}".format(bit + 1)
            for bit in range(6) if mask & (1 << bit)))

    def _monsters_or_self(self, mask):
        return "AiMonsters::self()" if mask == 0 else self._slots(mask)

    def _named(self, table, value, enum, what):
        if value not in table:
            self._error("{} {} has no {} enumerator".format(what, value, enum))
        return "{}::{}".format(enum, table[value])

    def _variable(self, index):
        if index < BATTLE_VARIABLES:
            return "AiVariable::battle({})".format(index)
        if index == OWN_VARIABLE:
            return "AiVariable::own()"
        self._error("variable {} is neither a battle variable (0-{}) nor the "
                    "monster's own ({})".format(index, BATTLE_VARIABLES - 1,
                                                OWN_VARIABLE))

    def _switch(self, index, bit):
        if bit > 7:
            self._error("switch bit {} is not 0-7".format(bit))
        if index < BATTLE_VARIABLES:
            return "AiSwitch::battleVariable({}, {})".format(index, bit)
        if FIRST_SWITCH_VARIABLE <= index <= LAST_SWITCH_VARIABLE:
            return "AiSwitch::battleSwitch({})".format(
                (index - FIRST_SWITCH_VARIABLE) * 8 + bit)
        if index == OWN_VARIABLE:
            return "AiSwitch::own({})".format(bit)
        self._error("a switch in variable {} has no typed form".format(index))

    def _zero(self, value, what):
        if value != 0:
            self._error("{} holds {}, which the game never reads".format(what, value))

    # -- steps --

    def step(self, raw):
        first = raw[0]
        if first < 0xF0:
            return "AiStep::attack({})".format(self._attack(first))
        handler = getattr(self, "_f{:x}".format(first - 0xF0))
        return handler(raw)

    def _f0(self, raw):
        return "AiStep::attackOneOf({{ {} }})".format(", ".join(
            self._choice(b, self._attack, "kNoAttack") for b in raw[1:]))

    def _f1(self, raw):
        return "AiStep::setTarget({})".format(self._target(raw[1]))

    def _f2(self, raw):
        word = raw[2] | raw[3] << 8
        fields = [".formation = " + self._named(self.symbols.formations,
                                                word & 0x7FFF, "FormationId",
                                                "formation"),
                  ".exit = " + self._named(self.symbols.entrances, raw[1] & 0x7F,
                                           "MonsterEntranceType", "exit")]
        if word & 0x8000:
            fields.append(".restoreMonsters = true")
        if raw[1] & 0x80:
            fields.append(".scrollBackground = true")
        return "AiStep::changeBattle({{ {} }})".format(", ".join(fields))

    def _f3(self, raw):
        if raw[2] != 0:
            self._error("dialog {} is past 255".format(raw[1] | raw[2] << 8))
        return "AiStep::dialog({})".format(raw[1])

    def _f4(self, raw):
        return "AiStep::commandOneOf({{ {} }})".format(", ".join(
            self._choice(b, self._command, "kNoCommand") for b in raw[1:]))

    def _f5(self, raw):
        return ("AiStep::showOrHideMonsters({{ .mode = {}, .monsters = {}, "
                ".type = {} }})".format(
                    self._named(self.symbols.visibility, raw[2],
                                "MonsterVisibilityMode", "visibility mode"),
                    self._monsters_or_self(raw[3]),
                    self._named(self.symbols.entrances, raw[1],
                                "MonsterEntranceType", "entrance")))

    def _f6(self, raw):
        if raw[1] > 1:
            self._error("an item step's first operand is {}, not 0 or 1".format(raw[1]))
        return "AiStep::{}({{ .twoInThree = {}, .oneInThree = {} }})".format(
            "throwItemOneOf" if raw[1] else "useItemOneOf",
            self._choice(raw[2], self._item, "kNoItem"),
            self._choice(raw[3], self._item, "kNoItem"))

    def _f7(self, raw):
        return "AiStep::battleEvent({})".format(raw[1])

    def _f8(self, raw):
        variable = self._variable(raw[1])
        operand = raw[2]
        if operand < 0x80:
            if operand > LARGEST_AMOUNT:
                self._error("a variable is set to {}, more than {}".format(
                    operand, LARGEST_AMOUNT))
            return "AiStep::setVariable({{ .variable = {}, .value = {} }})".format(
                variable, operand)
        return "AiStep::{}({{ .variable = {}, .amount = {} }})".format(
            "subtractFromVariable" if operand & 0x40 else "addToVariable",
            variable, operand & LARGEST_AMOUNT)

    def _f9(self, raw):
        builders = ("toggleSwitch", "setSwitch", "clearSwitch")
        if raw[1] >= len(builders):
            self._error("switch operation {} is not 0-2".format(raw[1]))
        return "AiStep::{}({})".format(builders[raw[1]],
                                       self._switch(raw[2], raw[3]))

    def _fa(self, raw):
        animation = self._named(self.symbols.animations, raw[1],
                                "MonsterAnimation", "animation")
        if animation.endswith("::SoundEffect"):
            return "AiStep::soundEffect({{ .sound = {}, .pan = {} }})".format(
                raw[3], raw[2])
        self._zero(raw[3], "an animation's last byte")
        if animation.endswith("::PartyRunsLeftToRight"):
            self._zero(raw[2], "a party animation's monsters")
            return "AiStep::partyRunsLeftToRight()"
        if animation.endswith("::PartyRunsRightToLeft"):
            self._zero(raw[2], "a party animation's monsters")
            return "AiStep::partyRunsRightToLeft()"
        return "AiStep::animate({{ .animation = {}, .monsters = {} }})".format(
            animation, self._slots(raw[2]))

    _PLAIN_EFFECTS = {0: "resetOwnTimer", 2: "endBattle", 3: "recruitGau",
                      4: "resetBattleTimer", 9: "endBattleLearningRages",
                      13: "hideSelf"}
    _TARGET_EFFECTS = {1: "makeInvincible", 5: "endInvincible",
                       6: "makeTargetable", 7: "makeUntargetable", 8: "fillGauge"}
    _STATUS_EFFECTS = {11: "setStatus", 12: "clearStatus"}

    def _fb(self, raw):
        effect, operand = raw[1], raw[2]
        if effect in self._PLAIN_EFFECTS:
            self._zero(operand, "an effect's operand")
            return "AiStep::{}()".format(self._PLAIN_EFFECTS[effect])
        if effect in self._TARGET_EFFECTS:
            return "AiStep::{}({})".format(self._TARGET_EFFECTS[effect],
                                           self._target(operand))
        if effect in self._STATUS_EFFECTS:
            return "AiStep::{}({})".format(self._STATUS_EFFECTS[effect],
                                           self._status(operand))
        self._error("effect {} has no typed form".format(effect))

    def _either(self, builder, first, second, render):
        if first == second:
            return "AiStep::{}({})".format(builder, render(first))
        return "AiStep::{}({}, {})".format(builder, render(first), render(second))

    def _fc(self, raw):
        number, a, b = raw[1], raw[2], raw[3]
        if number in (0x00, 0x0A):
            self._zero(a | b, "a never condition's operands")
            return "AiStep::never()"
        if number == 0x01:
            return self._either("ifHitByCommand", a, b, self._command)
        if number == 0x02:
            return self._either("ifHitByAttack", a, b, self._attack)
        if number == 0x03:
            return self._either("ifHitByItem", a, b, self._item)
        if number == 0x04:
            self._zero(b, "an element condition's last byte")
            return "AiStep::ifHitByElement({})".format(self._elements(a))
        if number == 0x05:
            self._zero(a | b, "a damaged condition's operands")
            return "AiStep::ifDamaged()"
        if number == 0x06:
            return "AiStep::ifHpAtMost({{ .target = {}, .hp = {} }})".format(
                self._target(a), b * HP_STEP)
        if number == 0x07:
            return "AiStep::ifMpAtMost({{ .target = {}, .mp = {} }})".format(
                self._target(a), b)
        if number in (0x08, 0x09):
            return "AiStep::{}({{ .target = {}, .status = {} }})".format(
                "ifStatus" if number == 0x08 else "ifNoStatus",
                self._target(a), self._status(b))
        if number == 0x0B:
            self._zero(b, "a timer condition's last byte")
            return "AiStep::ifHalfOwnTimerAtLeast({})".format(a)
        if number in (0x0C, 0x0D):
            return "AiStep::{}({{ .variable = {}, .value = {} }})".format(
                "ifVariableBelow" if number == 0x0C else "ifVariableAtLeast",
                self._variable(a), b)
        if number in (0x0E, 0x0F):
            return "AiStep::{}({{ .target = {}, .level = {} }})".format(
                "ifLevelBelow" if number == 0x0E else "ifLevelAtLeast",
                self._target(a), b)
        if number == 0x10:
            self._zero(a | b, "a monster-kind condition's operands")
            return "AiStep::ifOneKindOfMonster()"
        if number in (0x11, 0x12):
            self._zero(b, "a monsters condition's last byte")
            return "AiStep::{}({})".format(
                "ifMonstersAlive" if number == 0x11 else "ifMonstersDead",
                self._monsters_or_self(a))
        if number == 0x13:
            if a > 1:
                self._error("a count condition's first operand is {}, not 0 "
                            "or 1".format(a))
            return "AiStep::{}({})".format(
                "ifMonstersAtMost" if a else "ifPartyAtLeast", b)
        if number in (0x14, 0x15):
            return "AiStep::{}({})".format(
                "ifSwitchSet" if number == 0x14 else "ifSwitchClear",
                self._switch(a, b))
        if number == 0x16:
            self._zero(b, "a timer condition's last byte")
            return "AiStep::ifHalfBattleTimerAtLeast({})".format(a)
        if number == 0x17:
            self._zero(b, "a target condition's last byte")
            return "AiStep::ifTargetValid({})".format(self._target(a))
        if number == 0x18:
            self._zero(a | b, "a Gau condition's operands")
            return "AiStep::ifGauNotRecruited()"
        if number == 0x19:
            self._zero(b, "a slot condition's last byte")
            return "AiStep::ifInSlot({})".format(self._slots(a))
        if number == 0x1A:
            return "AiStep::ifWeakTo({{ .target = {}, .elements = {} }})".format(
                self._target(a), self._elements(b))
        if number == 0x1B:
            return "AiStep::ifFormation({})".format(self._named(
                self.symbols.formations, a | b << 8, "FormationId", "formation"))
        if number == 0x1C:
            self._zero(a | b, "an always condition's operands")
            return "AiStep::always()"
        self._error("condition ${:02X} has no typed form".format(number))

    def _fd(self, raw):
        return "AiStep::wait()"

    def _fe(self, raw):
        return "AiStep::endIf()"

    def _ff(self, raw):
        return "AiStep::end()"


def read_scripts(assembled, symbols):
    """Each script's parts, read from the assembled bytes as the game's setup
    reads them: the turn part, then a counter part unless the byte after the
    turn part's `end` is another `end`."""
    block = assembled.block
    decoder = Decoder(symbols)
    scripts = []
    for number in range(assembled.count):
        offset = assembled.offsets[number]
        turn = decoder.part(block, offset)
        turn.offset = offset
        script = Script(number, turn)
        script.name = symbols.name("MONSTER", number, "script")
        if turn.end >= len(block):
            raise ParseError("ai scripts", 0,
                             "script {} has no byte after its turn part".format(number))
        if block[turn.end] != 0xFF:
            script.counter = decoder.part(block, turn.end)
            script.counter.offset = turn.end
        scripts.append(script)
    _check_tiling(scripts, len(block))
    return scripts


def _check_tiling(scripts, block_length):
    """The scripts tile the block in cartridge order. A script whose counter
    part starts where the next script starts borrows that script's turn part."""
    in_order = sorted(scripts, key=lambda s: s.turn.offset)
    if in_order[0].turn.offset != 0:
        raise ParseError("ai scripts", 0, "the first script does not start the block")
    for script, following in zip(in_order, in_order[1:] + [None]):
        next_start = block_length if following is None else following.turn.offset
        own_end = (script.turn.end + 1 if script.counter is None
                   else script.counter.end)
        if own_end == next_start:
            continue
        if (script.counter is not None and following is not None
                and script.counter.offset == following.turn.offset
                and script.counter.end == following.turn.end):
            script.counter = None
            script.counter_of = following.number
            continue
        raise ParseError("ai scripts", 0,
                         "script {} ends at ${:04X}; the next script starts at "
                         "${:04X}".format(script.number, own_end, next_start))


# --- rendering -----------------------------------------------------------------

def array_name(enumerator, part):
    """kMagRoader4Turn for MAG_ROADER_4's turn part."""
    return "k{}{}".format("".join(w[:1].upper() + w[1:].lower()
                                  for w in enumerator.split("_") if w), part)


def check_names(scripts):
    seen = {}
    for script in scripts:
        name = array_name(script.name, "")
        if name in seen:
            raise ParseError("ai scripts", 0,
                             "scripts {} and {} both become {}".format(
                                 seen[name], script.number, name))
        seen[name] = script.number


_REGEN = ("// DO NOT EDIT BY HAND — regenerate via:\n"
          "//   python3 tools/asm_parser/parse_ai_scripts.py \\\n"
          "//       --source-root original-src\n")

_SOURCE = (
    "// Source: src/battle/ai_script.asm (AIScriptPtrs, 384 words, ROM $CF/8400;\n"
    "//         AIScript, ROM $CF/8700)\n"
    "// Source: include/battle/ai_script.inc (the script macros, AI_TARGET),\n"
    "//         include/const.inc (ATTACK, BATTLE_CMD, ITEM, STATUS_ID, ELEMENT,\n"
    "//         CHAR_PROP, MONSTER)\n"
    "// (original-src pinned at 1ea47b5; the pointer table and every script's\n"
    "// bytes compared with the vanilla cartridge over the whole block)\n")


def render_selector_h(symbols):
    width = max(len(n) for n, _ in symbols.selectors)
    out = ["// AUTO-GENERATED by tools/asm_parser/parse_ai_scripts.py\n"
           "// Source: original-src/include/battle/ai_script.inc (ca65 .enum "
           "AI_TARGET)\n"
           "// (original-src pinned at 1ea47b5)\n", _REGEN,
           "//\n"
           "// Whom a step of a monster's AI script names, other than one party\n"
           "// member by who it is: a monster slot, a party slot, a side, one\n"
           "// member of a side picked at random, the battler that last attacked.\n"
           "// AiTarget (src/data/ai_scripts.h) holds one of these or a\n"
           "// CharacterPropId.\n"
           "#pragma once\n\n#include <cstdint>\n\nnamespace ostinato {\n\n"
           "enum class AiTargetSelector : std::uint8_t {\n"]
    for name, value in symbols.selectors:
        out.append("    {}{} = 0x{:02X},\n".format(
            name, " " * (width - len(name)), value))
    out.append("};\n\n}  // namespace ostinato\n")
    return "".join(out)


def render_steps_inc(scripts):
    out = ["// AUTO-GENERATED by tools/asm_parser/parse_ai_scripts.py\n", _SOURCE,
           _REGEN, "\n"
           "// The steps of every monster AI script, one array per part, in\n"
           "// MonsterId order: k<Monster>Turn, and k<Monster>Counter for a\n"
           "// script that answers attacks. #included at namespace scope in\n"
           "// src/data/ai_scripts.cpp, ahead of the kAiScripts table that points\n"
           "// at these arrays. Each step is one AiStep builder call\n"
           "// (src/data/ai_scripts.h).\n\n"]
    for script in scripts:
        for part, steps in (("Turn", script.turn.steps),
                            ("Counter", None if script.counter is None
                             else script.counter.steps)):
            if steps is None:
                continue
            out.append("constexpr AiStep {}[] = {{\n".format(
                array_name(script.name, part)))
            for step in steps:
                out.append("    {},\n".format(step))
            out.append("};\n\n")
    return "".join(out).rstrip("\n") + "\n"


def counter_reference(script, scripts):
    if script.counter is not None:
        return array_name(script.name, "Counter")
    if script.counter_of is not None:
        return array_name(scripts[script.counter_of].name, "Turn")
    return "{}"


def render_table_inc(scripts):
    width = max(len(s.name) for s in scripts)
    turn_width = max(len(array_name(s.name, "Turn")) for s in scripts)
    out = ["// AUTO-GENERATED by tools/asm_parser/parse_ai_scripts.py\n", _SOURCE,
           _REGEN, "\n"
           "// AiScript rows in MonsterId order, #included inside the kAiScripts\n"
           "// array in src/data/ai_scripts.cpp. Each row's identity is its\n"
           "// .monster field; a compile-time assert verifies it matches the\n"
           "// row's position. .turn and .counter name the step arrays of\n"
           "// ai_script_steps_data.inc; an empty .counter is a script that\n"
           "// answers no attack.\n\n"]
    for script in scripts:
        turn = array_name(script.name, "Turn")
        out.append("    {{ .monster = MonsterId::{},{} .turn = {},{} .counter = {} }},\n"
                   .format(script.name, " " * (width - len(script.name)),
                           turn, " " * (turn_width - len(turn)),
                           counter_reference(script, scripts)))
    return "".join(out)


_FIXTURE_STRUCT = (
    "// Where the cartridge holds one script and how long its parts are. The\n"
    "// offsets count from the start of the script block (ROM $CF/8700); a\n"
    "// script with no counter part has no counterOffset and 0 counterSteps. The\n"
    "// id is raw decimal so the fixture stays independent of the port's\n"
    "// MonsterId header.\n"
    "struct ExpectedAiScript {\n"
    "    std::uint16_t id;\n"
    "    std::uint16_t turnOffset;\n"
    "    std::optional<std::uint16_t> counterOffset;\n"
    "    std::uint16_t turnSteps;\n"
    "    std::uint16_t counterSteps;\n"
    "};\n"
)


def render_fixture(scripts, block_length):
    out = ["// AUTO-GENERATED by tools/asm_parser/parse_ai_scripts.py\n", _SOURCE,
           _REGEN, "\n"
           "// Test fixture for tests/test_ai_scripts.cpp — where each script's\n"
           "// parts sit in the cartridge's script block.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstddef>\n"
           "#include <cstdint>\n#include <optional>\n\n"
           "namespace ostinato::test {\n\n", _FIXTURE_STRUCT,
           "\n// How many bytes of the block the scripts fill.\n"
           "inline constexpr std::size_t kExpectedAiScriptBytes = {};\n"
           .format(block_length),
           "\ninline constexpr std::array<ExpectedAiScript, {}>\n"
           "    kExpectedAiScripts = {{{{\n".format(len(scripts))]
    for script in scripts:
        if script.counter is not None:
            counter_offset = str(script.counter.offset)
            counter_steps = len(script.counter.steps)
        elif script.counter_of is not None:
            borrowed = scripts[script.counter_of].turn
            counter_offset = str(borrowed.offset)
            counter_steps = len(borrowed.steps)
        else:
            counter_offset = "std::nullopt"
            counter_steps = 0
        out.append("    {{ .id = {}, .turnOffset = {}, .counterOffset = {}, "
                   ".turnSteps = {}, .counterSteps = {} }},\n".format(
                       script.number, script.turn.offset, counter_offset,
                       len(script.turn.steps), counter_steps))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver --------------------------------------------------------------------

def read_all(source_root, repo_root):
    paths = _paths(source_root, repo_root)
    symbols = Symbols(paths)
    check_macro_coverage(symbols, paths["ai_inc"])
    assembled = assemble_scripts(paths["ai_asm"], symbols)
    return paths, symbols, assembled


def run(source_root, repo_root, outputs, check_only=False):
    paths, symbols, assembled = read_all(source_root, repo_root)
    assert_rom(common.load_vanilla_rom(source_root), assembled, paths["ai_asm"])
    scripts = read_scripts(assembled, symbols)
    check_names(scripts)
    with_counter = sum(1 for s in scripts
                       if s.counter is not None or s.counter_of is not None)
    steps = sum(len(s.turn.steps) + (len(s.counter.steps) if s.counter else 0)
                for s in scripts)
    if check_only:
        print("OK: {} scripts, {} with a counter part ({} borrowed), {} steps, "
              "{} bytes; the pointer table and the block are cartridge-identical."
              .format(len(scripts), with_counter,
                      sum(1 for s in scripts if s.counter_of is not None),
                      steps, len(assembled.block)))
        return 0
    _write(outputs["selector_enum"], render_selector_h(symbols))
    _write(outputs["steps_inc"], render_steps_inc(scripts))
    _write(outputs["table_inc"], render_table_inc(scripts))
    _write(outputs["fixture"], render_fixture(scripts, len(assembled.block)))
    for key in ("selector_enum", "steps_inc", "table_inc", "fixture"):
        print("Emitted -> {}".format(outputs[key]))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root", default="original-src")
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--steps-inc-out",
                    default="src/data/generated/ai_script_steps_data.inc")
    ap.add_argument("--table-inc-out",
                    default="src/data/generated/ai_scripts_data.inc")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/ai_scripts_expected.h")
    ap.add_argument("--selector-enum-out",
                    default="include/ostinato/ai_target_selector.h")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)
    outputs = {"steps_inc": args.steps_inc_out, "table_inc": args.table_inc_out,
               "fixture": args.fixture_out,
               "selector_enum": args.selector_enum_out}
    try:
        return run(args.source_root, args.repo_root, outputs,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
