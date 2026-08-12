#!/usr/bin/env python3
"""Emit the battle-command properties table from original-src.

Port-time tooling (NOT a build/CI dependency). BattleCmdProp
(battle/battle_cmd_prop.asm, ROM cf/fe00) is a 32-row ROM table, two bytes per
command:

  * byte 0 — battle-command flags (BATTLE_CMD_FLAG: GOGO / MIMIC / IMP /
    UNKNOWN), the "who can use this command" mask read at the mimic/imp checks;
  * byte 1 — target-selection flags (TARGET), the default targeting mode.

The 30 real commands are $00 (FIGHT) .. $1D (MAGITEK); rows $1E/$1F are unused
NONE/MENU padding with no BATTLE_CMD name. The emitted typed surface is a simple
mapping from BattleCommandId to its properties (30 named entries); the fixture
carries all 32 raw ROM rows so the two pad bytes are still verified, not modeled
with a fake identity.

Each row is written with the `make_battle_cmd_prop flags, target` macro, whose
arguments are either a single namespace member or a `{A, B, C}` brace list. This
parser resolves every member against the BATTLE_CMD_FLAG / TARGET enums in
include/const.inc, ORs each list into its ROM byte, and emits:

  * src/data/generated/battle_cmd_prop_data.inc — the { BattleCommandId, record }
    mapping rows (flags/targeting built from named enumerators, token-for-token
    with the upstream macro args — no raw flag bytes);
  * tests/fixtures/battle_cmd_prop_expected.h — all 32 raw two-byte ROM rows.

Structural guarantees, hard-errored at emit time:
  * exactly 32 ROM rows (wrong count = wrong artifact);
  * the documenting comment index ($00.. $1f) advances by one, matching position;
  * rows $1E/$1F are the expected NONE/MENU padding ($00,$FF);
  * every flags-arg member is a real BATTLE_CMD_FLAG enumerator;
  * every target-arg member is a real TARGET enumerator.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_battle_cmd_prop.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_const_enums as pce
from common import ParseError

CMD_ROWS = 32       # ROM rows (incl. the two pad slots)
REAL_CMDS = 30      # named commands $00 (FIGHT) .. $1D (MAGITEK)
PAD_FLAGS = 0x00    # rows $1E/$1F: make_battle_cmd_prop NONE, MENU
PAD_TARGET = 0xFF

_MACRO = "make_battle_cmd_prop"
# A row's documenting comment: "; $NN: name". Used only to check the running
# index; the bytes come from the macro args and the identity comes from
# BATTLE_CMD, never the comment.
_ROW_COMMENT_RE = re.compile(r"^\$([0-9a-fA-F]{2}):\s*(.*)$")


def read_enum(parsed, name, path):
    enum = parsed.enum(name)
    if enum is None:
        raise ParseError(path, 0, "expected enum '{}' not found".format(name))
    return enum


def _split_args(path, lineno, rest):
    """Split `flags, target` into two argument strings, respecting brace lists."""
    depth = 0
    for i, ch in enumerate(rest):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        elif ch == "," and depth == 0:
            return rest[:i].strip(), rest[i + 1:].strip()
    raise ParseError(path, lineno,
                     "{}: expected two arguments, got {!r}".format(_MACRO, rest))


def _members(path, lineno, arg, namespace, values):
    """Resolve a macro argument (single member or `{A, B, ...}`) to its member
    tokens, validating each against `values` (the namespace's enum)."""
    inner = arg
    if inner.startswith("{"):
        if not inner.endswith("}"):
            raise ParseError(path, lineno,
                             "{}: unbalanced brace list {!r}".format(_MACRO, arg))
        inner = inner[1:-1]
    tokens = [t.strip() for t in inner.split(",") if t.strip()]
    if not tokens:
        raise ParseError(path, lineno,
                         "{}: empty {} argument".format(_MACRO, namespace))
    for tok in tokens:
        if tok not in values:
            raise ParseError(path, lineno,
                             "{}: {!r} is not a {} enumerator"
                             .format(_MACRO, tok, namespace))
    return tokens


def read_rows(path, flag_values, target_values):
    """Parse the 32 make_battle_cmd_prop rows into
    (index, flag_tokens, target_tokens, byte0, byte1) tuples."""
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    rows = []
    pending_index = None
    for idx, raw in enumerate(lines):
        code, comment = common.strip_comment(raw)
        if comment is not None:
            m = _ROW_COMMENT_RE.match(comment)
            if m:
                pending_index = int(m.group(1), 16)
        s = code.strip()
        if not s.startswith(_MACRO):
            continue
        rest = s[len(_MACRO):].strip()
        flags_arg, target_arg = _split_args(path, idx + 1, rest)
        flag_tokens = _members(path, idx + 1, flags_arg, "BATTLE_CMD_FLAG",
                               flag_values)
        target_tokens = _members(path, idx + 1, target_arg, "TARGET",
                                 target_values)
        byte0 = 0
        for tok in flag_tokens:
            byte0 |= flag_values[tok]
        byte1 = 0
        for tok in target_tokens:
            byte1 |= target_values[tok]
        if byte0 > 0xFF or byte1 > 0xFF:
            raise ParseError(path, idx + 1,
                             "row bytes out of range (${:X},${:X})"
                             .format(byte0, byte1))
        expected_index = len(rows)
        if pending_index is None or pending_index != expected_index:
            raise ParseError(path, idx + 1,
                             "row {}: documenting index {} disagrees with "
                             "position".format(expected_index, pending_index))
        rows.append((expected_index, flag_tokens, target_tokens, byte0, byte1))
        pending_index = None

    if len(rows) != CMD_ROWS:
        raise ParseError(path, 0,
                         "expected {} rows, found {}".format(CMD_ROWS, len(rows)))
    for pad in (0x1E, 0x1F):
        _, _, _, b0, b1 = rows[pad]
        if (b0, b1) != (PAD_FLAGS, PAD_TARGET):
            raise ParseError(path, 0,
                             "row ${:02X} is (${:02X},${:02X}), expected the "
                             "unused padding (${:02X},${:02X})"
                             .format(pad, b0, b1, PAD_FLAGS, PAD_TARGET))
    return rows


# --- rendering ---------------------------------------------------------------

_BANNER = ("// AUTO-GENERATED by tools/asm_parser/parse_battle_cmd_prop.py — "
           "DO NOT EDIT.\n")
_REGEN = ("//   python3 tools/asm_parser/parse_battle_cmd_prop.py \\\n"
          "//       --source-root  original-src\n")


def render_inc(rows, cmd_enum):
    cmd_name = {m.value: m.name for m in cmd_enum.members}
    out = [
        _BANNER,
        "// Source: original-src/src/battle/battle_cmd_prop.asm (BattleCmdProp, "
        "ROM cf/fe00)\n"
        "// (original-src pinned at 1ea47b5)\n"
        "// DO NOT EDIT BY HAND — regenerate via:\n"
        "{}\n".format(_REGEN),
        "// A mapping from each of the 30 real commands to its properties,\n"
        "// #included inside the array in src/data/battle_commands.cpp. flags\n"
        "// and targeting are built from named enumerators, matching the upstream\n"
        "// make_battle_cmd_prop args token-for-token. (ROM rows $1E/$1F are\n"
        "// unused NONE/MENU padding — verified in the fixture, not modeled.)\n\n",
    ]
    for index, flags, targets, _b0, _b1 in rows[:REAL_CMDS]:
        name = cmd_name.get(index)
        if name is None:
            raise ParseError("<battle_cmd_prop>", 0,
                             "no BATTLE_CMD name for command {}".format(index))
        flag_args = ", ".join("BattleCommandFlags::" + t for t in flags)
        target_args = ", ".join("TargetFlags::" + t for t in targets)
        out.append(
            "    {{ BattleCommandId::{}, BattleCommandProperties{{\n"
            "        FlagSet<BattleCommandFlags>::of({}),\n"
            "        Targeting::of({}) }} }},\n"
            .format(name, flag_args, target_args))
    return "".join(out)


def render_fixture(rows):
    out = [
        _BANNER,
        "// (original-src pinned at 1ea47b5) — DO NOT EDIT BY HAND.\n"
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace ostinato::test {\n\n",
        "// Ground-truth BattleCmdProp rows, all 32 ROM slots (.index = raw\n"
        "// position, .bytes = the two ROM bytes: [0] command flags, [1] target\n"
        "// flags). Slots 30/31 ($1E/$1F) are the unused NONE/MENU padding.\n"
        "struct ExpectedBattleCmdProp {\n"
        "    std::uint8_t index;\n"
        "    std::array<std::uint8_t, 2> bytes;\n"
        "};\n\n"
        "inline constexpr std::array<ExpectedBattleCmdProp, "
        + str(CMD_ROWS) + "> kExpectedBattleCmdProp = {{\n",
    ]
    for index, _flags, _targets, b0, b1 in rows:
        out.append("    {{ .index = {:>2}, .bytes = {{ 0x{:02X}, 0x{:02X} }} }},\n"
                   .format(index, b0, b1))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def run(paths, outs, check_only=False):
    parsed = common.parse_ca65_constants(paths["const_inc"],
                                         skip_body_enums=pce.SKIP)
    flag_enum = read_enum(parsed, "BATTLE_CMD_FLAG", paths["const_inc"])
    target_enum = read_enum(parsed, "TARGET", paths["const_inc"])
    cmd_enum = read_enum(parsed, "BATTLE_CMD", paths["const_inc"])
    flag_values = {m.name: m.value for m in flag_enum.members}
    target_values = {m.name: m.value for m in target_enum.members}
    rows = read_rows(paths["cmd_prop_asm"], flag_values, target_values)

    if check_only:
        print("OK: {} ROM rows ({} named commands + 2 pad); all flag/target "
              "members resolve; index order + padding verified."
              .format(len(rows), REAL_CMDS))
        return 0

    _write(outs["inc"], render_inc(rows, cmd_enum))
    _write(outs["fixture"], render_fixture(rows))
    print("Emitted {} named battle-command rows -> {}".format(REAL_CMDS,
                                                              outs["inc"]))
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
                    help="disassembly root (battle_cmd_prop.asm, const.inc "
                         "resolve under it)")
    ap.add_argument("--repo-root", default=".",
                    help="repo root for output paths")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    root = args.source_root or "original-src"
    repo = args.repo_root
    paths = {
        "cmd_prop_asm": os.path.join(root, "src", "battle",
                                     "battle_cmd_prop.asm"),
        "const_inc": os.path.join(root, "include", "const.inc"),
    }
    outs = {
        "inc": os.path.join(repo, "src", "data", "generated",
                            "battle_cmd_prop_data.inc"),
        "fixture": os.path.join(repo, "tests", "fixtures",
                                "battle_cmd_prop_expected.h"),
    }
    try:
        return run(paths, outs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
