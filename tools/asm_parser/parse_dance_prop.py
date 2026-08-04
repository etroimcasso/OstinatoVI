#!/usr/bin/env python3
"""Emit the dance attack table from original-src dance_prop.asm.

Port-time tooling (NOT a build/CI dependency): interprets the ca65 macro DSL in
src/battle/dance_prop.asm and emits:

  * src/data/generated/dance_prop_data.inc — one designated-initializer
    DancePropertiesEntry row per dance (8 records), identity as the DanceId
    enumerator field; the kDanceProperties array #includes it.
  * tests/fixtures/dance_prop_expected.h   — the same 8 records as raw 4-byte
    rows (the ground-truth byte contract) for a full-corpus byte-equivalence
    test.

The DSL: after the `DanceProp:` label, exactly one `make_dance_prop a, b, c, d`
invocation per dance, each argument a known ATTACK member; the macro emits the
four attack bytes in argument order (the invocation IS the record). The macro
*definition* at the top of the file is skipped; this parser models its
documented effect. Slot order is the consumer's probability-tier order
(7/16, 3/8, 1/8, 1/16 — RandDance in src/battle/battle_main.asm); the rate
table itself is battle-logic data outside this table.

Symbol values resolve against original-src/include/const.inc (ATTACK for the
record bytes, DANCE for the identity names); an unknown symbol is a hard error
with a file:line citation so the executor escalates rather than guessing.

Structural guarantees, hard-errored at emit time:
  * the `DanceProp:` label anchors the table (structural anchor);
  * every record emits exactly 4 bytes, each in 0..255;
  * the record count equals the DANCE index space (8), whose values must be
    contiguous from 0 so record index == DanceId value.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_dance_prop.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_dance_prop.py --dance-prop-asm PATH --const-inc PATH \\
                        --inc-out FILE --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import sys

import common
import parse_const_enums as pce
from common import ParseError, strip_comment


# The four probability tiers, in slot order (RandDance's DanceRateTbl
# semantics) — rendered as per-slot comments so no slot is positionally opaque.
_SLOT_RATES = ("7/16", "3/8", "1/8", "1/16")


class Record(object):
    """One dance: its four ATTACK member names, resolved bytes, and identity."""

    def __init__(self, index, attacks, attack_bytes):
        self.index = index
        self.attacks = attacks          # list[str] of ATTACK member names, len 4
        self.bytes = attack_bytes       # list[int], len 4
        self.name = None                # DANCE member name, filled by index


class _Symbols(object):
    """The const.inc enums the DSL resolves against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        for enum_name in ("ATTACK", "DANCE"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(enum_name))

    def attack_value(self, member, path, lineno):
        val = self.parsed.enum("ATTACK").value_of(member)
        if val is None:
            raise ParseError(path, lineno,
                             "unknown ATTACK::{}".format(member))
        return val


def parse_dance_prop(asm_path, symbols):
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    records = []
    macro_depth = 0
    label_seen = False

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

        # --- the structural anchor ---
        if s == "DanceProp:":
            if label_seen:
                raise ParseError(asm_path, lineno, "duplicate DanceProp label")
            label_seen = True
            continue

        # --- record invocations ---
        parts = s.split(None, 1)
        if parts[0] == "make_dance_prop":
            if not label_seen:
                raise ParseError(asm_path, lineno,
                                 "make_dance_prop before the DanceProp label")
            args = [a.strip() for a in parts[1].split(",")] if len(parts) > 1 else []
            if len(args) != 4 or any(not a for a in args):
                raise ParseError(asm_path, lineno,
                                 "make_dance_prop expects 4 args, got {}"
                                 .format(len(args)))
            attack_bytes = [symbols.attack_value(a, asm_path, lineno)
                            for a in args]
            records.append(Record(len(records), args, attack_bytes))
            continue

        raise ParseError(asm_path, lineno,
                         "unrecognized line: '{}' (grammar not covered — "
                         "escalate, never guess)".format(s))

    if not label_seen:
        raise ParseError(asm_path, len(lines),
                         "DanceProp label not found (structural anchor missing)")
    _assign_names_and_verify(records, symbols, asm_path)
    return records


def _assign_names_and_verify(records, symbols, path):
    dance = symbols.parsed.enum("DANCE")
    expected_count = len(dance.members)
    if len(records) != expected_count:
        raise ParseError(path, 0,
                         "dance_prop produced {} records; DANCE index space is "
                         "{}".format(len(records), expected_count))
    for i, m in enumerate(dance.members):
        if m.value != i:
            raise ParseError(path, 0,
                             "DANCE::{} = {} breaks the contiguous-from-0 index "
                             "space (record index == DanceId value assumption)"
                             .format(m.name, m.value))
    for rec in records:
        rec.name = dance.members[rec.index].name
        for b in rec.bytes:
            if not (0 <= b <= 0xFF):
                raise ParseError(path, 0,
                                 "byte value {} out of range 0..255".format(b))


