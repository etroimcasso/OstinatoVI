#!/usr/bin/env python3
"""Emit the character level-progression tables from field/event.asm.

Port-time tooling (NOT a build/CI dependency). Levelling up is driven by seven
tables that live with the field event code rather than the battle engine, and
are imported by it:

  Per-level progression (98 rows each, levels 2-99):
    * LevelUpExp (:1329, seg "level_up_exp") — experience deltas (.word)
    * LevelUpHP  (:1385, e6/f4a0)           — max-HP deltas
    * LevelUpMP  (:1441, e6/f502)           — max-MP deltas, forked by language

  Ability learning:
    * BushidoLevelTbl (:1235, e6/f490) — the level each swdtech is learned at
    * BlitzLevelTbl   (:1239, e6/f498) — the level each blitz is learned at
    * LearnAbilityTbl (:1228)          — cumulative learned-ability bitmasks

  Party level:
    * CharLevelModTbl (:1224) — the level offset a character joins at, relative
      to the party average, keyed by the character's CHAR_LEVEL_MOD setting

LevelUpMP is the port's first version-forked table: the disassembly commits
BOTH branches of its `.if LANG_EN` (the English deltas and the Japanese ones),
so both are emitted and both are fully testable without a second ROM.

Structural guarantees, hard-errored at emit time:
  * every run's length is exactly the expected row count, and the two LevelUpMP
    branches are the same length as each other;
  * LearnAbilityTbl is the cumulative low-bit ramp (row i == 2^i - 1);
  * the level-99 experience value is the documented 11111 outlier;
  * every CHAR_LEVEL_MOD index resolves to a real enumerator.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_level_up.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import sys

import common
import parse_battle_main_tables as pbmt
import parse_const_enums as pce
from common import ParseError

LEVEL_ROWS = 98          # levels 2..99
FIRST_LEVEL = 2
MAX_LEVEL = 99
LEVEL_99_EXP = 11111     # the documented outlier at the top of the ramp
ABILITY_SLOTS = 8        # 8 swdtechs / 8 blitzes
LEARN_FLAG_ROWS = 9      # 0..8 abilities learned
LEVEL_MOD_ROWS = 4
LEVEL_MOD_STRIDE = 4     # CHAR_LEVEL_MOD values step by 4 (the field is >> 2)


def read_lang_forked_byte_run(path, lines, label, count):
    """The two branches of a `.if LANG_EN / .else / .endif` .byte run.

    Returns (english_values, japanese_values). Anything other than that exact
    three-part shape is a hard error — the fork is the contract."""
    start = pbmt.find_label(path, lines, label)
    i = start
    while i < len(lines):
        code, _ = common.strip_comment(lines[i])
        if code.strip():
            break
        i += 1
    opening = common.strip_comment(lines[i])[0].strip() if i < len(lines) else ""
    if opening != ".if LANG_EN":
        raise ParseError(path, i + 1,
                         "{}: expected '.if LANG_EN', found {!r}"
                         .format(label, opening))

    branches = []
    values = []
    j = i + 1
    while j < len(lines):
        code, _ = common.strip_comment(lines[j])
        s = code.strip()
        lineno = j + 1
        j += 1
        if not s:
            continue
        if s == ".else":
            branches.append(values)
            values = []
            continue
        if s == ".endif":
            branches.append(values)
            break
        if not s.startswith(".byte"):
            raise ParseError(path, lineno,
                             "{}: unexpected line {!r} inside the language fork"
                             .format(label, s))
        for tok in s[len(".byte"):].split(","):
            tok = tok.strip()
            if tok:
                values.append(pbmt.byte_value(path, lineno, label, tok))
    else:
        raise ParseError(path, start, "{}: unterminated language fork".format(label))

    if len(branches) != 2:
        raise ParseError(path, start,
                         "{}: expected 2 language branches, found {}"
                         .format(label, len(branches)))
    for name, branch in zip(("LANG_EN", "else"), branches):
        if len(branch) != count:
            raise ParseError(path, start,
                             "{}: {} branch has {} values, expected {}"
                             .format(label, name, len(branch), count))
    return branches[0], branches[1]


def read_ability_names(paths, parsed, command, count):
    """The ATTACK names of a command's `count` abilities, in teaching order.

    An attack-carrying command's abilities are numbered up from a base attack id
    (CmdWithAttackTbl/CmdAttackOffsetTbl, battle_main.asm:12790), so swdtech N is
    the Nth attack from BUSHIDO's base and blitz N the Nth from BLITZ's. Derived
    from those tables rather than assumed, so a corpus change hard-errors."""
    attack_by_value = pbmt.read_value_names(parsed, "ATTACK",
                                            paths["const_inc"])
    cmd_by_value = pbmt.read_value_names(parsed, "BATTLE_CMD",
                                         paths["const_inc"])
    asm = paths["battle_main_asm"]
    with open(asm, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    commands = pbmt.byte_run_ext(asm, lines, "CmdWithAttackTbl",
                                 pbmt.THROW_TOOLS_LEN)
    bases = pbmt.byte_run_ext(asm, lines, "CmdAttackOffsetTbl",
                              pbmt.THROW_TOOLS_LEN)

    base = None
    for value, attack_base in zip(commands, bases):
        if cmd_by_value.get(value) == command:
            base = attack_base
            break
    if base is None:
        raise ParseError(asm, 0,
                         "CmdWithAttackTbl has no {} row (cannot name its "
                         "abilities)".format(command))

    names = []
    for i in range(count):
        attack = base + i
        if attack not in attack_by_value:
            raise ParseError(paths["const_inc"], 0,
                             "{} ability {} (attack ${:02X}) has no ATTACK "
                             "enumerator".format(command, i, attack))
        names.append(attack_by_value[attack])
    return names


def read_tables(paths):
    parsed = common.parse_ca65_constants(paths["const_inc"],
                                         skip_body_enums=pce.SKIP)
    mod_by_value = pbmt.read_value_names(parsed, "CHAR_LEVEL_MOD",
                                         paths["const_inc"])
    bushido_names = read_ability_names(paths, parsed, "BUSHIDO", ABILITY_SLOTS)
    blitz_names = read_ability_names(paths, parsed, "BLITZ", ABILITY_SLOTS)

    asm = paths["event_asm"]
    with open(asm, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    level_mod = pbmt.byte_run_ext(asm, lines, "CharLevelModTbl", LEVEL_MOD_ROWS)
    learn_flags = pbmt.byte_run_ext(asm, lines, "LearnAbilityTbl",
                                    LEARN_FLAG_ROWS)
    bushido = pbmt.byte_run_ext(asm, lines, "BushidoLevelTbl", ABILITY_SLOTS)
    blitz = pbmt.byte_run_ext(asm, lines, "BlitzLevelTbl", ABILITY_SLOTS)
    exp = pbmt.word_run(asm, lines, "LevelUpExp", LEVEL_ROWS)
    hp = pbmt.byte_run_ext(asm, lines, "LevelUpHP", LEVEL_ROWS)
    mp_en, mp_jp = read_lang_forked_byte_run(asm, lines, "LevelUpMP", LEVEL_ROWS)

    # LearnAbilityTbl is the cumulative low-bit ramp $00,$01,$03,...,$ff.
    for i, value in enumerate(learn_flags):
        if value != (1 << i) - 1:
            raise ParseError(asm, 0,
                             "LearnAbilityTbl row {} is ${:02X}, expected the "
                             "cumulative ${:02X}".format(i, value, (1 << i) - 1))
    if exp[-1] != LEVEL_99_EXP:
        raise ParseError(asm, 0,
                         "LevelUpExp level {} is {}, expected the documented "
                         "{} outlier".format(MAX_LEVEL, exp[-1], LEVEL_99_EXP))

    mod_names = []
    for i in range(LEVEL_MOD_ROWS):
        value = i * LEVEL_MOD_STRIDE
        if value not in mod_by_value:
            raise ParseError(paths["const_inc"], 0,
                             "no CHAR_LEVEL_MOD name for value ${:02X}"
                             .format(value))
        mod_names.append(mod_by_value[value])

    return {
        "level_mod": level_mod,
        "level_mod_signed": [pbmt.to_signed8(v) for v in level_mod],
        "mod_names": mod_names,
        "learn_flags": learn_flags,
        "bushido": bushido,
        "blitz": blitz,
        "bushido_names": bushido_names,
        "blitz_names": blitz_names,
        "exp": exp,
        "hp": hp,
        "mp_en": mp_en,
        "mp_jp": mp_jp,
    }


# --- rendering ---------------------------------------------------------------

_BANNER = ("// AUTO-GENERATED by tools/asm_parser/parse_level_up.py — "
           "DO NOT EDIT.\n")
_REGEN = ("//   python3 tools/asm_parser/parse_level_up.py \\\n"
          "//       --source-root  original-src\n")


def _level_rows(entry, field, values):
    return ",\n".join(
        "    {}{{ .level = {}, .{} = {} }}".format(entry, FIRST_LEVEL + i,
                                                   field, v)
        for i, v in enumerate(values))


def render_inc(t):
    out = [
        _BANNER,
        "// Source: original-src/src/field/event.asm (level progression)\n"
        "// (original-src pinned at 1ea47b5)\n"
        "// DO NOT EDIT BY HAND — regenerate via:\n"
        "{}\n".format(_REGEN),
        "// The character level-progression tables. Each progression row is\n"
        "// keyed by the level it applies to, so a row reads as \"at level N,\n"
        "// gain this much\". #included at namespace scope in\n"
        "// src/data/level_up.h.\n\n",
    ]

    out.append(
        "// CharLevelModTbl (event.asm:1224): the level offset a joining\n"
        "// character gets relative to the party average.\n"
        "inline constexpr std::array<CharacterLevelModifierEntry, {}> "
        "kCharacterLevelModifiers = {{{{\n{}\n}}}};\n\n".format(
            LEVEL_MOD_ROWS,
            ",\n".join("    {{ LevelMod::{}, {} }}".format(n, v)
                       for n, v in zip(t["mod_names"],
                                       t["level_mod_signed"]))))

    out.append(
        "// LearnAbilityTbl (event.asm:1228): the learned-ability bits for a\n"
        "// character who has reached the level of N abilities — a cumulative\n"
        "// low-bit ramp, so row N has the low N bits set.\n"
        "inline constexpr std::array<LearnedAbilityFlagsEntry, {}> "
        "kLearnedAbilityFlags = {{{{\n{}\n}}}};\n\n".format(
            LEARN_FLAG_ROWS,
            ",\n".join("    LearnedAbilityFlagsEntry{{ .learnedCount = {}, "
                       ".abilities = AbilityLearnedSet{{0x{:02X}}} }}"
                       .format(i, v)
                       for i, v in enumerate(t["learn_flags"]))))

    for key, label, line, addr, what in (
            ("bushido", "kBushidoLearnLevels", 1235, "e6/f490", "swdtech"),
            ("blitz", "kBlitzLearnLevels", 1239, "e6/f498", "blitz")):
        out.append(
            "// {}Tbl (event.asm:{}, {}): the level each {} is\n"
            "// learned at, in the order they are taught.\n"
            "inline constexpr std::array<AbilityLearnLevelEntry, {}> {} = "
            "{{{{\n{}\n}}}};\n\n".format(
                label[1:].replace("LearnLevels", "Level"), line, addr, what,
                ABILITY_SLOTS, label,
                ",\n".join("    {{ AttackId::{}, {} }}".format(n, v)
                           for n, v in zip(t[key + "_names"], t[key]))))

    out.append(
        "// LevelUpExp (event.asm:1329, segment \"level_up_exp\"): the experience\n"
        "// step from the previous level to this one. (The consumer sums the\n"
        "// steps below a level and multiplies by 8.) The level-99 value is an\n"
        "// upstream outlier against the rest of the ramp.\n"
        "inline constexpr std::array<LevelUpExpEntry, {}> kLevelUpExp = "
        "{{{{\n{}\n}}}};\n\n".format(
            LEVEL_ROWS, _level_rows("LevelUpExpEntry", "exp", t["exp"])))

    out.append(
        "// LevelUpHP (event.asm:1385, e6/f4a0): the max-HP gained on reaching\n"
        "// each level.\n"
        "inline constexpr std::array<LevelUpStatEntry, {}> kLevelUpHp = "
        "{{{{\n{}\n}}}};\n\n".format(
            LEVEL_ROWS, _level_rows("LevelUpStatEntry", "gain", t["hp"])))

    out.append(
        "// LevelUpMP (event.asm:1441, e6/f502): the max-MP gained on reaching\n"
        "// each level. The Japanese release uses a different curve, so the\n"
        "// table is selected by game version.\n"
        "inline constexpr std::array<LevelUpStatEntry, {}> kLevelUpMpEn = "
        "{{{{\n{}\n}}}};\n\n".format(
            LEVEL_ROWS, _level_rows("LevelUpStatEntry", "gain", t["mp_en"])))
    out.append(
        "inline constexpr std::array<LevelUpStatEntry, {}> kLevelUpMpJp = "
        "{{{{\n{}\n}}}};\n".format(
            LEVEL_ROWS, _level_rows("LevelUpStatEntry", "gain", t["mp_jp"])))

    return "".join(out)


def render_fixture(t):
    out = [
        _BANNER,
        "// (original-src pinned at 1ea47b5) — DO NOT EDIT BY HAND.\n"
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace ostinato::test {\n\n"
        "// Ground-truth raw ROM bytes for the level-progression tables.\n"
        "// Byte-identity tests compare each generated table against these.\n\n",
    ]
    out.append(pbmt._fixture_array("kExpectedCharLevelMod", t["level_mod"]))
    out.append(pbmt._fixture_array("kExpectedLearnAbility", t["learn_flags"]))
    out.append(pbmt._fixture_array("kExpectedBushidoLevel", t["bushido"]))
    out.append(pbmt._fixture_array("kExpectedBlitzLevel", t["blitz"]))
    out.append(pbmt._fixture_words("kExpectedLevelUpExp", t["exp"]))
    out.append(pbmt._fixture_array("kExpectedLevelUpHP", t["hp"]))
    out.append(pbmt._fixture_array("kExpectedLevelUpMPEn", t["mp_en"]))
    out.append(pbmt._fixture_array("kExpectedLevelUpMPJp", t["mp_jp"]))
    out.append("\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def run(paths, outs, check_only=False):
    t = read_tables(paths)
    if check_only:
        print("OK: level progression {} rows (exp/hp/mp-en/mp-jp), {} ability "
              "slots x2, {} learn-flag rows, {} level modifiers; all resolve."
              .format(LEVEL_ROWS, ABILITY_SLOTS, LEARN_FLAG_ROWS,
                      LEVEL_MOD_ROWS))
        return 0
    _write(outs["inc"], render_inc(t))
    _write(outs["fixture"], render_fixture(t))
    print("Emitted level-progression tables -> {}".format(outs["inc"]))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (event.asm, const.inc resolve under "
                         "it)")
    ap.add_argument("--repo-root", default=".", help="repo root for outputs")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    root = args.source_root or "original-src"
    repo = args.repo_root
    paths = {
        "event_asm": os.path.join(root, "src", "field", "event.asm"),
        "battle_main_asm": os.path.join(root, "src", "battle",
                                        "battle_main.asm"),
        "const_inc": os.path.join(root, "include", "const.inc"),
    }
    outs = {
        "inc": os.path.join(repo, "src", "data", "generated",
                            "level_up_data.inc"),
        "fixture": os.path.join(repo, "tests", "fixtures",
                                "level_up_expected.h"),
    }
    try:
        return run(paths, outs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
