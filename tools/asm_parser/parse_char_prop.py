#!/usr/bin/env python3
"""Emit the character base-stats table from original-src char_prop.asm.

Port-time tooling (NOT a build/CI dependency): interprets the ca65 record DSL in
src/field/char_prop.asm and emits, per the Phase-1.A PLAN (D6/D7 + Amendment A1):

  * src/data/generated/char_prop_data.inc  — one designated-initializer
    CharacterBaseStats row per record (64 records), self-labeling per the
    data-surface discipline; the CharacterBaseStats array #includes it.
  * tests/fixtures/char_prop_expected.h     — the same 64 records as raw 22-byte
    rows (the ground-truth byte contract) for a full-corpus byte-equivalence test.

The DSL: each record is a `make_char_prop` (reset to defaults) followed by
`set_char_prop_*` overrides and closed by `end_char_prop` (emits 22 bytes in the
`end_char_prop` field order). `empty_char_prop N` emits N zero-filled records.
The macro *definitions* at the top of the file are skipped; this parser models
their documented effect on the record's fields.

Symbol values (battle commands, items, run/level modifiers, the fixed-equip flag)
resolve against original-src/include/const.inc; an unknown symbol is a hard error
with a file:line citation so the executor escalates rather than guessing.

Structural guarantees, hard-errored at emit time:
  * every record emits exactly 22 bytes, each in 0..255;
  * the packed trait byte's three fields are disjoint and losslessly recoverable
    (run_factor bits 0-1, level_mod bits 2-3, fixed_equip bit 4 — PLAN D6);
  * the record count equals the CHAR_PROP index space (64 — PLAN A1);
  * every record index has a CHAR_PROP name.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_char_prop.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_char_prop.py --char-prop-asm PATH --const-inc PATH \\
                       --inc-out FILE --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import sys

import common
import parse_const_enums as pce
from common import ParseError, parse_int_literal, strip_comment


# --- record model ----------------------------------------------------------

# Field order of the emitted 22-byte record (PLAN D7 == char_prop.asm
# end_char_prop). The nine stat fields and the six equipment fields are named
# here once so the .inc rows and the field-order docs never drift.
_STAT_FIELDS = ("strength", "agility", "stamina", "magicPower", "battlePower",
                "defense", "magicDefense", "evade", "magicBlock")
_EQUIP_FIELDS = ("weapon", "shield", "helmet", "armor")
_RELIC_FIELDS = ("relic1", "relic2")


class Record(object):
    """One char_prop record: its populated fields (or empty flag) + its bytes.

    Symbolic fields (cmds/equip/relics/run_factor/level_mod) hold the upstream
    member *names* so the .inc can render typed enum initializers; `bytes` holds
    the resolved 22-byte ground truth. `name` is filled from CHAR_PROP by index.
    """

    def __init__(self, index, empty):
        self.index = index
        self.empty = empty
        self.name = None
        # Populated-record fields (None when empty):
        self.hp = 0
        self.mp = 0
        self.cmds = None          # list[str] of BATTLE_CMD member names, len 4
        self.stats = None         # list[int], len 9
        self.equip = None         # list[str] of ITEM member names, len 4
        self.relics = None        # list[str] of ITEM member names, len 2
        self.run_factor = None    # CHAR_RUN_FACTOR member name
        self.level_mod = None     # CHAR_LEVEL_MOD member name
        self.fixed_equip = False
        self.bytes = None         # list[int], len 22


class _Regs(object):
    """The `_char_prop_*` assembler variables, reset by make_char_prop."""

    def __init__(self):
        # Defaults per make_char_prop in char_prop.asm.
        self.hp = 0
        self.mp = 0
        self.cmds = ["NONE", "NONE", "NONE", "NONE"]
        self.stats = [0] * 9
        self.equip = ["EMPTY", "EMPTY", "EMPTY", "EMPTY"]
        self.relics = ["EMPTY", "EMPTY"]
        self.run_factor = "NORMAL"
        self.level_mod = "NORMAL"
        self.fixed_equip = False


# --- symbol resolution -----------------------------------------------------

class _Symbols(object):
    """The const.inc enums + the fixed-equip global the DSL resolves against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc, skip_body_enums=pce.SKIP)
        for enum_name in ("BATTLE_CMD", "ITEM", "CHAR_RUN_FACTOR",
                          "CHAR_LEVEL_MOD", "CHAR_PROP"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(enum_name))
        if "CHAR_PROP_FIXED_EQUIP" not in self.parsed.globals:
            raise ParseError(const_inc, 0,
                             "expected symbol 'CHAR_PROP_FIXED_EQUIP' not found")
        self.fixed_equip_bit = self.parsed.globals["CHAR_PROP_FIXED_EQUIP"]

    def value(self, enum_name, member, path, lineno):
        val = self.parsed.enum(enum_name).value_of(member)
        if val is None:
            raise ParseError(path, lineno,
                             "unknown {}::{}".format(enum_name, member))
        return val

    def char_prop_name_by_index(self):
        by_index = {}
        for m in self.parsed.enum("CHAR_PROP").members:
            by_index[m.value] = m.name
        return by_index


