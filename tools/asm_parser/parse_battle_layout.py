#!/usr/bin/env python3
"""Emit where the party stands and which way it faces at the start of a battle.

Port-time tooling (NOT a build/CI dependency). The battle graphics code places
the party by battle type from four short tables (btlgfx/btlgfx_main.asm):

  * CharDirTbl (:45277) — which way each party member faces: 0 left, 1 right
    (UpdateFacingDir :39958 reads it as "0 = left, 1 = right").
  * CharFlipTbl (:45268) — the sprite's horizontal flip, $40 where the member
    faces right. It sits right before CharDirTbl and says the same thing; the
    parser proves that and emits nothing from it.
  * CharXAngleTbl (:42816) — where each member stands across the field, as an
    angle from a point above the screen: $0100 is straight down the middle,
    above it right of center, below it left (CalcCharXPos :8171).
  * CharRowOffsetTbl (:42801) — what the back row adds to that angle.
  * CharYOffsetTbl (:42808) — how far down the screen each member stands; the
    fifth row is the magitek train car's (InitCharGfxMain :8203).

Emitted artifacts:

  * party layout rows  src/data/generated/party_layout_data.inc — per battle
    type, each member's facing and slope (the angle's distance from $0100),
    #included inside kPartyLayout in src/battle/battle_layout.h.
  * fixture            tests/fixtures/party_layout_expected.h — all four
    tables as the cartridge holds them, independent of the typed rows.

Structural guarantees, hard-errored at emit time:
  * each table holds one row of four per battle type (five for the y offsets),
    directly after its label, in the grammar it is written in;
  * the row offsets, y offsets and angles follow each other in that order with
    nothing between them, and so do the flips and the facings;
  * every facing is 0 or 1, every flip is $00 or $40, and a member's flip is
    $40 exactly where it faces right;
  * every angle is below $0200 (CalcCharXPos masks it with $01FF), and a member
    standing off the middle faces toward it;
  * both blocks occur exactly once in the bank the bank map loads their segment
    into, byte for byte.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_battle_layout.py --source-root PATH
    parse_battle_layout.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError, strip_comment
from parse_monster_sprites import read_segment_bank

# The battle types in the order the tables index them by ($201f): the names the
# port's BattleType enum gives them.
BATTLE_TYPES = ("Front", "Back", "Pincer", "Side")
PARTY_SLOTS = 4
Y_OFFSET_ROWS = len(BATTLE_TYPES) + 1      # the fifth row: the magitek train car

FACING_LEFT = 0
FACING_RIGHT = 1
FLIP_NONE = 0x00
FLIP_HORIZONTAL = 0x40

ANGLE_MIDDLE = 0x0100                      # straight down the middle
ANGLE_LIMIT = 0x0200                       # CalcCharXPos: `and #$01ff`

BANK_SIZE = 0x10000

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
_RE_LOCAL = re.compile(r"^@[0-9a-f]{4}:\s*")
_RE_DATA = re.compile(r"^\.(byte|word)\s+(.+)$")
_RE_SEGMENT = re.compile(r'^\.segment\s+"([^"]+)"$')


class Table(object):
    """One table: its label's line, its values, and the directive they use."""

    def __init__(self, label, line, directive, values, end_line):
        self.label = label
        self.line = line
        self.directive = directive   # "byte" or "word"
        self.values = values
        self.end_line = end_line     # the last line holding one of its values

    def rows(self, width=PARTY_SLOTS):
        return [self.values[i:i + width]
                for i in range(0, len(self.values), width)]

    def bytes(self):
        if self.directive == "byte":
            return list(self.values)
        out = []
        for value in self.values:
            out += [value & 0xFF, (value >> 8) & 0xFF]
        return out


def read_lines(source_root):
    path = os.path.join(source_root, "src", "btlgfx", "btlgfx_main.asm")
    if not os.path.isfile(path):
        raise ParseError(path, 0, "source file not found")
    with open(path, "r", encoding="utf-8") as fh:
        return path, fh.readlines()


