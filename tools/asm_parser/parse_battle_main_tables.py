#!/usr/bin/env python3
"""Emit the battle-command satellite tables inline in battle_main.asm.

Port-time tooling (NOT a build/CI dependency). Alongside BattleCmdProp
(parse_battle_cmd_prop.py), the battle engine carries a family of small tables
keyed by command id. This parser reverse-derives the twelve command-domain
tables and emits them as one bundle of named constexpr definitions:

  Command-membership masks (four bytes, GetBitPtr bit order — BattleCommandSet):
    * ConfusedCmdTbl (:754, c2/04d0)  — allowed while muddled/charmed/colosseum
    * BerserkCmdTbl  (:759, c2/04d4)  — allowed while berserk/zombie
    * RetargetCmdTbl (:12798, c2/4e46)— commands that re-pick their target

  Command-id lists (one BATTLE_CMD value per slot; the value names itself):
    * RandCmdIDTbl   (:763, c2/04d8)  — special random-use handlers (10)
    * UpdateCmdIDTbl (:13621, c2/52e9)— need an enabled-state update handler (8)
    * InitCmdIDTbl   (:13904, c2/5468)— have an init function (6)

  Per-command mappings (BattleCommandId -> value; the 30 real commands):
    * CmdDelayTbl    (:1060, c2/067b) — ATB advance-wait ticks (decimal)
    * CmdTargetTbl   (:6594, c2/278a) — packed targeting-init byte

  Positionally-paired lists (rendered as pair-struct mappings):
    * RelicCmdTbl1/2 (:13879/:13885)  — a relic swap from -> to (5)
    * CmdWithAttackTbl/CmdAttackOffsetTbl (:12790/:12794) — command -> base
      ATTACK id (5)

Structural guarantees, hard-errored at emit time:
  * each table label is found and its byte run is exactly the expected length;
  * every command-mask bit that is set names a real BATTLE_CMD enumerator, and
    re-encoding the decoded member list reproduces the ROM bytes exactly;
  * every command-id byte and every attack-offset byte resolves to a BATTLE_CMD
    / ATTACK enumerator;
  * CmdDelayTbl rows $1E/$1F are the expected unused ($00) padding.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_battle_main_tables.py --source-root PATH --repo-root PATH
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

REAL_CMDS = 30       # named commands $00 (FIGHT) .. $1D (MAGITEK)
CMD_DELAY_ROWS = 32  # CmdDelayTbl spans 32 ROM bytes (rows $1E/$1F unreachable)

_LOCAL_LABEL_RE = re.compile(r"^@[0-9A-Fa-f]+:\s*")


def read_value_names(parsed, enum_name, path):
    """value -> enumerator name for an enum, first declaration wins (so aliases
    never shadow the canonical name)."""
    enum = parsed.enum(enum_name)
    if enum is None:
        raise ParseError(path, 0, "expected enum '{}' not found".format(enum_name))
    by_value = {}
    for m in enum.members:
        by_value.setdefault(m.value, m.name)
    return by_value


def byte_run(path, lines, label, count):
    """The `count` .byte values following `label:`.

    Skips blank/comment-only lines and the ca65 `@addr:` local-label prefix on
    each data line; stops at the first line that is not a `.byte` directive.
    Hard-errors unless exactly `count` values are gathered."""
    start = None
    for i, raw in enumerate(lines):
        code, _ = common.strip_comment(raw)
        if code.strip() == label + ":":
            start = i + 1
            break
    if start is None:
        raise ParseError(path, 0, "table label '{}:' not found".format(label))

    values = []
    j = start
    while j < len(lines):
        code, _ = common.strip_comment(lines[j])
        s = code.strip()
        j += 1
        if not s:
            continue
        s = _LOCAL_LABEL_RE.sub("", s).strip()
        if not s.startswith(".byte"):
            break
        for tok in s[len(".byte"):].split(","):
            tok = tok.strip()
            if not tok:
                continue
            val = common.parse_int_literal(tok)
            if val is None:
                raise ParseError(path, j,
                                 "{}: {!r} is not a byte literal"
                                 .format(label, tok))
            if val > 0xFF:
                raise ParseError(path, j,
                                 "{}: {!r} exceeds one byte".format(label, tok))
            values.append(val)
    if len(values) != count:
        raise ParseError(path, start,
                         "{}: expected {} bytes, gathered {}"
                         .format(label, count, len(values)))
    return values


def decode_command_set(path, label, raw, cmd_names):
    """Decode a four-byte command mask into its member BATTLE_CMD names (GetBitPtr
    order: command n -> byte n/8, bit n%8), then re-encode and assert the bytes
    round-trip exactly."""
    members = []
    for n in range(32):
        if raw[n >> 3] & (1 << (n & 0x07)):
            name = cmd_names.get(n)
            if name is None:
                raise ParseError(path, 0,
                                 "{}: bit {} has no BATTLE_CMD enumerator "
                                 "(escalate)".format(label, n))
            members.append((n, name))
    check = [0, 0, 0, 0]
    for n, _name in members:
        check[n >> 3] |= 1 << (n & 0x07)
    if check != list(raw):
        raise ParseError(path, 0,
                         "{}: re-encoded mask {} != ROM bytes {}"
                         .format(label, check, list(raw)))
    return [name for _n, name in members]


def names_for(path, label, raw, value_names, kind):
    """Map each byte of `raw` to its enumerator name in `value_names`."""
    out = []
    for i, b in enumerate(raw):
        name = value_names.get(b)
        if name is None:
            raise ParseError(path, 0,
                             "{}: byte {} value ${:02X} has no {} enumerator"
                             .format(label, i, b, kind))
        out.append(name)
    return out


# --- rendering ---------------------------------------------------------------

_BANNER = ("// AUTO-GENERATED by tools/asm_parser/parse_battle_main_tables.py — "
           "DO NOT EDIT.\n")
_REGEN = ("//   python3 tools/asm_parser/parse_battle_main_tables.py \\\n"
          "//       --source-root  original-src\n")


def _cmd_list(names):
    return ", ".join("BattleCommandId::" + n for n in names)


def _enum_array(names, enum, indent="    "):
    body = ",\n".join(
        "{}    {}::{}".format(indent, enum, n) for n in names)
    return "{{{{\n{}\n{}}}}}".format(body, indent)


def render_inc(t):
    out = [
        _BANNER,
        "// Source: original-src/src/battle/battle_main.asm (command-domain "
        "tables)\n"
        "// (original-src pinned at 1ea47b5)\n"
        "// DO NOT EDIT BY HAND — regenerate via:\n"
        "{}\n".format(_REGEN),
        "// The command-domain satellite tables of the battle engine, one bundle\n"
        "// of named constexpr definitions #included at namespace scope in\n"
        "// src/data/battle_commands.h. Every command / attack byte names its\n"
        "// enumerator; the three masks list their member commands and are\n"
        "// re-encoded by the parser to seal byte identity against the ROM.\n\n",
    ]

    # --- command-membership masks ---
    out.append(
        "// ConfusedCmdTbl (battle_main.asm:754, c2/04d0): commands allowed while\n"
        "// muddled, charmed, or fighting in the colosseum.\n"
        "inline constexpr BattleCommandSet kConfusedAllowedCommands =\n"
        "    BattleCommandSet::of({});\n\n".format(_cmd_list(t["confused"])))
    out.append(
        "// BerserkCmdTbl (battle_main.asm:759, c2/04d4): commands allowed while\n"
        "// berserk or zombie.\n"
        "inline constexpr BattleCommandSet kBerserkAllowedCommands =\n"
        "    BattleCommandSet::of({});\n\n".format(_cmd_list(t["berserk"])))
    out.append(
        "// RetargetCmdTbl (battle_main.asm:12798, c2/4e46): commands that\n"
        "// re-pick their target after selection.\n"
        "inline constexpr BattleCommandSet kRetargetCommands =\n"
        "    BattleCommandSet::of({});\n\n".format(_cmd_list(t["retarget"])))

    # --- command-id lists (value self-labels) ---
    out.append(
        "// RandCmdIDTbl (battle_main.asm:763, c2/04d8): commands with a special\n"
        "// handler when used randomly by Gogo/Mimic (the consumer at\n"
        "// battle_main.asm:710 walks the list in interleaved even/odd pairs).\n"
        "inline constexpr std::array<BattleCommandId, 10> kRandomHandlerCommands "
        "= {};\n\n".format(_enum_array(t["rand_id_names"], "BattleCommandId")))
    out.append(
        "// UpdateCmdIDTbl (battle_main.asm:13621, c2/52e9): commands whose\n"
        "// enabled state needs an update handler.\n"
        "inline constexpr std::array<BattleCommandId, 8> kUpdateStateCommands = "
        "{};\n\n".format(_enum_array(t["update_names"], "BattleCommandId")))
    out.append(
        "// InitCmdIDTbl (battle_main.asm:13904, c2/5468): commands with an init\n"
        "// function run when the command is set up.\n"
        "inline constexpr std::array<BattleCommandId, 6> kInitFunctionCommands = "
        "{};\n\n".format(_enum_array(t["init_names"], "BattleCommandId")))

    # --- CmdDelayTbl: command -> advance-wait ticks (decimal) ---
    delay_rows = ",\n".join(
        "    {{ BattleCommandId::{}, {} }}".format(t["cmd_names"][i], t["delay"][i])
        for i in range(REAL_CMDS))
    out.append(
        "// CmdDelayTbl (battle_main.asm:1060, c2/067b): the ATB advance-wait (in\n"
        "// ticks) each command adds before its action runs. (ROM rows $1E/$1F\n"
        "// are unreachable behind the `cmp #$1e` guard — verified in the\n"
        "// fixture, not modeled.)\n"
        "inline constexpr std::array<CommandAdvanceWaitEntry, 30> "
        "kCommandAdvanceWait = {{{{\n{}\n}}}};\n\n".format(delay_rows))

    # --- CmdTargetTbl: command -> packed targeting-init byte ---
    tgt_rows = ",\n".join(
        "    {{ BattleCommandId::{}, CommandTargetingInit{{0x{:02X}}} }}"
        .format(t["cmd_names"][i], t["target"][i])
        for i in range(REAL_CMDS))
    out.append(
        "// CmdTargetTbl (battle_main.asm:6594, c2/278a): the packed\n"
        "// targeting-init byte for each command (see CommandTargetingInit for\n"
        "// the field split at InitTarget).\n"
        "inline constexpr std::array<CommandTargetingInitEntry, 30> "
        "kCommandTargetingInit = {{{{\n{}\n}}}};\n\n".format(tgt_rows))

    # --- RelicCmdTbl1/2: from -> to ---
    relic_rows = ",\n".join(
        "    {{ BattleCommandId::{}, BattleCommandId::{} }}".format(a, b)
        for a, b in zip(t["relic1_names"], t["relic2_names"]))
    out.append(
        "// RelicCmdTbl1/2 (battle_main.asm:13879/13885, c2/5452): a relic that\n"
        "// replaces `from` with `to` (steal->capture, slot->gp rain,\n"
        "// sketch->control, magic->x-magic, fight->jump).\n"
        "inline constexpr std::array<RelicCommandSwap, 5> kRelicCommandSwaps = "
        "{{{{\n{}\n}}}};\n\n".format(relic_rows))

    # --- CmdWithAttackTbl/CmdAttackOffsetTbl: command -> base attack ---
    atk_rows = ",\n".join(
        "    {{ BattleCommandId::{}, AttackId::{} }}".format(c, a)
        for c, a in zip(t["cmd_with_attack_names"], t["attack_offset_names"]))
    out.append(
        "// CmdWithAttackTbl/CmdAttackOffsetTbl (battle_main.asm:12790/12794,\n"
        "// c2/4e3c): the base ATTACK id each attack-carrying command counts up\n"
        "// from (summon/lore/magitek/blitz/swdtech).\n"
        "inline constexpr std::array<CommandAttackBase, 5> kCommandAttackBases = "
        "{{{{\n{}\n}}}};\n".format(atk_rows))

    return "".join(out)


def _fixture_array(name, values):
    body = ", ".join("0x{:02X}".format(v) for v in values)
    return ("inline constexpr std::array<std::uint8_t, {}> {} = {{{{ {} }}}};\n"
            .format(len(values), name, body))


def render_fixture(t):
    out = [
        _BANNER,
        "// (original-src pinned at 1ea47b5) — DO NOT EDIT BY HAND.\n"
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace ostinato::test {\n\n"
        "// Ground-truth raw ROM bytes for the command-domain satellite tables.\n"
        "// Byte-identity tests compare each generated table against these.\n\n",
    ]
    out.append(_fixture_array("kExpectedConfusedCmd", t["confused_raw"]))
    out.append(_fixture_array("kExpectedBerserkCmd", t["berserk_raw"]))
    out.append(_fixture_array("kExpectedRetargetCmd", t["retarget_raw"]))
    out.append(_fixture_array("kExpectedRandCmdId", t["rand_id_raw"]))
    out.append(_fixture_array("kExpectedCmdDelay", t["delay_raw"]))
    out.append(_fixture_array("kExpectedCmdTarget", t["target"]))
    out.append(_fixture_array("kExpectedUpdateCmdId", t["update_raw"]))
    out.append(_fixture_array("kExpectedInitCmdId", t["init_raw"]))
    out.append(_fixture_array("kExpectedRelicCmd1", t["relic1_raw"]))
    out.append(_fixture_array("kExpectedRelicCmd2", t["relic2_raw"]))
    out.append(_fixture_array("kExpectedCmdWithAttack", t["cmd_with_attack_raw"]))
    out.append(_fixture_array("kExpectedCmdAttackOffset", t["attack_offset_raw"]))
    out.append("\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def read_tables(paths):
    parsed = common.parse_ca65_constants(paths["const_inc"],
                                         skip_body_enums=pce.SKIP)
    cmd_names = read_value_names(parsed, "BATTLE_CMD", paths["const_inc"])
    attack_names = read_value_names(parsed, "ATTACK", paths["const_inc"])

    asm = paths["battle_main_asm"]
    with open(asm, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    confused_raw = byte_run(asm, lines, "ConfusedCmdTbl", 4)
    berserk_raw = byte_run(asm, lines, "BerserkCmdTbl", 4)
    retarget_raw = byte_run(asm, lines, "RetargetCmdTbl", 4)
    rand_id_raw = byte_run(asm, lines, "RandCmdIDTbl", 10)
    delay_raw = byte_run(asm, lines, "CmdDelayTbl", CMD_DELAY_ROWS)
    target = byte_run(asm, lines, "CmdTargetTbl", REAL_CMDS)
    update_raw = byte_run(asm, lines, "UpdateCmdIDTbl", 8)
    init_raw = byte_run(asm, lines, "InitCmdIDTbl", 6)
    relic1_raw = byte_run(asm, lines, "RelicCmdTbl1", 5)
    relic2_raw = byte_run(asm, lines, "RelicCmdTbl2", 5)
    cmd_with_attack_raw = byte_run(asm, lines, "CmdWithAttackTbl", 5)
    attack_offset_raw = byte_run(asm, lines, "CmdAttackOffsetTbl", 5)

    # CmdDelayTbl rows $1E/$1F are unreachable padding; expect $00.
    for pad in (0x1E, 0x1F):
        if delay_raw[pad] != 0x00:
            raise ParseError(asm, 0,
                             "CmdDelayTbl row ${:02X} is ${:02X}, expected the "
                             "unused $00 padding".format(pad, delay_raw[pad]))

    # The command name for each of the 30 real command slots (index 0..29).
    for i in range(REAL_CMDS):
        if cmd_names.get(i) is None:
            raise ParseError(paths["const_inc"], 0,
                             "no BATTLE_CMD name for command slot {}".format(i))

    return {
        "cmd_names": [cmd_names[i] for i in range(REAL_CMDS)],
        "confused_raw": confused_raw,
        "berserk_raw": berserk_raw,
        "retarget_raw": retarget_raw,
        "confused": decode_command_set(asm, "ConfusedCmdTbl", confused_raw,
                                       cmd_names),
        "berserk": decode_command_set(asm, "BerserkCmdTbl", berserk_raw,
                                      cmd_names),
        "retarget": decode_command_set(asm, "RetargetCmdTbl", retarget_raw,
                                       cmd_names),
        "rand_id_raw": rand_id_raw,
        "rand_id_names": names_for(asm, "RandCmdIDTbl", rand_id_raw, cmd_names,
                                   "BATTLE_CMD"),
        "delay_raw": delay_raw,
        "delay": delay_raw[:REAL_CMDS],
        "target": target,
        "update_raw": update_raw,
        "update_names": names_for(asm, "UpdateCmdIDTbl", update_raw, cmd_names,
                                  "BATTLE_CMD"),
        "init_raw": init_raw,
        "init_names": names_for(asm, "InitCmdIDTbl", init_raw, cmd_names,
                                "BATTLE_CMD"),
        "relic1_raw": relic1_raw,
        "relic2_raw": relic2_raw,
        "relic1_names": names_for(asm, "RelicCmdTbl1", relic1_raw, cmd_names,
                                  "BATTLE_CMD"),
        "relic2_names": names_for(asm, "RelicCmdTbl2", relic2_raw, cmd_names,
                                  "BATTLE_CMD"),
        "cmd_with_attack_raw": cmd_with_attack_raw,
        "cmd_with_attack_names": names_for(asm, "CmdWithAttackTbl",
                                           cmd_with_attack_raw, cmd_names,
                                           "BATTLE_CMD"),
        "attack_offset_raw": attack_offset_raw,
        "attack_offset_names": names_for(asm, "CmdAttackOffsetTbl",
                                         attack_offset_raw, attack_names,
                                         "ATTACK"),
    }


def run(paths, outs, check_only=False):
    t = read_tables(paths)
    if check_only:
        print("OK: 3 command masks (round-trip verified), 3 id lists, 2 "
              "per-command mappings ({} rows), 2 pair tables; all bytes resolve."
              .format(REAL_CMDS))
        return 0
    _write(outs["inc"], render_inc(t))
    _write(outs["fixture"], render_fixture(t))
    print("Emitted command-domain satellite tables -> {}".format(outs["inc"]))
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
                    help="disassembly root (battle_main.asm, const.inc resolve "
                         "under it)")
    ap.add_argument("--repo-root", default=".", help="repo root for outputs")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    root = args.source_root or "original-src"
    repo = args.repo_root
    paths = {
        "battle_main_asm": os.path.join(root, "src", "battle",
                                        "battle_main.asm"),
        "const_inc": os.path.join(root, "include", "const.inc"),
    }
    outs = {
        "inc": os.path.join(repo, "src", "data", "generated",
                            "battle_cmd_tables_data.inc"),
        "fixture": os.path.join(repo, "tests", "fixtures",
                                "battle_cmd_tables_expected.h"),
    }
    try:
        return run(paths, outs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