# --- the DSL walker --------------------------------------------------------

# File-scope lines that are recognized and ignored (not record content).
_IGNORED_PREFIXES = (".export", ".segment", ".import", ".global", ".include",
                     ".list", ".setcpu", ".p816", ".a", ".i")


def parse_char_prop(asm_path, symbols):
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    records = []
    macro_depth = 0
    regs = None          # non-None between make_char_prop and end_char_prop
    next_index = 0

    def emit_int_args(args, count, path, lineno, what):
        if len(args) != count:
            raise ParseError(path, lineno,
                             "{} expects {} args, got {}".format(what, count, len(args)))
        out = []
        for a in args:
            v = parse_int_literal(a)
            if v is None:
                raise ParseError(path, lineno,
                                 "{}: '{}' is not an integer literal".format(what, a))
            out.append(v)
        return out

    for idx, raw in enumerate(lines):
        lineno = idx + 1
        code, _comment = strip_comment(raw)
        if not code:
            continue
        s = code.strip()
        low = s.lower()

        # --- macro definitions: skip wholesale (.mac/.macro .. .endmac/.endmacro) ---
        if low.startswith(".endmac"):
            if macro_depth == 0:
                raise ParseError(asm_path, lineno, ".endmac without .mac")
            macro_depth -= 1
            continue
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if macro_depth > 0:
            continue

        # --- ignorable file-scope directives / labels ---
        if low.startswith(_IGNORED_PREFIXES):
            continue
        if s.endswith(":") and common._RE_IDENT.match(s[:-1]):
            continue
        if s.startswith("."):
            raise ParseError(asm_path, lineno,
                             "unexpected directive '{}' (grammar not covered — "
                             "escalate per D3)".format(s))

        # --- record DSL macro invocations ---
        parts = s.split(None, 1)
        name = parts[0]
        args = [a.strip() for a in parts[1].split(",")] if len(parts) > 1 else []

        if name == "make_char_prop":
            if regs is not None:
                raise ParseError(asm_path, lineno,
                                 "make_char_prop before the previous end_char_prop")
            regs = _Regs()
        elif name == "empty_char_prop":
            (count,) = emit_int_args(args, 1, asm_path, lineno, "empty_char_prop")
            for _ in range(count):
                rec = Record(next_index, empty=True)
                rec.bytes = [0] * 22
                records.append(rec)
                next_index += 1
        elif regs is None:
            raise ParseError(asm_path, lineno,
                             "'{}' outside a make_char_prop..end_char_prop block"
                             .format(name))
        elif name == "set_char_prop_hp_mp":
            regs.hp, regs.mp = emit_int_args(args, 2, asm_path, lineno, name)
        elif name == "set_char_prop_stats":
            regs.stats = emit_int_args(args, 9, asm_path, lineno, name)
        elif name == "set_char_prop_cmds":
            # 1..4 command names; missing trailing args default to NONE.
            _require_max(args, 4, asm_path, lineno, name)
            regs.cmds = [a if a else "NONE" for a in args] + ["NONE"] * (4 - len(args))
        elif name == "set_char_prop_equip":
            _require_max(args, 4, asm_path, lineno, name)
            regs.equip = [a if a else "EMPTY" for a in args] + ["EMPTY"] * (4 - len(args))
        elif name == "set_char_prop_relics":
            _require_max(args, 2, asm_path, lineno, name)
            regs.relics = [a if a else "EMPTY" for a in args] + ["EMPTY"] * (2 - len(args))
        elif name == "set_char_prop_run_factor":
            _require_exact(args, 1, asm_path, lineno, name)
            regs.run_factor = args[0]
        elif name == "set_char_prop_level_mod":
            _require_exact(args, 1, asm_path, lineno, name)
            regs.level_mod = args[0]
        elif name == "set_char_prop_fixed_equip":
            _require_exact(args, 0, asm_path, lineno, name)
            regs.fixed_equip = True
        elif name == "end_char_prop":
            _require_exact(args, 0, asm_path, lineno, name)
            records.append(_finish(next_index, regs, symbols, asm_path, lineno))
            next_index += 1
            regs = None
        else:
            raise ParseError(asm_path, lineno,
                             "unknown char_prop macro '{}'".format(name))

    if regs is not None:
        raise ParseError(asm_path, len(lines),
                         "unterminated record (missing end_char_prop)")
    if macro_depth:
        raise ParseError(asm_path, len(lines), "unterminated .macro")

    _assign_names_and_verify(records, symbols, asm_path)
    return records