def read_table(path, lines, label):
    """The values written after `label`, up to the first line that is not a
    data line. Every value line must be a .byte or .word line of literals, all
    with the same directive."""
    start = None
    for index, raw in enumerate(lines):
        code, _ = strip_comment(raw)
        found = _RE_LABEL.match(code.strip())
        if found is not None and found.group(1) == label:
            start = index
            break
    if start is None:
        raise ParseError(path, 0, "{} not found".format(label))

    directive = None
    values = []
    end_line = start + 1
    for index in range(start + 1, len(lines)):
        code, _ = strip_comment(lines[index])
        code = _RE_LOCAL.sub("", code.strip())
        if not code:
            break
        data = _RE_DATA.match(code)
        if data is None:
            raise ParseError(path, index + 1,
                             "unexpected line inside {}: {!r} (grammar not "
                             "covered — escalate, never guess)"
                             .format(label, code))
        if directive is None:
            directive = data.group(1)
        elif data.group(1) != directive:
            raise ParseError(path, index + 1,
                             "{} mixes .byte and .word".format(label))
        limit = 0xFF if directive == "byte" else 0xFFFF
        for term in data.group(2).split(","):
            value = common.parse_int_literal(term.strip())
            if value is None or not 0 <= value <= limit:
                raise ParseError(path, index + 1,
                                 "{!r} is not a .{} literal"
                                 .format(term.strip(), directive))
            values.append(value)
        end_line = index + 1
    if not values:
        raise ParseError(path, start + 1, "{} holds no values".format(label))
    return Table(label=label, line=start + 1, directive=directive,
                 values=values, end_line=end_line)


def check_adjacent(path, lines, first, second):
    """Nothing but blank and comment lines between `first` and `second`."""
    if second.line <= first.end_line:
        raise ParseError(path, second.line, "{} is not after {}"
                         .format(second.label, first.label))
    for lineno in range(first.end_line + 1, second.line):
        code, _ = strip_comment(lines[lineno - 1])
        if code.strip():
            raise ParseError(path, lineno,
                             "{!r} sits between {} and {}"
                             .format(code.strip(), first.label, second.label))


def segment_of(path, lines, table):
    """The segment `table` is assembled into: the last .segment above it."""
    segment = None
    for index in range(table.line - 1):
        code, _ = strip_comment(lines[index])
        found = _RE_SEGMENT.match(code.strip())
        if found is not None:
            segment = found.group(1)
    if segment is None:
        raise ParseError(path, table.line, "no .segment above {}"
                         .format(table.label))
    return segment


def signed(word):
    return word - 0x10000 if word & 0x8000 else word


class Layout(object):
    def __init__(self, path, row_offsets, y_offsets, angles, flips, facings):
        self.path = path
        self.row_offsets = row_offsets
        self.y_offsets = y_offsets
        self.angles = angles
        self.flips = flips
        self.facings = facings

    def slopes(self):
        """Each member's angle as a distance from the middle, row by row."""
        return [[value - ANGLE_MIDDLE for value in row]
                for row in self.angles.rows()]