# --- rendering -------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_dance_prop.py\n"
    "// Source: src/battle/dance_prop.asm (DanceProp, 8 dances x 4 ATTACK bytes)\n"
    "// Source: include/const.inc (ATTACK / DANCE values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_dance_prop.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/dance_prop_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/dance_prop_expected.h\n"
    "\n"
)


def _render_inc(records):
    lines = [_HEADER_COMMON,
             "// DancePropertiesEntry rows in DANCE index order ($00..$07),\n"
             "// one designated-initializer row per dance, #included inside\n"
             "// the kDanceProperties array in src/data/dance_properties.h.\n"
             "// Each row's identity is its .id field — the DanceId enumerator;\n"
             "// a compile-time assert verifies id == position. The packed\n"
             "// .record stays byte-identical to the 4 ROM bytes. Slot order is\n"
             "// the consumer's probability-tier order (the per-slot comments);\n"
             "// the rate values themselves live with the battle logic, not\n"
             "// here.\n\n"]
    for rec in records:
        lines.append("    DancePropertiesEntry{{  // [${:02X}]\n".format(rec.index))
        lines.append("        .id = DanceId::{},\n".format(rec.name))
        lines.append("        .record = DanceProperties{ .attacks = {\n")
        width = max(len(a) for a in rec.attacks)
        for slot, attack in enumerate(rec.attacks):
            lines.append("            AttackId::{},{}  // slot {} ({})\n".format(
                attack, " " * (width - len(attack)), slot, _SLOT_RATES[slot]))
        lines.append("        } },\n")
        lines.append("    },\n")
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 4-byte dance record: the four ATTACK bytes in slot order\n"
    "// (probability tiers 7/16, 3/8, 1/8, 1/16). Values are the exact ROM\n"
    "// bytes with every upstream symbol resolved — deliberately independent\n"
    "// of the enum-symbol rows in dance_prop_data.inc, so symbol/value drift\n"
    "// in either artifact fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedDanceRecord {\n"
    "    std::uint8_t attack1, attack2, attack3, attack4;  // ATTACK bytes\n"
    "};\n"
    "static_assert(sizeof(ExpectedDanceRecord) == 4,\n"
    "              \"fixture record must stay byte-identical to a ROM dance record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (raw decimal\n"
    "// index — the fixture stays independent of the port's DanceId header)\n"
    "// alongside the raw record bytes. Mirrors ostinato::DancePropertiesEntry\n"
    "// without depending on it.\n"
    "struct ExpectedDanceEntry {\n"
    "    std::uint8_t id;\n"
    "    ExpectedDanceRecord record;\n"
    "};\n"
)


def _fixture_row(rec):
    h = ["0x{:02X}".format(b) for b in rec.bytes]
    return ("    {{ .id = {},  // ${:02X} {}\n"
            "      .record = {{ .attack1 = {}, .attack2 = {},"
            " .attack3 = {}, .attack4 = {} }} }},\n").format(
                rec.index, rec.index, rec.name, *h)


def _render_fixture(records):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_dance_properties.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture id == position, table id enumerator ==\n"
             "// position, and a 4-byte memcmp of the packed record against\n"
             "// src/data/generated/dance_prop_data.inc's row.\n"
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
             "inline constexpr std::array<ExpectedDanceEntry, {}> "
             "kExpectedDanceEntries = {{{{  // ROM DanceProp\n".format(len(records))]
    for rec in records:
        lines.append(_fixture_row(rec))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ----------------------------------------------------------------

def run(dance_prop_asm, const_inc, inc_out, fixture_out, check_only=False):
    symbols = _Symbols(const_inc)
    records = parse_dance_prop(dance_prop_asm, symbols)

    if check_only:
        print("OK: {} dance records; all 4 bytes each, DANCE index space "
              "contiguous.".format(len(records)))
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
    dance_prop = args.dance_prop_asm
    const_inc = args.const_inc
    if args.source_root:
        if not dance_prop:
            dance_prop = os.path.join(args.source_root, "src", "battle",
                                      "dance_prop.asm")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return dance_prop, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (dance_prop.asm + const.inc resolved under it)")
    ap.add_argument("--dance-prop-asm", help="path to dance_prop.asm")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out", default="src/data/generated/dance_prop_data.inc",
                    help="output path for the DancePropertiesEntry rows")
    ap.add_argument("--fixture-out", default="tests/fixtures/dance_prop_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    dance_prop, const_inc = _resolve(args)
    if not dance_prop or not const_inc:
        ap.error("provide --source-root, or both --dance-prop-asm and --const-inc")
    try:
        return run(dance_prop, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
