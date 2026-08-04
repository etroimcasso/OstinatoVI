#!/usr/bin/env python3
"""Emit the esper properties table from original-src genju_prop.asm.

Port-time tooling (NOT a build/CI dependency): interprets the ca65 macro DSL in
src/menu/genju_prop.asm and emits, per the Phase-1.B PLAN (D5 + Amendment B1):

  * src/data/generated/genju_prop_data.inc — one designated-initializer
    EsperPropertiesEntry row per esper (27 records), identity as the EsperId
    enumerator field; the kEsperProperties array #includes it.
  * tests/fixtures/genju_prop_expected.h   — the same 27 records as raw
    11-byte rows (the ground-truth byte contract) for a full-corpus
    byte-equivalence test.

The DSL: after the `GenjuProp:` label, exactly one `make_genju_prop` invocation
per esper carrying five brace-grouped `{SPELL, rate}` args plus an optional
GENJU_BONUS arg. Each group expands to TWO bytes in ROM order **rate first,
spell second** (`make_genju_spell` emits `.byte spell_rate, ATTACK::spell_id`).
A blank group `{}` expands to `{0, ATTACK::NONE}` and a missing bonus arg to
GENJU_BONUS::NONE (the macro's `.ifnblank` semantics — the parser derives empty
slots from the source, never hand-assumes them). The macro *definitions* at the
top of the file are skipped; this parser models their documented effect.

Symbol values resolve against original-src/include/const.inc (ATTACK,
GENJU_BONUS for the record bytes; GENJU for the identity names); an unknown
symbol is a hard error with a file:line citation so the executor escalates
rather than guessing.

Structural guarantees, hard-errored at emit time:
  * the `GenjuProp:` label anchors the table (structural anchor);
  * every record emits exactly 11 bytes (5 x {rate, spell} + bonus), each in
    0..255;
  * the record count equals the GENJU index space (27), whose values must be
    contiguous from the first esper id so record index == EsperId - first.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_genju_prop.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_genju_prop.py --genju-prop-asm PATH --const-inc PATH \\
                        --inc-out FILE --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_const_enums as pce
from common import ParseError, parse_int_literal, strip_comment


_RE_GROUP = re.compile(r"^\{\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*(\S+)\s*\}$")


class Spell(object):
    """One learn-spell slot: the ATTACK member name + learn rate."""

    def __init__(self, rate, attack):
        self.rate = rate        # int, 0 for a blank slot
        self.attack = attack    # ATTACK member name ("NONE" for a blank slot)


class Record(object):
    """One esper: five spell slots, a bonus, resolved bytes, and identity."""

    def __init__(self, index, spells, bonus, record_bytes):
        self.index = index
        self.spells = spells        # list[Spell], len 5
        self.bonus = bonus          # GENJU_BONUS member name
        self.bytes = record_bytes   # list[int], len 11 (data-order: rate, spell)
        self.name = None            # GENJU member name, filled by index
        self.id_value = None        # GENJU value, filled by index


class _Symbols(object):
    """The const.inc enums the DSL resolves against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        for enum_name in ("ATTACK", "GENJU", "GENJU_BONUS"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(enum_name))

    def value(self, enum_name, member, path, lineno):
        val = self.parsed.enum(enum_name).value_of(member)
        if val is None:
            raise ParseError(path, lineno,
                             "unknown {}::{}".format(enum_name, member))
        return val


def split_macro_args(text, path, lineno):
    """Split a ca65 macro argument list on top-level commas.

    Commas inside `{...}` token groups do not split (ca65 brace-group
    semantics). Unbalanced braces are a hard error.
    """
    args = []
    depth = 0
    cur = []
    for ch in text:
        if ch == "{":
            depth += 1
            cur.append(ch)
        elif ch == "}":
            depth -= 1
            if depth < 0:
                raise ParseError(path, lineno, "unbalanced '}' in macro args")
            cur.append(ch)
        elif ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if depth != 0:
        raise ParseError(path, lineno, "unbalanced '{' in macro args")
    args.append("".join(cur).strip())
    return args


def parse_spell_group(arg, path, lineno):
    """Parse one `{SPELL, rate}` group (or blank `{}`) into a Spell.

    Blank groups model the macro's `.ifnblank` fallback: rate 0, ATTACK::NONE.
    """
    if arg in ("{}", ""):
        return Spell(0, "NONE")
    m = _RE_GROUP.match(arg)
    if not m:
        raise ParseError(path, lineno,
                         "malformed spell group '{}' (expected "
                         "'{{NAME, rate}}' or '{{}}')".format(arg))
    rate = parse_int_literal(m.group(2))
    if rate is None:
        raise ParseError(path, lineno,
                         "spell rate '{}' is not an integer literal"
                         .format(m.group(2)))
    return Spell(rate, m.group(1))


