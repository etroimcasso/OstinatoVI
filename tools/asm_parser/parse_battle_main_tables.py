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


def read_name_values(parsed, enum_name, path):
    """enumerator name -> value for an enum (the inverse of read_value_names)."""
    enum = parsed.enum(enum_name)
    if enum is None:
        raise ParseError(path, 0, "expected enum '{}' not found".format(enum_name))
    return {m.name: m.value for m in enum.members}


# --- generic run readers (the s2 tables use richer directives than .byte) -----

# ca65 low-byte-of-a-negative-value:  <(-3)
_LOW_BYTE_NEG_RE = re.compile(r"^<\(\s*(-\d+)\s*\)$")
# ca65 low-word-of-a-value:  .loword(-10)
_LOWORD_RE = re.compile(r"^\.loword\(\s*(-?\d+)\s*\)$", re.IGNORECASE)
# A scoped enum reference in a data run:  ATTACK::RIOT_BLADE
_SCOPED_TOK_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)$")


def find_label(path, lines, label):
    """The 0-based index of the line after `label:`."""
    for i, raw in enumerate(lines):
        code, _ = common.strip_comment(raw)
        if code.strip() == label + ":":
            return i + 1
    raise ParseError(path, 0, "table label '{}:' not found".format(label))


def find_segment(path, lines, segment):
    """The 0-based index of the line after `.segment "<segment>"`.

    The desperation-attack run is label-less — the segment directive is the only
    anchor it has."""
    needle = '.segment "{}"'.format(segment)
    for i, raw in enumerate(lines):
        code, _ = common.strip_comment(raw)
        if code.strip() == needle:
            return i + 1
    raise ParseError(path, 0, "segment '{}' not found".format(segment))


def gather_run(path, lines, start, directive):
    """The comma-separated tokens of the consecutive `directive` lines at `start`.

    Skips blank/comment-only lines and the ca65 `@addr:` local-label prefix;
    stops at the first line that is not `directive`. Returns [(token, lineno)]."""
    tokens = []
    j = start
    while j < len(lines):
        code, _ = common.strip_comment(lines[j])
        s = code.strip()
        j += 1
        if not s:
            continue
        s = _LOCAL_LABEL_RE.sub("", s).strip()
        if not s.startswith(directive):
            break
        for tok in s[len(directive):].split(","):
            tok = tok.strip()
            if tok:
                tokens.append((tok, j))
    return tokens


def _assert_count(path, label, tokens, count):
    if len(tokens) != count:
        raise ParseError(path, tokens[0][1] if tokens else 0,
                         "{}: expected {} values, gathered {}"
                         .format(label, count, len(tokens)))


def byte_value(path, lineno, label, tok):
    """One .byte token: an integer literal or ca65 `<(-N)` (low byte of -N)."""
    val = common.parse_int_literal(tok)
    if val is None:
        m = _LOW_BYTE_NEG_RE.match(tok)
        if m is None:
            raise ParseError(path, lineno,
                             "{}: {!r} is not a byte literal".format(label, tok))
        val = int(m.group(1)) & 0xFF
    if val > 0xFF:
        raise ParseError(path, lineno,
                         "{}: {!r} exceeds one byte".format(label, tok))
    return val


def byte_run_ext(path, lines, label, count):
    """A .byte run that may contain ca65 `<(-N)` low-byte terms."""
    tokens = gather_run(path, lines, find_label(path, lines, label), ".byte")
    _assert_count(path, label, tokens, count)
    return [byte_value(path, ln, label, t) for t, ln in tokens]


def word_run(path, lines, label, count):
    """A .word run; accepts integer literals and ca65 `.loword(-N)` terms."""
    tokens = gather_run(path, lines, find_label(path, lines, label), ".word")
    _assert_count(path, label, tokens, count)
    values = []
    for tok, lineno in tokens:
        m = _LOWORD_RE.match(tok)
        if m is not None:
            values.append(int(m.group(1)) & 0xFFFF)
            continue
        val = common.parse_int_literal(tok)
        if val is None:
            raise ParseError(path, lineno,
                             "{}: {!r} is not a word literal".format(label, tok))
        if val > 0xFFFF:
            raise ParseError(path, lineno,
                             "{}: {!r} exceeds one word".format(label, tok))
        values.append(val)
    return values


