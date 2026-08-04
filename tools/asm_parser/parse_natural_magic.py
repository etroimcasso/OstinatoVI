#!/usr/bin/env python3
"""Emit the natural-magic tables from original-src event.asm.

Port-time tooling (NOT a build/CI dependency): reads the NaturalMagic block in
src/field/event.asm and emits, per the Phase-1.B PLAN (D6 + Amendment B1):

  * src/data/generated/natural_magic_data.inc — the kNaturalMagicTerra and
    kNaturalMagicCeles array definitions (16 designated-initializer rows each,
    slot identity as a typed field), consumed at namespace scope by
    src/data/natural_magic.h.
  * tests/fixtures/natural_magic_expected.h  — the same 2x16 records as raw
    2-byte rows (the ground-truth byte contract) for a full-corpus
    byte-equivalence test.

The grammar: event.asm is a large code file; the parser anchors on the
`.segment "natural_magic"` directive immediately followed by the
`NaturalMagic:` label, then requires exactly 32 `.byte ATTACK::NAME, level`
lines closed by `.popseg` — anything else inside the anchored region is a hard
error. Everything outside the region is event code, not table content, and is
skipped without evaluation. ROM byte order within a pair is **spell first,
level second** (the opposite of the esper table's pairs — do not conflate).
The first 16 pairs are Terra's list, the last 16 Celes's (the consumers read
Celes's half at NaturalMagic+$20). Celes's out-of-sorted-order MUDDLE-at-32
entry after BSERK-at-40 is contract — ported byte-verbatim, never re-sorted.

Symbol values resolve against original-src/include/const.inc (ATTACK); an
unknown symbol is a hard error with a file:line citation so the executor
escalates rather than guessing.

Structural guarantees, hard-errored at emit time:
  * the segment + label anchor must exist exactly once;
  * exactly 32 pairs, split 16/16, every byte in 0..255;
  * the region terminates with .popseg right after the 32nd pair.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_natural_magic.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_natural_magic.py --event-asm PATH --const-inc PATH \\
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


_RE_PAIR = re.compile(
    r"^\.byte\s+ATTACK::([A-Za-z_][A-Za-z0-9_]*)\s*,\s*(\S+)$")

_PAIR_COUNT = 32          # 16 Terra + 16 Celes (consumer reads Celes at +$20)
_HALF = 16


class Pair(object):
    """One {spell, level} pair: the ATTACK member name, level, and bytes."""

    def __init__(self, slot, attack, level, attack_byte):
        self.slot = slot            # 0..15 within its character's half
        self.attack = attack        # ATTACK member name
        self.level = level          # int
        self.bytes = [attack_byte, level]  # ROM order: spell first, level second


class _Symbols(object):
    """The const.inc ATTACK enum the pairs resolve against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        if self.parsed.enum("ATTACK") is None:
            raise ParseError(const_inc, 0, "expected enum 'ATTACK' not found")

    def attack_value(self, member, path, lineno):
        val = self.parsed.enum("ATTACK").value_of(member)
        if val is None:
            raise ParseError(path, lineno,
                             "unknown ATTACK::{}".format(member))
        return val


def parse_natural_magic(event_asm, symbols):
    """Returns (terra_pairs, celes_pairs), each list[Pair] of length 16."""
    with open(event_asm, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    # State machine: 0 = scanning for the segment, 1 = expecting the label,
    # 2 = collecting pairs, 3 = done.
    state = 0
    pairs = []

    for idx, raw in enumerate(lines):
        lineno = idx + 1
        code, _comment = strip_comment(raw)
        if not code:
            continue
        s = code.strip()

        if state == 0:
            if s == '.segment "natural_magic"':
                state = 1
            continue

        if state == 1:
            if s == "NaturalMagic:":
                state = 2
                continue
            raise ParseError(event_asm, lineno,
                             "expected 'NaturalMagic:' right after the "
                             "natural_magic segment, got '{}'".format(s))

        if state == 2:
            if len(pairs) == _PAIR_COUNT:
                if s == ".popseg":
                    state = 3
                    continue
                raise ParseError(event_asm, lineno,
                                 "expected .popseg after {} pairs, got '{}'"
                                 .format(_PAIR_COUNT, s))
            m = _RE_PAIR.match(s)
            if not m:
                raise ParseError(event_asm, lineno,
                                 "unrecognized line inside NaturalMagic: '{}' "
                                 "(expected '.byte ATTACK::NAME, level')"
                                 .format(s))
            attack, level_tok = m.group(1), m.group(2)
            level = parse_int_literal(level_tok)
            if level is None:
                raise ParseError(event_asm, lineno,
                                 "level '{}' is not an integer literal"
                                 .format(level_tok))
            if not (0 <= level <= 0xFF):
                raise ParseError(event_asm, lineno,
                                 "level {} out of range 0..255".format(level))
            attack_byte = symbols.attack_value(attack, event_asm, lineno)
            pairs.append(Pair(len(pairs) % _HALF, attack, level, attack_byte))
            continue

        if state == 3:
            if s == '.segment "natural_magic"':
                raise ParseError(event_asm, lineno,
                                 "duplicate natural_magic segment")
            continue

    if state == 0:
        raise ParseError(event_asm, len(lines),
                         'natural_magic segment not found (structural anchor '
                         'missing)')
    if state != 3:
        raise ParseError(event_asm, len(lines),
                         "NaturalMagic block truncated: got {} pairs, "
                         "expected {} + .popseg".format(len(pairs), _PAIR_COUNT))
    return pairs[:_HALF], pairs[_HALF:]


# --- rendering -------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_natural_magic.py\n"
    "// Source: src/field/event.asm (NaturalMagic, ROM EC/E3C0,\n"
    "//         Terra's 16 {spell, level} pairs then Celes's 16, contiguous)\n"
    "// Source: include/const.inc (ATTACK values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_natural_magic.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/natural_magic_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/natural_magic_expected.h\n"
    "\n"
)


