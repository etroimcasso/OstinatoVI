#!/usr/bin/env python3
"""Emit the battle graphics trig tables from original-src btlgfx_main.asm.

Port-time tooling (NOT a build/CI dependency): reads `ArcTanTbl`, `SineTbl16`
and `SineTbl8` in src/btlgfx/btlgfx_main.asm and emits:

  * src/data/generated/arc_tangent_data.inc — 1,024 { .y, .x, .angle } rows
  * src/data/generated/sine16_data.inc       — 256 { .angle, .value } rows
  * src/data/generated/sine8_data.inc        — 256 { .angle, .value } rows
  * tests/fixtures/trig_tables_expected.h    — the same three tables as raw
    bytes and words (the ground-truth contract).

Angles are in 256ths of a turn. The values emitted are always the cartridge's;
the formulas upstream documents beside each table are checks, not sources:

  * ArcTanTbl (:42588-42590) — arctan(y / x) * 128 / pi "up to rounding
    errors", for x and y in 0..31. Every entry is round(...) or one below it;
    the number one below is pinned. Column x = 0 holds a quarter turn.
  * SineTbl16 (:48733-48742) — round(sin(2 pi i / 256) * 32767), "about 1/8"
    of entries off by one; the counts either side are pinned.
  * SineTbl8 (:48778-48783) — round(sin(2 pi i / 256) * 127), exact.

Structural guarantees, hard-errored at parse/emit time:
  * each table holds exactly its documented entry count;
  * each formula check holds with the pinned deviation counts;
  * all three tables are identical to the vanilla cartridge.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_trig_tables.py --source-root PATH
    parse_trig_tables.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import math
import os
import re
import sys

import common
from common import ParseError, strip_comment

ARC_TANGENT_SNES = 0xC2C945
SINE16_SNES = 0xC2FC6D
SINE8_SNES = 0xC2FE6D

ARC_TANGENT_SIDE = 32
ANGLES = 256
QUARTER_TURN = 64

# How many entries sit off the documented formula, measured on the corpus.
ARC_TANGENT_BELOW = 323
SINE16_BELOW = 13
SINE16_ABOVE = 18

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
_RE_LOCAL = re.compile(r"^@[0-9A-Za-z_]+:\s*")
_RE_DATA = re.compile(r"^\.(byte|word)\s+(.+)$")


def read_run(asm_path, label, directive):
    """The literal values following `label`, up to the next global label."""
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    values = []
    inside = False
    limit = 0xFF if directive == "byte" else 0xFFFF
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        found = _RE_LABEL.match(s)
        if found:
            if found.group(1) == label:
                inside = True
                continue
            if inside:
                break
            continue
        if not inside:
            continue
        s = _RE_LOCAL.sub("", s)
        data = _RE_DATA.match(s)
        if not data:
            raise ParseError(asm_path, lineno,
                             "unexpected line inside {}: '{}' (grammar not "
                             "covered — escalate, never guess)".format(label, s))
        if data.group(1) != directive:
            raise ParseError(asm_path, lineno,
                             "{} mixes .{} into a .{} table".format(
                                 label, data.group(1), directive))
        for term in data.group(2).split(","):
            value = common.parse_int_literal(term.strip())
            if value is None or value > limit:
                raise ParseError(asm_path, lineno,
                                 "'{}' is not a {}-bit literal".format(
                                     term.strip(), 8 if limit == 0xFF else 16))
            values.append(value)
    if not inside:
        raise ParseError(asm_path, 0, "{} not found".format(label))
    return values


def signed(value, bits):
    return value - (1 << bits) if value >= 1 << (bits - 1) else value


def arc_tangent_formula(y, x):
    if x == 0:
        return QUARTER_TURN
    return round(math.atan2(y, x) * 128 / math.pi)


def sine_formula(angle, amplitude):
    return round(math.sin(2 * math.pi * angle / ANGLES) * amplitude)


class Tables(object):
    def __init__(self, arc_tangent, sine16, sine8):
        self.arc_tangent = arc_tangent      # 1,024 bytes, row y then column x
        self.sine16 = sine16                # 256 raw words
        self.sine8 = sine8                  # 256 raw bytes


def check(tables, path):
    """Entry counts and the formula checks, with the pinned deviations."""
    if len(tables.arc_tangent) != ARC_TANGENT_SIDE * ARC_TANGENT_SIDE:
        raise ParseError(path, 0, "ArcTanTbl holds {} entries, not {}".format(
            len(tables.arc_tangent), ARC_TANGENT_SIDE * ARC_TANGENT_SIDE))
    for name, table in (("SineTbl16", tables.sine16),
                        ("SineTbl8", tables.sine8)):
        if len(table) != ANGLES:
            raise ParseError(path, 0, "{} holds {} entries, not {}".format(
                name, len(table), ANGLES))

    below = 0
    for y in range(ARC_TANGENT_SIDE):
        for x in range(ARC_TANGENT_SIDE):
            diff = (tables.arc_tangent[y * ARC_TANGENT_SIDE + x]
                    - arc_tangent_formula(y, x))
            if diff not in (0, -1):
                raise ParseError(path, 0,
                                 "ArcTanTbl ({}, {}) is {} from the formula"
                                 .format(y, x, diff))
            below += diff == -1
    if below != ARC_TANGENT_BELOW:
        raise ParseError(path, 0,
                         "ArcTanTbl has {} entries one below the formula, "
                         "expected {}".format(below, ARC_TANGENT_BELOW))

    counts = {-1: 0, 0: 0, 1: 0}
    for angle, raw in enumerate(tables.sine16):
        diff = signed(raw, 16) - sine_formula(angle, 32767)
        if diff not in counts:
            raise ParseError(path, 0, "SineTbl16[{}] is {} from the formula"
                             .format(angle, diff))
        counts[diff] += 1
    if (counts[-1], counts[1]) != (SINE16_BELOW, SINE16_ABOVE):
        raise ParseError(path, 0,
                         "SineTbl16 has {} entries below and {} above the "
                         "formula, expected {} and {}".format(
                             counts[-1], counts[1], SINE16_BELOW, SINE16_ABOVE))

    for angle, raw in enumerate(tables.sine8):
        if signed(raw, 8) != sine_formula(angle, 127):
            raise ParseError(path, 0, "SineTbl8[{}] differs from its formula"
                             .format(angle))


def assert_rom(rom, tables, path):
    words = []
    for word in tables.sine16:
        words += [word & 0xFF, word >> 8]
    for label, snes, want in (("ArcTanTbl", ARC_TANGENT_SNES, tables.arc_tangent),
                              ("SineTbl16", SINE16_SNES, words),
                              ("SineTbl8", SINE8_SNES, tables.sine8)):
        base = common.hirom_file_offset(snes)
        got = list(rom[base:base + len(want)])
        if got != list(want):
            raise ParseError(path, 0,
                             "ROM MISMATCH: {} at ${:06X} differs from the "
                             "assembled source".format(label, snes))


def read_tables(asm_path):
    tables = Tables(read_run(asm_path, "ArcTanTbl", "byte"),
                    read_run(asm_path, "SineTbl16", "word"),
                    read_run(asm_path, "SineTbl8", "byte"))
    check(tables, asm_path)
    return tables


# --- rendering -----------------------------------------------------------------

_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_trig_tables.py\n"
    "// Source: src/btlgfx/btlgfx_main.asm ({})\n"
    "// (original-src pinned at 1ea47b5; cross-checked against the vanilla ROM\n"
    "// over the whole table)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_trig_tables.py \\\n"
    "//       --source-root  original-src\n\n")


def render_arc_tangent_inc(tables):
    out = [_HEADER.format("ArcTanTbl, 32 x 32 bytes, ROM $C2/C945"),
           "// ArcTangentEntry rows, row y then column x, #included inside the\n"
           "// kArcTangent array in src/data/trig_tables.cpp. .angle is in 256ths\n"
           "// of a turn: arctan(y / x) rounded, or one below it; x = 0 is a\n"
           "// quarter turn. A compile-time assert verifies y * 32 + x ==\n"
           "// position.\n\n"]
    for y in range(ARC_TANGENT_SIDE):
        for x in range(ARC_TANGENT_SIDE):
            out.append("    {{ .y = {:>2}, .x = {:>2}, .angle = {:>2} }},\n".format(
                y, x, tables.arc_tangent[y * ARC_TANGENT_SIDE + x]))
    return "".join(out)


def render_sine_inc(values, bits, label, snes_text, array):
    out = [_HEADER.format("{}, 256 entries, ROM {}".format(label, snes_text)),
           "// Sine{}Entry rows in angle order (256ths of a turn), #included\n"
           "// inside the {} array in src/data/trig_tables.cpp. .value is\n"
           "// signed; a compile-time assert verifies angle == position.\n\n"
           .format(bits, array)]
    width = 6 if bits == 16 else 4
    for angle, raw in enumerate(values):
        out.append("    {{ .angle = {:>3}, .value = {:>{w}} }},\n".format(
            angle, signed(raw, bits), w=width))
    return "".join(out)


def render_fixture(tables):
    out = [_HEADER.format("ArcTanTbl, SineTbl16, SineTbl8"),
           "// Test fixture for tests/test_trig_tables.cpp — the raw cartridge\n"
           "// values, deliberately independent of the signed rows in the\n"
           "// generated tables.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"
           "// One raw byte or word, at its position in its table.\n"
           "struct ExpectedTrigByte {\n"
           "    std::uint16_t index;\n"
           "    std::uint8_t value;\n"
           "};\n\n"
           "struct ExpectedTrigWord {\n"
           "    std::uint16_t index;\n"
           "    std::uint16_t value;\n"
           "};\n\n"
           "inline constexpr std::array<ExpectedTrigByte, 1024>\n"
           "    kExpectedArcTangent = {{\n"]
    for index, value in enumerate(tables.arc_tangent):
        out.append("    {{ .index = {:>4}, .value = 0x{:02X} }},\n".format(index, value))
    out.append("}};\n\ninline constexpr std::array<ExpectedTrigWord, 256>\n"
               "    kExpectedSine16 = {{\n")
    for index, value in enumerate(tables.sine16):
        out.append("    {{ .index = {:>3}, .value = 0x{:04X} }},\n".format(index, value))
    out.append("}};\n\ninline constexpr std::array<ExpectedTrigByte, 256>\n"
               "    kExpectedSine8 = {{\n")
    for index, value in enumerate(tables.sine8):
        out.append("    {{ .index = {:>3}, .value = 0x{:02X} }},\n".format(index, value))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver --------------------------------------------------------------------

def run(source_root, outputs, check_only=False):
    asm_path = os.path.join(source_root, "src", "btlgfx", "btlgfx_main.asm")
    tables = read_tables(asm_path)
    assert_rom(common.load_vanilla_rom(source_root), tables, asm_path)
    if check_only:
        print("OK: arctangent 32x32, sine 16-bit and 8-bit x 256, formula checks "
              "hold, cartridge-identical.")
        return 0
    _write(outputs["arc_tangent"], render_arc_tangent_inc(tables))
    _write(outputs["sine16"], render_sine_inc(
        tables.sine16, 16, "SineTbl16", "$C2/FC6D", "kSine16"))
    _write(outputs["sine8"], render_sine_inc(
        tables.sine8, 8, "SineTbl8", "$C2/FE6D", "kSine8"))
    _write(outputs["fixture"], render_fixture(tables))
    for key in ("arc_tangent", "sine16", "sine8", "fixture"):
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
    ap.add_argument("--arc-tangent-out",
                    default="src/data/generated/arc_tangent_data.inc")
    ap.add_argument("--sine16-out", default="src/data/generated/sine16_data.inc")
    ap.add_argument("--sine8-out", default="src/data/generated/sine8_data.inc")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/trig_tables_expected.h")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)
    outputs = {"arc_tangent": args.arc_tangent_out, "sine16": args.sine16_out,
               "sine8": args.sine8_out, "fixture": args.fixture_out}
    try:
        return run(args.source_root, outputs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