def enum_run(path, lines, label, count, scope, name_to_value, start=None):
    """A .byte run written as `SCOPE::MEMBER` references.

    Returns [(value, member_name)]. Every token must name a real enumerator of
    `scope` — an unresolvable one is a hard error, never a guessed value."""
    if start is None:
        start = find_label(path, lines, label)
    tokens = gather_run(path, lines, start, ".byte")
    _assert_count(path, label, tokens, count)
    out = []
    for tok, lineno in tokens:
        m = _SCOPED_TOK_RE.match(tok)
        if m is None or m.group(1) != scope:
            raise ParseError(path, lineno,
                             "{}: {!r} is not a {}:: reference"
                             .format(label, tok, scope))
        name = m.group(2)
        if name not in name_to_value:
            raise ParseError(path, lineno,
                             "{}: {}::{} is not a {} enumerator"
                             .format(label, scope, name, scope))
        out.append((name_to_value[name], name))
    return out


def to_signed8(value):
    return value - 0x100 if value >= 0x80 else value


def to_signed16(value):
    return value - 0x10000 if value >= 0x8000 else value


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


# --- s2: formula support, mappings, and the AI/dance/item satellites ---------

DANCE_RATE_LEN = 3
EQUIP_EVADE_LEN = 11
RAND_BIT_RATE_ROWS = 5
RAND_BIT_RATE_COLS = 4
FINAL_BATTLE_LEN = 4
FINAL_SCROLL_LEN = 6
THROW_TOOLS_LEN = 5
SLOT_OUTCOME_LEN = 8
JOKER_TARGET_LEN = 2
DANCE_BG_LEN = 10        # 8 dances + 2 unused rows
BG_DANCE_LEN = 64        # more rows than named backgrounds (trailing padding)
AI_CMD_SIZE_LEN = 16     # ai script commands $f0..$ff
AI_ATTACK_LEN = 11
ITEM_TYPE_MASK_LEN = 8   # 7 ITEM_TYPE rows + 1 out-of-enum pad
MAGIC_ORDER_LEN = 6
DESPERATION_LEN = 14
AI_CMD_FIRST = 0xF0      # the opcode the AICmdSizeTbl index space starts at

_HEADER_ENUM_RE = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(\d+),\s*$")

# The upstream handler banner that names each AI script command, e.g.
#   ; [ ai script command $f3: display short battle dialog ]
#   ; [ ai script command $fe/$ff: end if/end of script ]
_AI_CMD_NAME_RE = re.compile(
    r"^\[\s*ai script command\s+\$([0-9a-f]{2})"
    r"(?:/\$([0-9a-f]{2}))?\s*:\s*(.+?)\s*\]$", re.IGNORECASE)


def _slugify(text):
    """An upstream description -> a SCREAMING_SNAKE enumerator name."""
    out = []
    for ch in text:
        out.append(ch.upper() if ch.isalnum() else "_")
    name = "".join(out)
    while "__" in name:
        name = name.replace("__", "_")
    return name.strip("_")


def read_ai_command_names(path, lines, count, first):
    """value -> enumerator name for the AI script commands.

    The commands are named only in the banner comment above each handler, so the
    names are derived from those. Hard-errors unless every opcode in the range
    is named exactly once — a missing or duplicated banner means the port would
    invent or drop a command name."""
    names = {}
    for lineno, raw in enumerate(lines, 1):
        _code, comment = common.strip_comment(raw)
        if not comment:
            continue
        m = _AI_CMD_NAME_RE.match(comment.strip())
        if m is None:
            continue
        opcodes = [int(m.group(1), 16)]
        descriptions = [m.group(3)]
        if m.group(2) is not None:
            # A shared banner names two commands: "$fe/$ff: end if/end of script"
            opcodes.append(int(m.group(2), 16))
            parts = m.group(3).split("/")
            if len(parts) != 2:
                raise ParseError(path, lineno,
                                 "shared AI-command banner {!r} does not name "
                                 "two commands".format(comment.strip()))
            descriptions = parts
        for opcode, description in zip(opcodes, descriptions):
            name = _slugify(description)
            if not name:
                raise ParseError(path, lineno,
                                 "AI command ${:02X} has an empty name"
                                 .format(opcode))
            if opcode in names:
                raise ParseError(path, lineno,
                                 "AI command ${:02X} is named twice"
                                 .format(opcode))
            names[opcode] = name

    for i in range(count):
        if first + i not in names:
            raise ParseError(path, 0,
                             "no banner names AI script command ${:02X}"
                             .format(first + i))
    if len(names) != count:
        raise ParseError(path, 0,
                         "found {} AI command names, expected {}"
                         .format(len(names), count))
    return names