def _require_max(args, n, path, lineno, what):
    if len(args) > n or len(args) == 0:
        raise ParseError(path, lineno,
                         "{} expects 1..{} args, got {}".format(what, n, len(args)))


def _require_exact(args, n, path, lineno, what):
    # An empty arg string yields [''] from split; treat a lone '' as zero args.
    real = [a for a in args if a != ""]
    if len(real) != n:
        raise ParseError(path, lineno,
                         "{} expects {} args, got {}".format(what, n, len(real)))


def _finish(index, regs, symbols, path, lineno):
    rec = Record(index, empty=False)
    rec.hp, rec.mp = regs.hp, regs.mp
    rec.cmds, rec.stats = list(regs.cmds), list(regs.stats)
    rec.equip, rec.relics = list(regs.equip), list(regs.relics)
    rec.run_factor, rec.level_mod = regs.run_factor, regs.level_mod
    rec.fixed_equip = regs.fixed_equip

    rf = symbols.value("CHAR_RUN_FACTOR", regs.run_factor, path, lineno)
    lm = symbols.value("CHAR_LEVEL_MOD", regs.level_mod, path, lineno)
    fe = symbols.fixed_equip_bit if regs.fixed_equip else 0

    # PLAN D6: the three trait fields must be disjoint and losslessly recoverable.
    packed = rf | lm | fe
    if (packed & CharacterTraitsMasks.RUN) != rf \
            or (packed & CharacterTraitsMasks.LEVEL) != lm \
            or (packed & CharacterTraitsMasks.FIXED) != fe:
        raise ParseError(
            path, lineno,
            "packed trait byte fields overlap (run={:#04x} level={:#04x} "
            "fixed={:#04x}) — D6 disjointness violated".format(rf, lm, fe))

    body = [rec.hp, rec.mp]
    body += [symbols.value("BATTLE_CMD", c, path, lineno) for c in rec.cmds]
    body += rec.stats
    body += [symbols.value("ITEM", e, path, lineno) for e in rec.equip]
    body += [symbols.value("ITEM", r, path, lineno) for r in rec.relics]
    body.append(packed)

    if len(body) != 22:
        raise ParseError(path, lineno,
                         "record produced {} bytes, expected 22".format(len(body)))
    for b in body:
        if not (0 <= b <= 0xFF):
            raise ParseError(path, lineno,
                             "byte value {} out of range 0..255".format(b))
    rec.bytes = body
    return rec


class CharacterTraitsMasks(object):
    RUN = 0x03
    LEVEL = 0x0c
    FIXED = 0x10