def parse_genju_prop(asm_path, symbols):
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    records = []
    macro_depth = 0
    label_seen = False

    # File-scope lines that are recognized and ignored (not record content).
    ignored_prefixes = (".export", ".segment", ".import", ".global",
                       ".include", ".list", ".setcpu", ".p816", ".pushseg",
                       ".popseg")

    for idx, raw in enumerate(lines):
        lineno = idx + 1
        code, _comment = strip_comment(raw)
        if not code:
            continue
        s = code.strip()
        low = s.lower()

        # --- macro definitions: skip wholesale (.mac .. .endmac) ---
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

        # --- ignorable file-scope directives ---
        if low.startswith(ignored_prefixes):
            continue

        # --- the structural anchor ---
        if s == "GenjuProp:":
            if label_seen:
                raise ParseError(asm_path, lineno, "duplicate GenjuProp label")
            label_seen = True
            continue

        # --- record invocations ---
        parts = s.split(None, 1)
        if parts[0] == "make_genju_prop":
            if not label_seen:
                raise ParseError(asm_path, lineno,
                                 "make_genju_prop before the GenjuProp label")
            arg_text = parts[1] if len(parts) > 1 else ""
            args = split_macro_args(arg_text, asm_path, lineno)
            if len(args) not in (5, 6):
                raise ParseError(asm_path, lineno,
                                 "make_genju_prop expects 5 spell groups + "
                                 "optional bonus, got {} args".format(len(args)))
            spells = [parse_spell_group(a, asm_path, lineno) for a in args[:5]]
            bonus = args[5] if len(args) == 6 and args[5] else "NONE"
            if not common._RE_IDENT.match(bonus):
                raise ParseError(asm_path, lineno,
                                 "malformed bonus '{}'".format(bonus))
            records.append(_finish(len(records), spells, bonus, symbols,
                                   asm_path, lineno))
            continue

        raise ParseError(asm_path, lineno,
                         "unrecognized line: '{}' (grammar not covered — "
                         "escalate per the PLAN's T1 trigger)".format(s))

    if not label_seen:
        raise ParseError(asm_path, len(lines),
                         "GenjuProp label not found (structural anchor missing)")
    _assign_names_and_verify(records, symbols, asm_path)
    return records


def _finish(index, spells, bonus, symbols, path, lineno):
    body = []
    for sp in spells:
        # ROM byte order within a pair: rate first, spell second
        # (make_genju_spell emits `.byte spell_rate, ATTACK::spell_id`).
        body.append(sp.rate)
        body.append(symbols.value("ATTACK", sp.attack, path, lineno))
    body.append(symbols.value("GENJU_BONUS", bonus, path, lineno))

    if len(body) != 11:
        raise ParseError(path, lineno,
                         "record produced {} bytes, expected 11".format(len(body)))
    for b in body:
        if not (0 <= b <= 0xFF):
            raise ParseError(path, lineno,
                             "byte value {} out of range 0..255".format(b))
    return Record(index, spells, bonus, body)


def _assign_names_and_verify(records, symbols, path):
    genju = symbols.parsed.enum("GENJU")
    expected_count = len(genju.members)
    if len(records) != expected_count:
        raise ParseError(path, 0,
                         "genju_prop produced {} records; GENJU index space is "
                         "{}".format(len(records), expected_count))
    base = genju.members[0].value
    for i, m in enumerate(genju.members):
        if m.value != base + i:
            raise ParseError(path, 0,
                             "GENJU::{} = {} breaks the contiguous index space "
                             "(record index == EsperId - first assumption)"
                             .format(m.name, m.value))
    for rec in records:
        rec.name = genju.members[rec.index].name
        rec.id_value = genju.members[rec.index].value


# --- rendering -------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_genju_prop.py\n"
    "// Source: src/menu/genju_prop.asm (GenjuProp, ROM D8/6E00,\n"
    "//         27 espers x 11 bytes: five {rate, spell} pairs + bonus)\n"
    "// Source: include/const.inc (ATTACK / GENJU / GENJU_BONUS values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_genju_prop.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/genju_prop_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/genju_prop_expected.h\n"
    "\n"
)