def render_ai_script_command_h(names, count, first):
    rows = "\n".join(
        "    {:<28} = 0x{:02X},".format(names[first + i], first + i)
        for i in range(count))
    return (
        "// AUTO-GENERATED by tools/asm_parser/parse_battle_main_tables.py — "
        "DO NOT EDIT.\n"
        "// Source: original-src/src/battle/battle_main.asm (the AI script "
        "command handlers)\n"
        "// (original-src pinned at 1ea47b5)\n"
        "//\n"
        "// The commands a monster's AI script is written in. A script is a\n"
        "// byte stream: any value below the first command is an attack to use,\n"
        "// and these values introduce everything else. Each command is followed\n"
        "// by a fixed number of argument bytes — see kAiCommandSizes in\n"
        "// src/data/battle_tables.h.\n"
        "#pragma once\n\n"
        "#include <cstdint>\n\n"
        "namespace ostinato {\n\n"
        "enum class AiScriptCommand : std::uint8_t {\n" +
        rows +
        "\n};\n\n"
        "}  // namespace ostinato\n")


def read_shipped_ids(path, enum_name):
    """value -> enumerator name from a generated port enum header.

    FormationId's names are derived from the formation corpus (not const.inc),
    so the shipped header is the only place the id names exist. Same posture as
    parse_formations.py's MonsterId cross-check."""
    names = {}
    with open(path, "r", encoding="utf-8") as fh:
        for lineno, raw in enumerate(fh, 1):
            m = _HEADER_ENUM_RE.match(raw)
            if m:
                names.setdefault(int(m.group(2)), m.group(1))
    if not names:
        raise ParseError(path, 0,
                         "no {} enumerators found".format(enum_name))
    return names


def _contiguous_named(by_value, length):
    """How many of the first `length` indices have an enumerator (the modeled
    row count; anything past the last named index is upstream padding)."""
    count = 0
    while count < length and count in by_value:
        count += 1
    return count