def _assign_names_and_verify(records, symbols, path):
    names = symbols.char_prop_name_by_index()
    expected_count = len(symbols.parsed.enum("CHAR_PROP").members)
    if len(records) != expected_count:
        raise ParseError(path, 0,
                         "char_prop produced {} records; CHAR_PROP index space is "
                         "{} (PLAN A1)".format(len(records), expected_count))
    for rec in records:
        if rec.index not in names:
            raise ParseError(path, 0,
                             "record index {} has no CHAR_PROP name".format(rec.index))
        rec.name = names[rec.index]


# --- rendering -------------------------------------------------------------

# Emitted-file headers follow the house format (worked precedent: the Crystal
# port's src/data/generated/): AUTO-GENERATED line, Source lines, upstream pin,
# DO-NOT-EDIT + exact regeneration command, then a consumption paragraph.
_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_char_prop.py\n"
    "// Source: src/field/char_prop.asm (CharProp, 64 records x 22 bytes)\n"
    "// Source: include/const.inc (BATTLE_CMD / ITEM / CHAR_RUN_FACTOR /\n"
    "//         CHAR_LEVEL_MOD / CHAR_PROP_FIXED_EQUIP / CHAR_PROP values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_char_prop.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/char_prop_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/char_prop_expected.h\n"
    "\n"
)


def _row_comment(rec):
    return "// [${:02X}] {}{}".format(
        rec.index, rec.name, "  (empty)" if rec.empty else "")


def _render_inc(records):
    lines = [_HEADER_COMMON,
             "// CharacterBaseStats rows in CHAR_PROP record-index order ($00..$3f),\n"
             "// one designated-initializer row per record, #included inside the\n"
             "// kCharacterBaseStats array in src/data/character.cpp. Each row is\n"
             "// preceded by its identity, // [$NN] CHAR_PROP_NAME — the table is\n"
             "// indexed by CharacterPropId (include/ostinato/character_prop_id.h),\n"
             "// never by CharacterId, whose 16 aliased values cannot address 64\n"
             "// records. Zero-filled padding records render as {} (all 22 bytes\n"
             "// zero — distinct from the $FF EMPTY/NONE sentinels real records use\n"
             "// for empty slots). The .traits initializer is { run factor,\n"
             "// level mod, fixed-equip }, packing bits 0-1 / 2-3 / 4 of the\n"
             "// record's final byte. Field order mirrors char_prop.asm's\n"
             "// end_char_prop byte emitter.\n\n"]
    for rec in records:
        lines.append(_row_comment(rec) + "\n")
        if rec.empty:
            lines.append("{},\n\n")
            continue
        cmds = ", ".join("BattleCommandId::{}".format(c) for c in rec.cmds)
        stat_kv = ", ".join(".{} = {}".format(f, v)
                            for f, v in zip(_STAT_FIELDS, rec.stats))
        equip_kv = ", ".join(".{} = ItemId::{}".format(f, v)
                             for f, v in zip(_EQUIP_FIELDS, rec.equip))
        relic_kv = ", ".join(".{} = ItemId::{}".format(f, v)
                             for f, v in zip(_RELIC_FIELDS, rec.relics))
        traits = "{{ RunFactor::{}, LevelMod::{}, {} }}".format(
            rec.run_factor, rec.level_mod,
            "true" if rec.fixed_equip else "false")
        lines.append("{{ .hp = {}, .mp = {},\n".format(rec.hp, rec.mp))
        lines.append("  .commands = {{ {} }},\n".format(cmds))
        lines.append("  {},\n".format(stat_kv))
        lines.append("  {},\n".format(equip_kv))
        lines.append("  {},\n".format(relic_kv))
        lines.append("  .traits = {} }},\n\n".format(traits))
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 22-byte char_prop record; field names and order mirror the\n"
    "// end_char_prop byte emitter. Values are the exact ROM bytes with every\n"
    "// upstream symbol resolved — deliberately independent of the enum-symbol\n"
    "// rows in char_prop_data.inc, so symbol/value drift in either artifact\n"
    "// fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedCharacterRecord {\n"
    "    std::uint8_t hp;\n"
    "    std::uint8_t mp;\n"
    "    std::uint8_t cmd1, cmd2, cmd3, cmd4;         // BATTLE_CMD bytes\n"
    "    std::uint8_t strength, agility, stamina;\n"
    "    std::uint8_t magicPower, battlePower;\n"
    "    std::uint8_t defense, magicDefense;\n"
    "    std::uint8_t evade, magicBlock;\n"
    "    std::uint8_t weapon, shield, helmet, armor;  // ITEM bytes\n"
    "    std::uint8_t relic1, relic2;                 // ITEM bytes\n"
    "    std::uint8_t traits;  // run factor bits 0-1 | level mod bits 2-3 | fixed-equip bit 4\n"
    "};\n"
    "static_assert(sizeof(ExpectedCharacterRecord) == 22,\n"
    "              \"fixture record must stay byte-identical to a ROM char_prop record\");\n"
)