def check(layout):
    path = layout.path
    for table, rows in ((layout.row_offsets, len(BATTLE_TYPES)),
                        (layout.angles, len(BATTLE_TYPES)),
                        (layout.flips, len(BATTLE_TYPES)),
                        (layout.facings, len(BATTLE_TYPES)),
                        (layout.y_offsets, Y_OFFSET_ROWS)):
        if len(table.values) != rows * PARTY_SLOTS:
            raise ParseError(path, table.line,
                             "{} holds {} values, not {} rows of {}"
                             .format(table.label, len(table.values), rows,
                                     PARTY_SLOTS))
    for table, directive in ((layout.row_offsets, "word"),
                             (layout.angles, "word"),
                             (layout.y_offsets, "byte"),
                             (layout.flips, "byte"),
                             (layout.facings, "byte")):
        if table.directive != directive:
            raise ParseError(path, table.line, "{} is not a .{} table"
                             .format(table.label, directive))

    for row, (facings, flips, angles) in enumerate(zip(
            layout.facings.rows(), layout.flips.rows(), layout.angles.rows())):
        for slot in range(PARTY_SLOTS):
            facing, flip, angle = facings[slot], flips[slot], angles[slot]
            where = "{} row {} slot {}".format(BATTLE_TYPES[row], row, slot)
            if facing not in (FACING_LEFT, FACING_RIGHT):
                raise ParseError(path, layout.facings.line,
                                 "CharDirTbl {}: {} is not 0 or 1"
                                 .format(where, facing))
            if flip not in (FLIP_NONE, FLIP_HORIZONTAL):
                raise ParseError(path, layout.flips.line,
                                 "CharFlipTbl {}: ${:02X} is not $00 or $40"
                                 .format(where, flip))
            if (flip == FLIP_HORIZONTAL) != (facing == FACING_RIGHT):
                raise ParseError(path, layout.flips.line,
                                 "CharFlipTbl {} disagrees with CharDirTbl"
                                 .format(where))
            if angle >= ANGLE_LIMIT:
                raise ParseError(path, layout.angles.line,
                                 "CharXAngleTbl {}: ${:04X} is past $01FF"
                                 .format(where, angle))
            if angle > ANGLE_MIDDLE and facing != FACING_LEFT:
                raise ParseError(path, layout.angles.line,
                                 "CharXAngleTbl {}: right of the middle but "
                                 "facing right".format(where))
            if angle < ANGLE_MIDDLE and facing != FACING_RIGHT:
                raise ParseError(path, layout.angles.line,
                                 "CharXAngleTbl {}: left of the middle but "
                                 "facing left".format(where))


def read_layout(source_root):
    path, lines = read_lines(source_root)
    tables = {label: read_table(path, lines, label)
              for label in ("CharRowOffsetTbl", "CharYOffsetTbl",
                            "CharXAngleTbl", "CharFlipTbl", "CharDirTbl")}
    check_adjacent(path, lines, tables["CharRowOffsetTbl"],
                   tables["CharYOffsetTbl"])
    check_adjacent(path, lines, tables["CharYOffsetTbl"],
                   tables["CharXAngleTbl"])
    check_adjacent(path, lines, tables["CharFlipTbl"], tables["CharDirTbl"])
    layout = Layout(path=path,
                    row_offsets=tables["CharRowOffsetTbl"],
                    y_offsets=tables["CharYOffsetTbl"],
                    angles=tables["CharXAngleTbl"],
                    flips=tables["CharFlipTbl"],
                    facings=tables["CharDirTbl"])
    check(layout)
    segments = {segment_of(path, lines, table) for table in tables.values()}
    if len(segments) != 1:
        raise ParseError(path, 0, "the tables span segments {}"
                         .format(sorted(segments)))
    return layout, segments.pop()


# --- the cartridge -------------------------------------------------------------

def place_block(rom, bank_at, block, label, path):
    """The address of `block`, which must occur exactly once in the bank."""
    base = common.hirom_file_offset(bank_at)
    bank = bytes(rom[base:base + BANK_SIZE])
    needle = bytes(block)
    found = []
    at = bank.find(needle)
    while at != -1:
        found.append(at)
        at = bank.find(needle, at + 1)
    if len(found) != 1:
        raise ParseError(path, 0, "{} occurs {} times in bank ${:02X}, "
                         "expected exactly once"
                         .format(label, len(found), bank_at >> 16))
    return bank_at | found[0]


def place_layout(layout, rom, bank_at):
    """Where both blocks sit: (positions block, facing block)."""
    positions = place_block(
        rom, bank_at,
        layout.row_offsets.bytes() + layout.y_offsets.bytes()
        + layout.angles.bytes(),
        "CharRowOffsetTbl + CharYOffsetTbl + CharXAngleTbl", layout.path)
    facings = place_block(
        rom, bank_at, layout.flips.bytes() + layout.facings.bytes(),
        "CharFlipTbl + CharDirTbl", layout.path)
    return positions, facings


# --- rendering -----------------------------------------------------------------

_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_battle_layout.py\n"
    "// Source: src/btlgfx/btlgfx_main.asm ({})\n"
    "// (original-src pinned at 1ea47b5; cross-checked against the vanilla ROM)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_battle_layout.py \\\n"
    "//       --source-root  original-src\n\n")