def _render_inc(records):
    lines = [_HEADER_COMMON,
             "// EsperPropertiesEntry rows in GENJU index order ($36..$50),\n"
             "// one designated-initializer row per esper, #included inside\n"
             "// the kEsperProperties array in src/data/esper_properties.h.\n"
             "// Each row's identity is its .id field — the EsperId enumerator\n"
             "// (GENJU values start at $36, the esper block of the unified\n"
             "// actor space); a compile-time assert verifies\n"
             "// id == position + EsperId::RAMUH. The packed .record stays\n"
             "// byte-identical to the 11 ROM bytes; within each EsperSpell\n"
             "// pair the ROM byte order is rate first, spell second. Empty\n"
             "// slots are { .learnRate = 0, .spell = AttackId::NONE } and a\n"
             "// missing bonus is EsperBonus::NONE — both derived from the\n"
             "// upstream macro's blank-argument semantics, never assumed.\n\n"]
    for rec in records:
        lines.append("    EsperPropertiesEntry{{  // [${:02X}]\n".format(rec.id_value))
        lines.append("        .id = EsperId::{},\n".format(rec.name))
        lines.append("        .record = EsperProperties{\n")
        lines.append("            .spells = {{\n")
        for sp in rec.spells:
            lines.append("                EsperSpell{{ .learnRate = {:>2}, "
                         ".spell = AttackId::{} }},\n".format(sp.rate, sp.attack))
        lines.append("            }},\n")
        lines.append("            .bonus = EsperBonus::{},\n".format(rec.bonus))
        lines.append("        },\n")
        lines.append("    },\n")
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 11-byte esper record: five {rate, spell} byte pairs in ROM\n"
    "// order (rate first, spell second) then the bonus byte. Values are the\n"
    "// exact ROM bytes with every upstream symbol resolved — deliberately\n"
    "// independent of the enum-symbol rows in genju_prop_data.inc, so\n"
    "// symbol/value drift in either artifact fails the full-corpus\n"
    "// byte-equivalence test.\n"
    "struct ExpectedEsperRecord {\n"
    "    std::uint8_t rate1, spell1;   // ATTACK byte after its learn rate\n"
    "    std::uint8_t rate2, spell2;\n"
    "    std::uint8_t rate3, spell3;\n"
    "    std::uint8_t rate4, spell4;\n"
    "    std::uint8_t rate5, spell5;\n"
    "    std::uint8_t bonus;           // GENJU_BONUS byte\n"
    "};\n"
    "static_assert(sizeof(ExpectedEsperRecord) == 11,\n"
    "              \"fixture record must stay byte-identical to a ROM esper record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (the raw\n"
    "// decimal GENJU value — the fixture stays independent of the port's\n"
    "// EsperId header) alongside the raw record bytes. Mirrors\n"
    "// ostinato::EsperPropertiesEntry without depending on it.\n"
    "struct ExpectedEsperEntry {\n"
    "    std::uint8_t id;\n"
    "    ExpectedEsperRecord record;\n"
    "};\n"
)


def _fixture_row(rec):
    h = ["0x{:02X}".format(b) for b in rec.bytes]
    return ("    {{ .id = {},  // ${:02X} {}\n"
            "      .record = {{ .rate1 = {}, .spell1 = {}, .rate2 = {},"
            " .spell2 = {},\n"
            "                  .rate3 = {}, .spell3 = {}, .rate4 = {},"
            " .spell4 = {},\n"
            "                  .rate5 = {}, .spell5 = {}, .bonus = {} }} }},\n"
            ).format(rec.id_value, rec.id_value, rec.name, *h)


def _render_fixture(records):
    first = records[0]
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_esper_properties.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture id == position + kExpectedEsperFirstId, table\n"
             "// id enumerator == fixture id, and an 11-byte memcmp of the\n"
             "// packed record against src/data/generated/genju_prop_data.inc's\n"
             "// row.\n"
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
             "// The GENJU index space starts at ${:02X} ({}) — parser-derived\n"
             "// from const.inc, so the fixture's id arithmetic never\n"
             "// hand-types the base.\n".format(first.id_value, first.name),
             "inline constexpr std::uint8_t kExpectedEsperFirstId = {};\n"
             "\n".format(first.id_value),
             "inline constexpr std::array<ExpectedEsperEntry, {}> "
             "kExpectedEsperEntries = {{{{  // ROM GenjuProp\n".format(len(records))]
    for rec in records:
        lines.append(_fixture_row(rec))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ----------------------------------------------------------------

def run(genju_prop_asm, const_inc, inc_out, fixture_out, check_only=False):
    symbols = _Symbols(const_inc)
    records = parse_genju_prop(genju_prop_asm, symbols)

    if check_only:
        blank = sum(1 for r in records for s in r.spells if s.attack == "NONE")
        no_bonus = sum(1 for r in records if r.bonus == "NONE")
        print("OK: {} esper records; all 11 bytes each, GENJU index space "
              "contiguous; {} blank spell slots, {} bonus-less records."
              .format(len(records), blank, no_bonus))
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
    genju_prop = args.genju_prop_asm
    const_inc = args.const_inc
    if args.source_root:
        if not genju_prop:
            genju_prop = os.path.join(args.source_root, "src", "menu",
                                      "genju_prop.asm")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return genju_prop, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (genju_prop.asm + const.inc resolved under it)")
    ap.add_argument("--genju-prop-asm", help="path to genju_prop.asm")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out", default="src/data/generated/genju_prop_data.inc",
                    help="output path for the EsperPropertiesEntry rows")
    ap.add_argument("--fixture-out", default="tests/fixtures/genju_prop_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    genju_prop, const_inc = _resolve(args)
    if not genju_prop or not const_inc:
        ap.error("provide --source-root, or both --genju-prop-asm and --const-inc")
    try:
        return run(genju_prop, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