def _fixture_row(rec):
    if rec.empty:
        return "    {},\n"
    h = ["0x{:02X}".format(b) for b in rec.bytes]
    return (
        "    {{ .hp = {}, .mp = {},\n"
        "      .cmd1 = {}, .cmd2 = {}, .cmd3 = {}, .cmd4 = {},\n"
        "      .strength = {}, .agility = {}, .stamina = {},"
        " .magicPower = {}, .battlePower = {},\n"
        "      .defense = {}, .magicDefense = {}, .evade = {}, .magicBlock = {},\n"
        "      .weapon = {}, .shield = {}, .helmet = {}, .armor = {},\n"
        "      .relic1 = {}, .relic2 = {}, .traits = {} }},\n"
    ).format(*h)


def _render_fixture(records):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_character_base.cpp — the ground-truth\n"
             "// record bytes. The full-corpus test memcmp-checks every\n"
             "// CharacterBaseStats row (src/data/generated/char_prop_data.inc)\n"
             "// against this table, record by record. Each record is labeled\n"
             "// // [$NN] CHAR_PROP_NAME as in the .inc.\n"
             "\n"
             "#pragma once\n"
             "\n"
             "#include <array>\n"
             "#include <cstdint>\n"
             "\n"
             "namespace ostinato::test {\n"
             "\n",
             _FIXTURE_STRUCT,
             "\n",
             "inline constexpr std::array<ExpectedCharacterRecord, {}> "
             "kExpectedCharacterRecords = {{{{  // ROM CharProp\n".format(len(records))]
    for rec in records:
        lines.append("    " + _row_comment(rec) + "\n")
        lines.append(_fixture_row(rec))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ----------------------------------------------------------------

def run(char_prop_asm, const_inc, inc_out, fixture_out, check_only=False):
    symbols = _Symbols(const_inc)
    records = parse_char_prop(char_prop_asm, symbols)

    if check_only:
        populated = sum(0 if r.empty else 1 for r in records)
        print("OK: {} records ({} populated, {} empty); all 22 bytes, trait "
              "fields disjoint.".format(len(records), populated,
                                        len(records) - populated))
        return 0

    _write(inc_out, _render_inc(records))
    _write(fixture_out, _render_fixture(records))
    print("Emitted {} records -> {}".format(len(records), inc_out))
    print("Emitted fixture -> {}".format(fixture_out))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def _resolve(args):
    char_prop = args.char_prop_asm
    const_inc = args.const_inc
    if args.source_root:
        if not char_prop:
            char_prop = os.path.join(args.source_root, "src", "field", "char_prop.asm")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return char_prop, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (char_prop.asm + const.inc resolved under it)")
    ap.add_argument("--char-prop-asm", help="path to char_prop.asm")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out", default="src/data/generated/char_prop_data.inc",
                    help="output path for the CharacterBaseStats rows")
    ap.add_argument("--fixture-out", default="tests/fixtures/char_prop_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    char_prop, const_inc = _resolve(args)
    if not char_prop or not const_inc:
        ap.error("provide --source-root, or both --char-prop-asm and --const-inc")
    try:
        return run(char_prop, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