def read_s2_tables(paths):
    parsed = common.parse_ca65_constants(paths["const_inc"],
                                         skip_body_enums=pce.SKIP)
    bg_parsed = common.parse_ca65_constants(paths["battle_bg_inc"])

    attack_by_value = read_value_names(parsed, "ATTACK", paths["const_inc"])
    attack_by_name = read_name_values(parsed, "ATTACK", paths["const_inc"])
    item_by_value = read_value_names(parsed, "ITEM", paths["const_inc"])
    cmd_by_value = read_value_names(parsed, "BATTLE_CMD", paths["const_inc"])
    char_by_value = read_value_names(parsed, "CHAR", paths["const_inc"])
    type_by_value = read_value_names(parsed, "ITEM_TYPE", paths["const_inc"])
    dance_by_value = read_value_names(parsed, "DANCE", paths["const_inc"])
    dance_by_name = read_name_values(parsed, "DANCE", paths["const_inc"])
    bg_by_value = read_value_names(bg_parsed, "BATTLE_BG",
                                   paths["battle_bg_inc"])
    bg_by_name = read_name_values(bg_parsed, "BATTLE_BG",
                                  paths["battle_bg_inc"])
    formation_by_value = read_shipped_ids(paths["formation_id_h"],
                                          "FormationId")

    asm = paths["battle_main_asm"]
    with open(asm, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    dance_rate = byte_run_ext(asm, lines, "DanceRateTbl", DANCE_RATE_LEN)
    equip_evade = word_run(asm, lines, "EquipEvadeTbl", EQUIP_EVADE_LEN)
    rand_bit_rate = byte_run_ext(asm, lines, "RandBitRateTbl",
                                 RAND_BIT_RATE_ROWS * RAND_BIT_RATE_COLS)
    final_ids = word_run(asm, lines, "FinalBattleIDTbl", FINAL_BATTLE_LEN)
    final_scroll = byte_run_ext(asm, lines, "FinalBattleScrollTbl",
                                FINAL_SCROLL_LEN)
    throw_items = byte_run_ext(asm, lines, "ThrowToolsItemTbl", THROW_TOOLS_LEN)
    throw_offsets = byte_run_ext(asm, lines, "ThrowToolsOffsetTbl",
                                 THROW_TOOLS_LEN)
    slot_attacks = byte_run_ext(asm, lines, "SlotAttackTbl", SLOT_OUTCOME_LEN)
    joker_targets = byte_run_ext(asm, lines, "JokerTargetTbl", JOKER_TARGET_LEN)
    ai_sizes = byte_run_ext(asm, lines, "AICmdSizeTbl", AI_CMD_SIZE_LEN)
    ai_attacks = byte_run_ext(asm, lines, "AttackForAITbl", AI_ATTACK_LEN)
    ai_commands = byte_run_ext(asm, lines, "CmdForAITbl", AI_ATTACK_LEN)
    item_type_mask = byte_run_ext(asm, lines, "ItemTypeMaskTbl",
                                  ITEM_TYPE_MASK_LEN)
    black_order = byte_run_ext(asm, lines, "BlackMagicOrderTbl", MAGIC_ORDER_LEN)
    effect_order = byte_run_ext(asm, lines, "EffectMagicOrderTbl",
                                MAGIC_ORDER_LEN)
    white_order = byte_run_ext(asm, lines, "WhiteMagicOrderTbl", MAGIC_ORDER_LEN)

    ai_command_names = read_ai_command_names(asm, lines, AI_CMD_SIZE_LEN,
                                             AI_CMD_FIRST)

    dance_bg = enum_run(asm, lines, "DanceBG", DANCE_BG_LEN, "BATTLE_BG",
                        bg_by_name)
    bg_dance = enum_run(asm, lines, "BattleBGDance", BG_DANCE_LEN, "DANCE",
                        dance_by_name)
    desperation = enum_run(asm, lines, "(desperation_attack)", DESPERATION_LEN,
                           "ATTACK", attack_by_name,
                           start=find_segment(asm, lines,
                                              "desperation_attack"))

    # Modeled row counts: rows past the last named index are upstream padding,
    # verified in the fixture rather than given invented identities.
    named_dances = _contiguous_named(dance_by_value, DANCE_BG_LEN)
    named_bgs = _contiguous_named(bg_by_value, BG_DANCE_LEN)
    named_types = _contiguous_named(type_by_value, ITEM_TYPE_MASK_LEN)

    if item_type_mask[named_types] != 0x00:
        raise ParseError(asm, 0,
                         "ItemTypeMaskTbl row {} is ${:02X}, expected the "
                         "out-of-enum $00 padding"
                         .format(named_types, item_type_mask[named_types]))
    for i in range(DESPERATION_LEN):
        if i not in char_by_value:
            raise ParseError(paths["const_inc"], 0,
                             "no CHAR name for character slot {}".format(i))

    return {
        "dance_rate": dance_rate,
        "equip_evade": equip_evade,
        "equip_evade_signed": [to_signed16(v) for v in equip_evade],
        "rand_bit_rate": rand_bit_rate,
        "final_ids": final_ids,
        "final_id_names": [
            formation_by_value[v] if v in formation_by_value else None
            for v in final_ids],
        "final_scroll": final_scroll,
        "throw_items": throw_items,
        "throw_item_names": names_for(asm, "ThrowToolsItemTbl", throw_items,
                                      item_by_value, "ITEM"),
        "throw_offsets": throw_offsets,
        "slot_attacks": slot_attacks,
        "slot_attack_names": names_for(asm, "SlotAttackTbl", slot_attacks,
                                       attack_by_value, "ATTACK"),
        "joker_targets": joker_targets,
        "ai_sizes": ai_sizes,
        "ai_script_names": ai_command_names,
        "ai_attacks": ai_attacks,
        "ai_attack_names": names_for(asm, "AttackForAITbl", ai_attacks,
                                     attack_by_value, "ATTACK"),
        "ai_commands": ai_commands,
        "ai_command_names": names_for(asm, "CmdForAITbl", ai_commands,
                                      cmd_by_value, "BATTLE_CMD"),
        "item_type_mask": item_type_mask,
        "type_names": [type_by_value[i] for i in range(named_types)],
        "black_order": black_order,
        "effect_order": effect_order,
        "white_order": white_order,
        "dance_bg": dance_bg,
        "dance_names": [dance_by_value[i] for i in range(named_dances)],
        "bg_dance": bg_dance,
        "bg_names": [bg_by_value[i] for i in range(named_bgs)],
        "desperation": desperation,
        "char_names": [char_by_value[i] for i in range(DESPERATION_LEN)],
        "named_dances": named_dances,
        "named_bgs": named_bgs,
        "named_types": named_types,
    }


_S2_REGEN = ("//   python3 tools/asm_parser/parse_battle_main_tables.py \\\n"
             "//       --source-root  original-src\n")


def _rows(items, indent="    "):
    return ",\n".join(indent + item for item in items)


def render_s2_inc(t):
    for name in t["final_id_names"]:
        if name is None:
            raise ParseError("formation_id.h", 0,
                             "FinalBattleIDTbl value has no FormationId "
                             "enumerator (escalate)")
    out = [
        _BANNER,
        "// Source: original-src/src/battle/battle_main.asm (formula-support,\n"
        "// mapping, and AI/dance/item tables)\n"
        "// (original-src pinned at 1ea47b5)\n"
        "// DO NOT EDIT BY HAND — regenerate via:\n"
        "{}\n".format(_S2_REGEN),
        "// The battle engine's small keyed tables: probability ladders, the\n"
        "// equipment evade boosts, the final-battle chain, and the dance /\n"
        "// AI / item mappings. Every row carries its own identity — an\n"
        "// enumerator where the key space is named, the ROM index otherwise.\n"
        "// #included at namespace scope in src/data/battle_tables.h.\n\n",
    ]

    out.append(
        "// DanceRateTbl (battle_main.asm:946, c2/05ce): the cumulative random\n"
        "// thresholds that pick a dance's step — the four outcomes land at\n"
        "// 7/16, 3/8, 1/8, 1/16.\n"
        "inline constexpr std::array<DanceStepThresholdEntry, {}> "
        "kDanceStepThresholds = {{{{\n{}\n}}}};\n\n".format(
            DANCE_RATE_LEN,
            _rows("DanceStepThresholdEntry{{ .index = {}, "
                  ".threshold = RandomThreshold{{{}}} }}".format(i, v)
                  for i, v in enumerate(t["dance_rate"]))))

    out.append(
        "// EquipEvadeTbl (battle_main.asm:2651, c2/1105): the evade / magic-block\n"
        "// boost an equipment item's evade nibble grants.\n"
        "inline constexpr std::array<EquipEvadeBoostEntry, {}> kEquipEvadeBoost "
        "= {{{{\n{}\n}}}};\n\n".format(
            EQUIP_EVADE_LEN,
            _rows("EquipEvadeBoostEntry{{ .index = {}, .boost = {} }}"
                  .format(i, v)
                  for i, v in enumerate(t["equip_evade_signed"]))))

    rate_rows = []
    for r in range(RAND_BIT_RATE_ROWS):
        row = t["rand_bit_rate"][r * RAND_BIT_RATE_COLS:
                                 (r + 1) * RAND_BIT_RATE_COLS]
        rate_rows.append(
            "RandomBitRateEntry{{ .index = {}, .weights = {{{{\n"
            "{}\n    }}}} }}".format(
                r, ",\n".join("        RandomThreshold{{{}}}".format(v)
                              for v in row)))
    out.append(
        "// RandBitRateTbl (battle_main.asm:13549, c2/5269): cumulative\n"
        "// probability weights over the four bits RandBitWithRate picks from.\n"
        "// Rows 0-3 weight Umaro's attack choice; row 4 weights the battle type.\n"
        "inline constexpr std::array<RandomBitRateEntry, {}> kRandomBitRates = "
        "{{{{\n{}\n}}}};\n\n".format(RAND_BIT_RATE_ROWS, _rows(rate_rows)))

    out.append(
        "// FinalBattleIDTbl (battle_main.asm:12278, c2/4aab): the final-battle\n"
        "// formation chain — clearing one advances to the next.\n"
        "inline constexpr std::array<FormationId, {}> kFinalBattleFormations = "
        "{{{{\n{}\n}}}};\n\n".format(
            FINAL_BATTLE_LEN,
            _rows("FormationId::" + n for n in t["final_id_names"])))

    out.append(
        "// FinalBattleScrollTbl (battle_main.asm:12284, c2/4ab3): the background\n"
        "// scroll position each step of that chain sets.\n"
        "inline constexpr std::array<FinalBattleScrollEntry, {}> "
        "kFinalBattleScroll = {{{{\n{}\n}}}};\n\n".format(
            FINAL_SCROLL_LEN,
            _rows("FinalBattleScrollEntry{{ .index = {}, .scroll = {} }}"
                  .format(i, v)
                  for i, v in enumerate(t["final_scroll"]))))

    out.append(
        "// ThrowToolsItemTbl/ThrowToolsOffsetTbl (battle_main.asm:6573/6579,\n"
        "// c2/2778): a thrown tool subtracts its offset from the item id to\n"
        "// reach the attack it performs.\n"
        "inline constexpr std::array<ThrowToolsConversion, {}> "
        "kThrowToolsConversions = {{{{\n{}\n}}}};\n\n".format(
            THROW_TOOLS_LEN,
            _rows("{{ ItemId::{}, {} }}".format(n, o)
                  for n, o in zip(t["throw_item_names"], t["throw_offsets"]))))

    out.append(
        "// SlotAttackTbl (battle_main.asm:12802, c2/4e4a): the attack each slot\n"
        "// reel outcome performs. NONE marks the outcome that rolls a random\n"
        "// esper instead.\n"
        "inline constexpr std::array<SlotOutcomeEntry, {}> kSlotOutcomes = "
        "{{{{\n{}\n}}}};\n\n".format(
            SLOT_OUTCOME_LEN,
            _rows("SlotOutcomeEntry{{ .index = {}, .attack = AttackId::{} }}"
                  .format(i, n)
                  for i, n in enumerate(t["slot_attack_names"]))))

    out.append(
        "// JokerTargetTbl (battle_main.asm:12806, c2/4e52): the target mask the\n"
        "// joker-doom outcomes apply (all characters / all monsters).\n"
        "inline constexpr std::array<JokerDoomTargetEntry, {}> "
        "kJokerDoomTargets = {{{{\n{}\n}}}};\n\n".format(
            JOKER_TARGET_LEN,
            _rows("JokerDoomTargetEntry{{ .index = {}, "
                  ".targets = BattleSlotMask{{0x{:02X}}} }}".format(i, v)
                  for i, v in enumerate(t["joker_targets"]))))

    out.append(
        "// DanceBG (battle_main.asm:3742, d1/f9ab): the battle background a\n"
        "// dance switches to. (The ROM run has {} rows; the {} past the named\n"
        "// dances are unused padding, verified in the fixture.)\n"
        "inline constexpr std::array<DanceBackgroundEntry, {}> "
        "kDanceBackgrounds = {{{{\n{}\n}}}};\n\n".format(
            DANCE_BG_LEN, DANCE_BG_LEN - t["named_dances"], t["named_dances"],
            _rows("{{ DanceId::{}, BattleBackgroundId::{} }}".format(
                t["dance_names"][i], t["dance_bg"][i][1])
                for i in range(t["named_dances"]))))

    out.append(
        "// BattleBGDance (battle_main.asm:3757, ed/8e5b): the dance that matches\n"
        "// each battle background — dancing it here keeps the background. (The\n"
        "// ROM run has {} rows; the {} past the named backgrounds are padding,\n"
        "// verified in the fixture.)\n"
        "inline constexpr std::array<BackgroundDanceEntry, {}> "
        "kBackgroundDances = {{{{\n{}\n}}}};\n\n".format(
            BG_DANCE_LEN, BG_DANCE_LEN - t["named_bgs"], t["named_bgs"],
            _rows("{{ BattleBackgroundId::{}, DanceId::{} }}".format(
                t["bg_names"][i], t["bg_dance"][i][1])
                for i in range(t["named_bgs"]))))

    out.append(
        "// AICmdSizeTbl (battle_main.asm:4999, c2/1daf): how many bytes each AI\n"
        "// script command occupies, including the command byte itself.\n"
        "inline constexpr std::array<AiCommandSizeEntry, {}> kAiCommandSizes = "
        "{{{{\n{}\n}}}};\n\n".format(
            AI_CMD_SIZE_LEN,
            _rows("{{ AiScriptCommand::{}, {} }}".format(
                t["ai_script_names"][AI_CMD_FIRST + i], v)
                for i, v in enumerate(t["ai_sizes"]))))

    out.append(
        "// AttackForAITbl/CmdForAITbl (battle_main.asm:5025/5031, c2/1dd8): the\n"
        "// command an AI-chosen attack belongs to. The consumer scans downward,\n"
        "// so each row is the first attack id that maps to its command.\n"
        "inline constexpr std::array<AiCommandForAttack, {}> "
        "kAiCommandsForAttack = {{{{\n{}\n}}}};\n\n".format(
            AI_ATTACK_LEN,
            _rows("{{ AttackId::{}, BattleCommandId::{} }}".format(a, c)
                  for a, c in zip(t["ai_attack_names"],
                                  t["ai_command_names"]))))

    out.append(
        "// ItemTypeMaskTbl (battle_main.asm:14042, c2/5549): the battle-usability\n"
        "// flag bits an item's type contributes. (The ROM run has {} rows; the\n"
        "// last is out-of-enum $00 padding, verified in the fixture.)\n"
        "inline constexpr std::array<ItemTypeFlagsEntry, {}> "
        "kItemTypeBattleFlags = {{{{\n{}\n}}}};\n\n".format(
            ITEM_TYPE_MASK_LEN, t["named_types"],
            _rows("{{ ItemType::{}, ItemTypeBattleFlags{{0x{:02X}}} }}".format(
                t["type_names"][i], t["item_type_mask"][i])
                for i in range(t["named_types"]))))

    for key, label, line, addr, desc in (
            ("black_order", "kBlackMagicOrder", 14342, "c2/574b",
             "black magic\n// (attacks $00-$17)"),
            ("effect_order", "kEffectMagicOrder", 14345, "c2/5751",
             "effect magic\n// (attacks $18-$2C)"),
            ("white_order", "kWhiteMagicOrder", 14348, "c2/5757",
             "white magic\n// (attacks $2D-$35)")):
        out.append(
            "// {}Tbl (battle_main.asm:{}, {}): where {} lands\n"
            "// in the spell list under each spell-order setting. The setting is\n"
            "// the config menu's magic order, which the menu draws as a bare\n"
            "// numeral (setting N is shown as order N+1), so it has no name of\n"
            "// its own. The offset is added to the attack id, so a negative\n"
            "// value moves the band ahead of the others.\n"
            "inline constexpr std::array<MagicOrderOffsetEntry, {}> {} = "
            "{{{{\n{}\n}}}};\n\n".format(
                label[1:], line, addr, desc,
                MAGIC_ORDER_LEN, label,
                _rows("MagicOrderOffsetEntry{{ .setting = {}, .offset = {} }}"
                      .format(i, to_signed8(v))
                      for i, v in enumerate(t[key]))))

    out.append(
        "// The desperation attacks (battle_main.asm:3599, cf/fea0, segment\n"
        "// \"desperation_attack\"): one per character slot. The run is unused —\n"
        "// nothing in the ROM reads it — and is carried as contract.\n"
        "inline constexpr std::array<DesperationAttackEntry, {}> "
        "kDesperationAttacks = {{{{\n{}\n}}}};\n".format(
            DESPERATION_LEN,
            _rows("{{ CharacterId::{}, AttackId::{} }}".format(
                t["char_names"][i], t["desperation"][i][1])
                for i in range(DESPERATION_LEN))))

    return "".join(out)


def _fixture_words(name, values):
    body = ", ".join("0x{:04X}".format(v) for v in values)
    return ("inline constexpr std::array<std::uint16_t, {}> {} = {{{{ {} }}}};\n"
            .format(len(values), name, body))


def render_s2_fixture(t):
    out = [
        _BANNER,
        "// (original-src pinned at 1ea47b5) — DO NOT EDIT BY HAND.\n"
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace ostinato::test {\n\n"
        "// Ground-truth raw ROM bytes for the battle formula-support and\n"
        "// mapping tables. Byte-identity tests compare each generated table\n"
        "// against these, including the rows the port does not model.\n\n",
    ]
    out.append(_fixture_array("kExpectedDanceRate", t["dance_rate"]))
    out.append(_fixture_words("kExpectedEquipEvade", t["equip_evade"]))
    out.append(_fixture_array("kExpectedRandBitRate", t["rand_bit_rate"]))
    out.append(_fixture_words("kExpectedFinalBattleId", t["final_ids"]))
    out.append(_fixture_array("kExpectedFinalBattleScroll", t["final_scroll"]))
    out.append(_fixture_array("kExpectedThrowToolsItem", t["throw_items"]))
    out.append(_fixture_array("kExpectedThrowToolsOffset", t["throw_offsets"]))
    out.append(_fixture_array("kExpectedSlotAttack", t["slot_attacks"]))
    out.append(_fixture_array("kExpectedJokerTarget", t["joker_targets"]))
    out.append(_fixture_array("kExpectedDanceBG",
                              [v for v, _n in t["dance_bg"]]))
    out.append(_fixture_array("kExpectedBattleBGDance",
                              [v for v, _n in t["bg_dance"]]))
    out.append(_fixture_array("kExpectedAICmdSize", t["ai_sizes"]))
    out.append(_fixture_array("kExpectedAttackForAI", t["ai_attacks"]))
    out.append(_fixture_array("kExpectedCmdForAI", t["ai_commands"]))
    out.append(_fixture_array("kExpectedItemTypeMask", t["item_type_mask"]))
    out.append(_fixture_array("kExpectedBlackMagicOrder", t["black_order"]))
    out.append(_fixture_array("kExpectedEffectMagicOrder", t["effect_order"]))
    out.append(_fixture_array("kExpectedWhiteMagicOrder", t["white_order"]))
    out.append(_fixture_array("kExpectedDesperationAttack",
                              [v for v, _n in t["desperation"]]))
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
    s2 = read_s2_tables(paths)
    if check_only:
        print("OK: 3 command masks (round-trip verified), 3 id lists, 2 "
              "per-command mappings ({} rows), 2 pair tables; all bytes resolve."
              .format(REAL_CMDS))
        print("OK: 19 formula-support/mapping tables; {} dance rows, {} "
              "background rows, {} item-type rows modeled (padding verified); "
              "all ids resolve."
              .format(s2["named_dances"], s2["named_bgs"], s2["named_types"]))
        return 0
    _write(outs["inc"], render_inc(t))
    _write(outs["fixture"], render_fixture(t))
    _write(outs["inc_s2"], render_s2_inc(s2))
    _write(outs["fixture_s2"], render_s2_fixture(s2))
    _write(outs["ai_command_h"],
           render_ai_script_command_h(s2["ai_script_names"], AI_CMD_SIZE_LEN,
                                      AI_CMD_FIRST))
    print("Emitted command-domain satellite tables -> {}".format(outs["inc"]))
    print("Emitted formula-support/mapping tables   -> {}".format(outs["inc_s2"]))
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
        "battle_bg_inc": os.path.join(root, "include", "gfx", "battle_bg.inc"),
        "formation_id_h": os.path.join(repo, "include", "ostinato",
                                       "formation_id.h"),
    }
    outs = {
        "inc": os.path.join(repo, "src", "data", "generated",
                            "battle_cmd_tables_data.inc"),
        "fixture": os.path.join(repo, "tests", "fixtures",
                                "battle_cmd_tables_expected.h"),
        "inc_s2": os.path.join(repo, "src", "data", "generated",
                               "battle_formula_tables_data.inc"),
        "fixture_s2": os.path.join(repo, "tests", "fixtures",
                                   "battle_formula_tables_expected.h"),
        "ai_command_h": os.path.join(repo, "include", "ostinato",
                                     "ai_script_command.h"),
    }
    try:
        return run(paths, outs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