def render_party_layout_inc(layout):
    out = [_HEADER.format("CharDirTbl, CharXAngleTbl"),
           "// PartyLayoutRow rows in battle-type order, #included inside the\n"
           "// kPartyLayout array in src/battle/battle_layout.h. Each row names its\n"
           "// battle type; a compile-time assert verifies it matches the row's\n"
           "// position. A member's slope is how far its place leans from the\n"
           "// middle of the field: above zero right of it, below zero left, zero\n"
           "// on it.\n\n"]
    for row, (facings, slopes) in enumerate(zip(layout.facings.rows(),
                                                 layout.slopes())):
        out.append("    PartyLayoutRow{{ .battleType = BattleType::{}, .slots = {{{{\n"
                   .format(BATTLE_TYPES[row]))
        for facing, slope in zip(facings, slopes):
            out.append("        {{ .facing = Facing::{}, .slope = {} }},\n".format(
                "Right" if facing == FACING_RIGHT else "Left", slope))
        out.append("    }} },\n")
    return "".join(out)


def _rows_literal(rows, fmt):
    return "".join("    {{ " + ", ".join(fmt.format(v) for v in row) + " }},\n"
                   for row in rows)


def render_fixture(layout, positions_at, facings_at):
    out = [_HEADER.format("CharRowOffsetTbl, CharYOffsetTbl, CharXAngleTbl, "
                          "CharFlipTbl, CharDirTbl"),
           "// Test fixture for tests/test_battle_layout.cpp — the four tables the\n"
           "// battle places the party from, as the cartridge holds them, one row\n"
           "// per battle type (front, back, pincer, side; the y offsets add the\n"
           "// magitek train car's row). Deliberately independent of the typed rows.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n",
           "inline constexpr std::uint32_t kExpectedPartyPositionsAt = 0x{:06X};\n"
           .format(positions_at),
           "inline constexpr std::uint32_t kExpectedPartyFacingAt = 0x{:06X};\n\n"
           .format(facings_at),
           "// CharDirTbl: 0 faces left, 1 faces right.\n"
           "inline constexpr std::array<std::array<std::uint8_t, 4>, 4> "
           "kExpectedPartyFacing = {{\n",
           _rows_literal(layout.facings.rows(), "{}"),
           "}};\n\n"
           "// CharXAngleTbl: 256 is the middle of the field.\n"
           "inline constexpr std::array<std::array<std::uint16_t, 4>, 4> "
           "kExpectedPartyAngle = {{\n",
           _rows_literal(layout.angles.rows(), "{}"),
           "}};\n\n"
           "// CharRowOffsetTbl: what the back row adds to the angle.\n"
           "inline constexpr std::array<std::array<std::int16_t, 4>, 4> "
           "kExpectedPartyBackRowAngle = {{\n",
           _rows_literal([[signed(v) for v in row]
                          for row in layout.row_offsets.rows()], "{}"),
           "}};\n\n"
           "// CharYOffsetTbl: how far down the screen each member stands. Row 4 is\n"
           "// the magitek train car's.\n"
           "inline constexpr std::array<std::array<std::uint8_t, 4>, 5> "
           "kExpectedPartyY = {{\n",
           _rows_literal(layout.y_offsets.rows(), "{}"),
           "}};\n\n}  // namespace ostinato::test\n"]
    return "".join(out)


# --- driver --------------------------------------------------------------------

def run(source_root, outputs, check_only=False):
    layout, segment = read_layout(source_root)
    rom = common.load_vanilla_rom(source_root)
    positions_at, facings_at = place_layout(
        layout, rom, read_segment_bank(source_root, segment))
    summary = ("party positions at ${:06X}, facings at ${:06X}; "
               "cartridge-identical".format(positions_at, facings_at))
    if check_only:
        print("OK: " + summary)
        return 0
    _write(outputs["layout"], render_party_layout_inc(layout))
    _write(outputs["fixture"],
           render_fixture(layout, positions_at, facings_at))
    print("Emitted 2 files: " + summary)
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
    ap.add_argument("--layout-out",
                    default="src/data/generated/party_layout_data.inc")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/party_layout_expected.h")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)
    outputs = {"layout": args.layout_out, "fixture": args.fixture_out}
    try:
        return run(args.source_root, outputs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