def _inc_rows(pairs):
    lines = []
    width = max(len(p.attack) for p in pairs)
    for p in pairs:
        lines.append(
            "    NaturalMagicSlot{{ .slot = {:>2},\n"
            "                      .record = NaturalMagicEntry{{ "
            ".spell = AttackId::{},{} .level = {:>2} }} }},\n".format(
                p.slot, p.attack, " " * (width - len(p.attack)), p.level))
    return "".join(lines)


def _render_inc(terra, celes):
    return "".join([
        _HEADER_COMMON,
        "// The two natural-magic tables (16 NaturalMagicSlot rows each),\n"
        "// consumed at namespace scope by src/data/natural_magic.h. Each\n"
        "// row's identity is its .slot field (decimal position within the\n"
        "// character's half — no index enum exists for the slots); a\n"
        "// compile-time assert verifies slot == position. The packed .record\n"
        "// stays byte-identical to the 2 ROM bytes; within each pair the ROM\n"
        "// byte order is spell first, level second (the opposite of the\n"
        "// esper table's pairs). Character->half selection is consumer\n"
        "// logic, so the halves are two named tables rather than one\n"
        "// dispatched surface. Celes's MUDDLE-at-32 row after BSERK-at-40\n"
        "// is the ROM's own out-of-sorted-order entry, ported verbatim.\n"
        "\n",
        "inline constexpr std::array<NaturalMagicSlot, 16> "
        "kNaturalMagicTerra = {{\n",
        _inc_rows(terra),
        "}};\n"
        "\n",
        "inline constexpr std::array<NaturalMagicSlot, 16> "
        "kNaturalMagicCeles = {{\n",
        _inc_rows(celes),
        "}};\n",
    ])


_FIXTURE_STRUCT = (
    "// One raw 2-byte natural-magic pair in ROM order: the ATTACK byte then\n"
    "// the learn level. Values are the exact ROM bytes with every upstream\n"
    "// symbol resolved — deliberately independent of the enum-symbol rows in\n"
    "// natural_magic_data.inc, so symbol/value drift in either artifact\n"
    "// fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedNaturalMagicRecord {\n"
    "    std::uint8_t spell;   // ATTACK byte\n"
    "    std::uint8_t level;\n"
    "};\n"
    "static_assert(sizeof(ExpectedNaturalMagicRecord) == 2,\n"
    "              \"fixture record must stay byte-identical to a ROM natural-magic pair\");\n"
    "\n"
    "// One fixture entry: the pair's identity as a typed field (its decimal\n"
    "// slot within the character's half) alongside the raw record bytes.\n"
    "// Mirrors ostinato::NaturalMagicSlot without depending on it.\n"
    "struct ExpectedNaturalMagicSlot {\n"
    "    std::uint8_t slot;\n"
    "    ExpectedNaturalMagicRecord record;\n"
    "};\n"
)


def _fixture_rows(pairs, array_name):
    lines = ["inline constexpr std::array<ExpectedNaturalMagicSlot, 16> "
             "{} = {{{{\n".format(array_name)]
    for p in pairs:
        lines.append("    {{ .slot = {:>2}, .record = {{ .spell = 0x{:02X}, "
                     ".level = 0x{:02X} }} }},  // {}\n".format(
                         p.slot, p.bytes[0], p.bytes[1], p.attack))
    lines.append("}};\n")
    return "".join(lines)


def _render_fixture(terra, celes):
    return "".join([
        _HEADER_COMMON,
        "// Test fixture for tests/test_natural_magic.cpp — the ground-truth\n"
        "// record bytes. The full-corpus test asserts, per entry of both\n"
        "// halves: fixture slot == position, table slot == position, and a\n"
        "// 2-byte memcmp of the packed record against\n"
        "// src/data/generated/natural_magic_data.inc's row.\n"
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
        _fixture_rows(terra, "kExpectedNaturalMagicTerra"),
        "\n",
        _fixture_rows(celes, "kExpectedNaturalMagicCeles"),
        "\n}  // namespace ostinato::test\n",
    ])


# --- driver ----------------------------------------------------------------

def run(event_asm, const_inc, inc_out, fixture_out, check_only=False):
    symbols = _Symbols(const_inc)
    terra, celes = parse_natural_magic(event_asm, symbols)

    if check_only:
        print("OK: {} + {} natural-magic pairs (Terra + Celes); spell-first "
              "byte order preserved.".format(len(terra), len(celes)))
        return 0

    _write(inc_out, _render_inc(terra, celes))
    _write(fixture_out, _render_fixture(terra, celes))
    print("Emitted {}+{} pairs -> {}".format(len(terra), len(celes), inc_out))
    print("Emitted fixture -> {}".format(fixture_out))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def _resolve(args):
    event_asm = args.event_asm
    const_inc = args.const_inc
    if args.source_root:
        if not event_asm:
            event_asm = os.path.join(args.source_root, "src", "field",
                                     "event.asm")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return event_asm, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (event.asm + const.inc resolved under it)")
    ap.add_argument("--event-asm", help="path to event.asm")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out", default="src/data/generated/natural_magic_data.inc",
                    help="output path for the two table definitions")
    ap.add_argument("--fixture-out", default="tests/fixtures/natural_magic_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    event_asm, const_inc = _resolve(args)
    if not event_asm or not const_inc:
        ap.error("provide --source-root, or both --event-asm and --const-inc")
    try:
        return run(event_asm, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
