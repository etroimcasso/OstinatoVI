#!/usr/bin/env python3
"""Emit the Magitek train ride's data tables and the world cutscene curves.

Port-time tooling (NOT a build/CI dependency). The train ride steers a Mode 7
tunnel: a script picks one 32-byte pitch, yaw and background curve per step, the
background byte then picks one of twenty tile arrangements, and a handful of
smaller curves drive the airship's liftoff camera and two cutscenes. All of it
is numeric — none of it is authored artwork — so unlike the sprite-composition
records these tables compile into the binary.

Sources:

  * src/world/train_script.asm — the five selector-indexed curves the script
    block-moves into WRAM, the position multipliers, the tile arrangements and
    the rotation angles.
  * src/world/liftoff.asm — the airship takeoff/landing camera curve.
  * src/world/cutscene.asm — the ending scene's expanding circles, and the
    48-word table the shipped ROM reaches from nowhere.
  * src/world/ctrl.asm — the strafe angle per direction bitmask.
  * src/world/event.asm — Figaro Castle emerging and submerging.
  * The vanilla cartridge — the oracle. Every table is compared to the bytes at
    its own address over its whole extent before anything is written.

Emitted artifacts:

  * enums     include/ostinato/train_yaw_type.h
              include/ostinato/train_background_type.h
  * rows      src/data/generated/train_*_data.inc
              src/data/generated/airship_liftoff_data.inc
              src/data/generated/ending_scene_circle_data.inc
              src/data/generated/figaro_castle_data.inc
              src/data/generated/strafe_angle_data.inc
              src/data/generated/unused_cutscene_data.inc
  * fixtures  tests/fixtures/train_ride_expected.h
              tests/fixtures/world_cutscenes_expected.h

Structural guarantees, hard-errored at emit time:
  * every table holds exactly the number of entries its consumer's index
    arithmetic implies, and divides evenly into the items that arithmetic reads;
  * every background byte names a tile arrangement that exists;
  * the airship camera curve is a mirror about its peak everywhere except the
    one pair of rows that genuinely differ — a second differing pair is a corpus
    change, not something to absorb;
  * every ending-scene circle starts at the same radius and grows by the same
    step, which is what makes those two columns constants rather than data;
  * every strafe angle is a bearing under 360 degrees;
  * every table is byte-identical to the cartridge at its documented address.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_train_data.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError

# The world program lives in bank $ee (cfg/ff6-en.cfg:196), so a table's
# `@xxxx:` address prefix is bank-relative and this is the bank it sits in.
WORLD_BANK = 0xEE

# Bytes the script's block move copies per curve item, at all five of its sites
# (world/train_script.asm:370-420). The selector arithmetic that reaches an item
# is `and #$00ff; xba; lsr3` — a byte times 256 divided by 8.
CURVE_ITEM_BYTES = 32

# One background variant's tile arrangement: twelve tiles of four bytes. The
# consumer reaches a variant by multiplying the background byte by 48
# (world/train_script.asm:165-173, `x32` then `+ x32/2`).
TILES_PER_VARIANT = 12
TILE_BYTES = 4
VARIANT_BYTES = TILES_PER_VARIANT * TILE_BYTES

# Bytes per row of the airship takeoff/landing camera curve, and the step the
# two loops walk it with (world/liftoff.asm:166-169, :297-300).
LIFTOFF_ROW_BYTES = 7

# Words per ending-scene circle (world/cutscene.asm:986-990 steps x by $000c).
CIRCLE_ROW_WORDS = 6

# Bytes per Figaro Castle row. The copy is word-wise but the consumer indexes
# the block a byte at a time at a six-byte stride (world/event.asm:2203-2236).
FIGARO_ROW_BYTES = 6

# The one pair of rows where the airship camera curve stops being a mirror about
# its peak. Row 7 and row 23 differ in two bytes; every other pair matches
# exactly. Recorded by index so a second divergence stops the parse.
LIFTOFF_ASYMMETRIC_PAIR = (7, 23)

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
_RE_ADDR_PREFIX = re.compile(r"^@([0-9a-f]{4}):")
_RE_DATA_DIR = re.compile(r"^(?:@[0-9a-f]{4}:)?\s*\.(byte|word)\s+(.+)$")

# The corpus names the items of two of the five curve tables and nothing else.
# These are its own words, kept verbatim; the tables it leaves unlabelled keep a
# decimal selector rather than gaining invented names.
YAW_TYPES = (                       # world/train_script.asm:513-515
    ("STRAIGHT", "straight"),
    ("LEFT_TURN", "left turn"),
    ("RIGHT_TURN", "right turn"),
)
BACKGROUND_TYPES = (                # world/train_script.asm:528-532
    ("PIPES_MORE_BEAMS", "pipes w/ more beams"),
    ("PIPES_FEWER_BEAMS", "pipes w/ fewer beams"),
    ("NO_PIPES", "no pipes"),
    ("SPLIT_TRACK_MORE_WALLS", "split track w/ more walls"),
    ("SPLIT_TRACK_FEWER_WALLS", "split track w/ fewer walls"),
)


class Table(object):
    """One parsed table: where it came from, and the values it holds."""

    def __init__(self, name, path, label, directive, values, address, line):
        self.name = name            # the port's name for it
        self.path = path            # source file, repo-relative
        self.label = label          # the ca65 label it sits under
        self.directive = directive  # "byte" or "word"
        self.values = values
        self.address = address      # bank-relative, from the @xxxx: prefix
        self.line = line            # 1-based line of the first data line

    @property
    def snes(self):
        return (WORLD_BANK << 16) | self.address

    @property
    def width(self):
        return 1 if self.directive == "byte" else 2

    @property
    def source_desc(self):
        return "{}:{} ({}, ROM {:02x}/{:04x})".format(
            self.path, self.line, self.label, WORLD_BANK, self.address)


# --- reading -----------------------------------------------------------------


def _eval_data_term(term, path, lineno):
    """Resolve one .byte/.word term. Every table in scope writes plain integer
    literals; anything else is a hard error rather than a guess."""
    value = common.parse_int_literal(term.strip())
    if value is None:
        raise ParseError(path, lineno, "unparsable data term {!r}".format(term))
    return value


def read_labeled_table(source_root, rel_path, label, directive, expected, name):
    """Read the `.byte`/`.word` body under `label`, with its own address.

    The body ends at the first line that is not a data directive of the
    requested kind, so the entry count comes from the source rather than from a
    constant here; `expected` is then asserted against it. Stacked alias labels
    above the data are scaffolding and are skipped — every table in this unit
    carries at least two (`_ee2692:` then `sindat:`).
    """
    path = os.path.join(source_root, "src", rel_path)
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    start = None
    wanted = "{}:".format(label)
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        if code.strip() == wanted:
            start = idx + 1
            break
    if start is None:
        raise ParseError(rel_path, 0, "label {} not found".format(wanted))

    address = None
    first_line = None
    values = []
    for idx in range(start, len(lines)):
        code, _comment = common.strip_comment(lines[idx])
        s = code.strip()
        if not s:
            if values:
                break
            continue

        m = _RE_DATA_DIR.match(s)
        if not m:
            if values:
                break
            if _RE_LABEL.match(s):
                continue
            raise ParseError(rel_path, idx + 1,
                             "unexpected line under {}: {!r}".format(label, s))
        if m.group(1) != directive:
            if values:
                break
            raise ParseError(rel_path, idx + 1,
                             "{} opens with .{} but .{} was expected"
                             .format(label, m.group(1), directive))

        addr_match = _RE_ADDR_PREFIX.match(s)
        if addr_match and address is None:
            address = int(addr_match.group(1), 16)
            first_line = idx + 1
        for term in m.group(2).split(","):
            values.append(_eval_data_term(term, rel_path, idx + 1))

    if address is None:
        raise ParseError(rel_path, 0, "{} has no @addr: line".format(label))
    if len(values) != expected:
        raise ParseError(rel_path, first_line,
                         "{} holds {} entries, expected {}"
                         .format(label, len(values), expected))
    return Table(name, rel_path, label, directive, values, address, first_line)


def _read_u16(buf, pos):
    return buf[pos] | (buf[pos + 1] << 8)


def assert_rom_table(rom, table):
    """Every entry must match the cartridge at the table's own address."""
    base = common.hirom_file_offset(table.snes)
    if table.width == 1:
        actual = list(rom[base:base + len(table.values)])
    else:
        actual = [_read_u16(rom, base + i * 2)
                  for i in range(len(table.values))]
    for i, (got, want) in enumerate(zip(actual, table.values)):
        if got != want:
            raise ParseError(table.path, table.line,
                             "ROM MISMATCH at {}[{}]: cartridge ${:0{w}x} != "
                             "source ${:0{w}x}"
                             .format(table.name, i, got, want,
                                     w=table.width * 2))
    if len(actual) != len(table.values):
        raise ParseError(table.path, table.line,
                         "ROM MISMATCH: {} length differs".format(table.name))


def as_signed(value):
    return value - 256 if value >= 128 else value


# --- structural checks -------------------------------------------------------


def _assert_divides(table, item_bytes, label):
    if len(table.values) % item_bytes:
        raise ParseError(table.path, table.line,
                         "{} holds {} bytes, not a whole number of {} ({} B "
                         "each)".format(table.name, len(table.values), label,
                                        item_bytes))
    return len(table.values) // item_bytes


def assert_background_bytes_name_a_variant(background, variants):
    """Every background byte selects one of the tile arrangements that exist.
    The selector is the byte times 48, so a byte past the end would read another
    table's bytes as tiles."""
    for i, value in enumerate(background.values):
        if value >= variants:
            raise ParseError(background.path, background.line,
                             "background byte {} at index {} names variant {}, "
                             "but only {} exist"
                             .format(value, i, value, variants))


def assert_liftoff_mirror(rows, path, line):
    """The camera curve rises to a peak and comes back down the way it went up.
    Rows 0-30 mirror about row 15; row 31 is the tail the loops never reach as a
    pair. Exactly one pair differs, and it is pinned by index — a second one is
    a corpus change."""
    differing = []
    span = 31
    for i in range(span // 2):
        j = span - 1 - i
        if rows[i] != rows[j]:
            differing.append((i, j))
    if differing != [LIFTOFF_ASYMMETRIC_PAIR]:
        raise ParseError(path, line,
                         "airship camera curve mirror changed: differing pairs "
                         "{} (expected exactly {})"
                         .format(differing, [LIFTOFF_ASYMMETRIC_PAIR]))


def assert_circle_constants(rows, path, line):
    """Every ending-scene circle opens at the same radius and grows by the same
    step. That is what lets those two columns read as constants; if a row ever
    disagreed they would be per-circle data."""
    radius = rows[0][2]
    step = rows[0][3]
    for i, row in enumerate(rows):
        if row[2] != radius or row[3] != step:
            raise ParseError(path, line,
                             "ending-scene circle {} opens at radius ${:04x} "
                             "step ${:04x}, but circle 0 opens at ${:04x} "
                             "step ${:04x}"
                             .format(i, row[2], row[3], radius, step))
    return radius, step


def assert_strafe_angles(table):
    """Every entry is a bearing. The table is shorter than the bitmask the
    consumer masks out; see resolve() for what that means and where it is
    recorded."""
    for i, value in enumerate(table.values):
        if value >= 360:
            raise ParseError(table.path, table.line,
                             "strafe angle {} at index {} is not a bearing "
                             "under 360".format(value, i))


# --- resolution --------------------------------------------------------------


class Resolved(object):
    def __init__(self):
        self.tables = {}
        self.pitch_items = 0
        self.yaw_items = 0
        self.background_items = 0
        self.variants = 0
        self.liftoff_rows = []
        self.circle_rows = []
        self.figaro_rows = []
        self.circle_radius = 0
        self.circle_step = 0
        self.strafe_gap = ()


def load_and_resolve(source_root):
    rom = common.load_vanilla_rom(source_root)

    specs = (
        # name,                   file,                    label,             dir,     count
        ("pitch1",                "world/train_script.asm", "sindat",          "byte",   416),
        ("pitch2",                "world/train_script.asm", "cosdat",          "byte",   416),
        ("positionMultiplier",    "world/train_script.asm", "reduce_rate",     "byte",    64),
        ("yawMultiplier",         "world/train_script.asm", "kyokuritu",       "byte",    32),
        ("yaw",                   "world/train_script.asm", "kyokup",          "byte",    96),
        ("background",            "world/train_script.asm", "chrset",          "byte",   160),
        ("tiles",                 "world/train_script.asm", "truck_spset",     "byte",   960),
        ("rotationAngles",        "world/train_script.asm", "thcntdt",         "byte",     8),
        ("airshipLiftoff",        "world/liftoff.asm",      "_ee98f1",         "byte",   224),
        ("unusedCutscene",        "world/cutscene.asm",     "_ee0d5c",         "word",    48),
        ("endingSceneCircles",    "world/cutscene.asm",     "_ee100e",         "word",    54),
        ("strafeAngles",          "world/ctrl.asm",         "StrafeAngleTbl",  "word",    11),
        ("figaroCastle",          "world/event.asm",        "_ee8066",         "word",    18),
    )

    res = Resolved()
    for name, rel_path, label, directive, count in specs:
        table = read_labeled_table(source_root, rel_path, label, directive,
                                   count, name)
        assert_rom_table(rom, table)
        res.tables[name] = table

    res.pitch_items = _assert_divides(res.tables["pitch1"], CURVE_ITEM_BYTES,
                                      "curve items")
    if _assert_divides(res.tables["pitch2"], CURVE_ITEM_BYTES,
                       "curve items") != res.pitch_items:
        raise ParseError("world/train_script.asm", 0,
                         "the two pitch tables hold different item counts, but "
                         "one selector reads both")
    res.yaw_items = _assert_divides(res.tables["yaw"], CURVE_ITEM_BYTES,
                                    "curve items")
    res.background_items = _assert_divides(res.tables["background"],
                                           CURVE_ITEM_BYTES, "curve items")
    _assert_divides(res.tables["yawMultiplier"], CURVE_ITEM_BYTES,
                    "curve items")
    res.variants = _assert_divides(res.tables["tiles"], VARIANT_BYTES,
                                   "tile arrangements")
    assert_background_bytes_name_a_variant(res.tables["background"],
                                           res.variants)

    liftoff = res.tables["airshipLiftoff"]
    rows = _assert_divides(liftoff, LIFTOFF_ROW_BYTES, "camera steps")
    res.liftoff_rows = [liftoff.values[i * LIFTOFF_ROW_BYTES:
                                       (i + 1) * LIFTOFF_ROW_BYTES]
                        for i in range(rows)]
    assert_liftoff_mirror(res.liftoff_rows, liftoff.path, liftoff.line)

    circles = res.tables["endingSceneCircles"]
    if len(circles.values) % CIRCLE_ROW_WORDS:
        raise ParseError(circles.path, circles.line,
                         "the ending-scene table holds {} words, not a whole "
                         "number of {}-word circles"
                         .format(len(circles.values), CIRCLE_ROW_WORDS))
    res.circle_rows = [circles.values[i * CIRCLE_ROW_WORDS:
                                      (i + 1) * CIRCLE_ROW_WORDS]
                       for i in range(len(circles.values) // CIRCLE_ROW_WORDS)]
    res.circle_radius, res.circle_step = assert_circle_constants(
        res.circle_rows, circles.path, circles.line)

    figaro = res.tables["figaroCastle"]
    figaro_bytes = []
    for word in figaro.values:
        figaro_bytes.append(word & 0xFF)
        figaro_bytes.append((word >> 8) & 0xFF)
    if len(figaro_bytes) % FIGARO_ROW_BYTES:
        raise ParseError(figaro.path, figaro.line,
                         "Figaro Castle holds {} bytes, not a whole number of "
                         "{}-byte rows".format(len(figaro_bytes),
                                               FIGARO_ROW_BYTES))
    res.figaro_rows = [figaro_bytes[i * FIGARO_ROW_BYTES:
                                    (i + 1) * FIGARO_ROW_BYTES]
                       for i in range(len(figaro_bytes) // FIGARO_ROW_BYTES)]

    assert_strafe_angles(res.tables["strafeAngles"])
    # The consumer masks the direction bits to 0-15 and indexes with all of them
    # (world/ctrl.asm:201-206), but the table stops at 11 entries. The masks the
    # table does not cover are reported rather than absorbed; see
    # docs/Bugs.md and src/data/world_cutscenes.h.
    res.strafe_gap = tuple(range(len(res.tables["strafeAngles"].values), 16))

    return res


# --- rendering ---------------------------------------------------------------


def _banner(source_desc):
    return ("// AUTO-GENERATED by tools/asm_parser/parse_train_data.py\n"
            "// Source: {}\n"
            "// (original-src pinned at 1ea47b5; every byte cross-checked\n"
            "// against the vanilla ROM over the whole block)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_train_data.py \\\n"
            "//       --source-root original-src --repo-root .\n"
            .format(source_desc))


def render_curve_inc(table, array_name, doc, signed):
    """A flat one-byte-per-step curve, as self-labeling rows."""
    out = [_banner(table.source_desc), "\n", doc, "\n\n"]
    width = max(len(str(len(table.values) - 1)), 1)
    for index, raw in enumerate(table.values):
        value = as_signed(raw) if signed else raw
        out.append("    {{ .index = {:>{w}}, .value = {:>4} }},\n"
                   .format(index, value, w=width))
    return "".join(out)


def render_tiles_inc(table, variants):
    doc = (
        "// Every Magitek train background variant's tile arrangement: twelve\n"
        "// tiles each — five left wall, five right wall, one ceiling, one rail\n"
        "// (world/train_script.asm:549). Rows are flattened, indexed\n"
        "// variant * 12 + tile; the row's identity (.index) is a typed field\n"
        "// and a compile-time assert verifies index == position. The four\n"
        "// bytes are stored exactly as the cartridge holds them, so a slot the\n"
        "// consumer treats as empty reads as four zeroes. Included inside\n"
        "// kTrainTiles in src/data/train_ride.cpp.")
    out = [_banner(table.source_desc), "\n", doc, "\n\n"]
    for index in range(variants * TILES_PER_VARIANT):
        x, y, low, high = table.values[index * TILE_BYTES:
                                       (index + 1) * TILE_BYTES]
        out.append("    {{ .index = {:>3}, .tile = {{ .x = {:>3}, .y = {:>3}, "
                   ".tileIndexLow = {:>3}, .tileIndexHigh = {:>3} }} }},\n"
                   .format(index, x, y, low, high))
    return "".join(out)


def render_liftoff_inc(table, rows):
    doc = (
        "// One frame of the airship's takeoff and landing camera move. The\n"
        "// takeoff walks the rows forward and subtracts each delta; the\n"
        "// landing walks them backward and adds (world/liftoff.asm:129-169,\n"
        "// :260-300). The row's identity (.index, the decimal frame) is a\n"
        "// typed field; a compile-time assert verifies index == position.\n"
        "// Included inside kAirshipLiftoffSteps in src/data/train_ride.cpp.")
    out = [_banner(table.source_desc), "\n", doc, "\n\n"]
    for index, row in enumerate(rows):
        scale = row[0] | (row[1] << 8)
        near = row[2] | (row[3] << 8)
        out.append("    {{ .index = {:>2}, .scaleDelta = {:>5}, "
                   ".nearWeightDelta = {:>5}, .farWeightDelta = {:>3}, "
                   ".horizonDelta = {:>2}, .pivotYDelta = {:>2} }},\n"
                   .format(index, scale, near, row[4], row[5], row[6]))
    return "".join(out)


def render_circles_inc(table, rows):
    doc = (
        "// The expanding circles of the ending airship scene. Each one waits\n"
        "// out its start delay, then grows by its step every frame until it\n"
        "// reaches its limit (world/cutscene.asm:948-990). The row's identity\n"
        "// (.index) is a typed field; a compile-time assert verifies\n"
        "// index == position. Included inside kEndingSceneCircles in\n"
        "// src/data/world_cutscenes.cpp.")
    out = [_banner(table.source_desc), "\n", doc, "\n\n"]
    for index, row in enumerate(rows):
        out.append("    {{ .index = {}, .x = {:>3}, .y = {:>3}, "
                   ".radius = {:>4}, .radiusStep = {:>4}, "
                   ".radiusLimit = {:>5}, .startDelay = {:>2} }},\n"
                   .format(index, row[0], row[1], row[2], row[3], row[4],
                           row[5]))
    return "".join(out)


def render_figaro_inc(table, rows):
    doc = (
        "// Figaro Castle rising out of the desert and sinking back into it —\n"
        "// six moving pieces, driven by the same table in both directions\n"
        "// (world/event.asm:2153, :2275). Each piece advances `phase` by\n"
        "// `phaseStep` every frame and steps to its next animation frame when\n"
        "// that wraps (world/event.asm:2214-2227). The row's identity (.index)\n"
        "// is a typed field; a compile-time assert verifies index == position.\n"
        "// Included inside kFigaroCastlePieces in\n"
        "// src/data/world_cutscenes.cpp.")
    out = [_banner(table.source_desc), "\n", doc, "\n\n"]
    for index, row in enumerate(rows):
        out.append("    {{ .index = {}, .x = {:>3}, .y = {:>3}, "
                   ".animationStep = {}, .phase = {:>3}, .phaseStep = {:>3}, "
                   ".xOffset = {} }},\n"
                   .format(index, row[0], row[1], row[2], row[3], row[4],
                           row[5]))
    return "".join(out)


def render_strafe_inc(table):
    doc = (
        "// The bearing the world map strafes along while the Y button is held,\n"
        "// per direction bitmask (world/ctrl.asm:193). The mask is up, down,\n"
        "// left, right in bit order, so the index is a combination of\n"
        "// directions rather than a single one — index 6 is down and left, and\n"
        "// the masks holding two opposed directions carry no bearing.\n"
        "// The row's identity (.index) is a typed field; a compile-time assert\n"
        "// verifies index == position. Included inside kStrafeAngles in\n"
        "// src/data/world_cutscenes.cpp.")
    out = [_banner(table.source_desc), "\n", doc, "\n\n"]
    for index, value in enumerate(table.values):
        out.append("    {{ .index = {:>2}, .degrees = {:>3} }},\n"
                   .format(index, value))
    return "".join(out)


def render_unused_inc(table):
    doc = (
        "// Forty-eight words the shipped cartridge reaches from nowhere: the\n"
        "// disassembly marks the table unused and a whole-tree search finds no\n"
        "// consumer, so nothing in the source says what the values mean. They\n"
        "// are carried as raw words and named nothing rather than given an\n"
        "// invented meaning. The row's identity (.index) is a typed field; a\n"
        "// compile-time assert verifies index == position. Included inside\n"
        "// kUnusedCutsceneWords in src/data/world_cutscenes.cpp.")
    out = [_banner(table.source_desc), "\n", doc, "\n\n"]
    for index, value in enumerate(table.values):
        out.append("    {{ .index = {:>2}, .value = 0x{:04X} }},\n"
                   .format(index, value))
    return "".join(out)


def _enum_header(name, members, doc, source_desc):
    out = [
        "// AUTO-GENERATED by tools/asm_parser/parse_train_data.py — "
        "DO NOT EDIT BY HAND\n",
        "// Source: {}\n".format(source_desc),
        "// Regenerate via:\n",
        "//   python3 tools/asm_parser/parse_train_data.py \\\n",
        "//       --source-root original-src --repo-root .\n",
        "#pragma once\n\n",
        "#include <cstdint>\n\n",
        "namespace ostinato {\n\n",
        doc, "\n",
        "enum class {} : std::uint8_t {{\n".format(name),
    ]
    width = max(len(member) for member, _text in members)
    for index, (member, text) in enumerate(members):
        out.append("    {:<{w}} = {:>2},  // {}\n"
                   .format(member, index, text, w=width))
    out.append("};\n\n")
    out.append("}  // namespace ostinato\n")
    return "".join(out)


def render_yaw_type_header(table):
    doc = ("// Which way the Magitek train's track bends over a script step.\n"
           "// The script's yaw byte picks one of these, and the curve it names\n"
           "// steers the tunnel for the next 32 frames.")
    return _enum_header("TrainYawType", YAW_TYPES, doc, table.source_desc)


def render_background_type_header(table):
    doc = ("// Which set of tunnel scenery the Magitek train runs through over a\n"
           "// script step. The script's background byte picks one of these, and\n"
           "// the curve it names supplies one tile arrangement per frame.")
    return _enum_header("TrainBackgroundType", BACKGROUND_TYPES, doc,
                        table.source_desc)


# --- fixtures ----------------------------------------------------------------


def _byte_array(values, per_line=12):
    lines = []
    for start in range(0, len(values), per_line):
        chunk = values[start:start + per_line]
        lines.append("    " + " ".join("0x{:02X},".format(v) for v in chunk))
    return "\n".join(lines)


def _word_array(values, per_line=8):
    lines = []
    for start in range(0, len(values), per_line):
        chunk = values[start:start + per_line]
        lines.append("    " + " ".join("0x{:04X},".format(v) for v in chunk))
    return "\n".join(lines)


def render_train_fixture(res):
    t = res.tables
    out = [
        _banner("world/train_script.asm + world/liftoff.asm (ROM bytes)"),
        "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
        "namespace ostinato::test {\n\n"
        "// Every train-ride table as the cartridge stores it, in index order.\n"
        "// The port's typed rows are compared against these raw bytes.\n\n",
    ]

    def emit(name, count, values):
        out.append("inline constexpr std::array<std::uint8_t, {}> {} = {{{{\n"
                   .format(count, name))
        out.append(_byte_array(values))
        out.append("\n}};\n\n")

    emit("kExpectedTrainPitchData1", len(t["pitch1"].values),
         t["pitch1"].values)
    emit("kExpectedTrainPitchData2", len(t["pitch2"].values),
         t["pitch2"].values)
    emit("kExpectedTrainYawData", len(t["yaw"].values), t["yaw"].values)
    emit("kExpectedTrainYawMultiplier", len(t["yawMultiplier"].values),
         t["yawMultiplier"].values)
    emit("kExpectedTrainBackgroundData", len(t["background"].values),
         t["background"].values)
    emit("kExpectedTrainPositionMultipliers",
         len(t["positionMultiplier"].values), t["positionMultiplier"].values)
    emit("kExpectedTrainRotationAngles", len(t["rotationAngles"].values),
         t["rotationAngles"].values)
    emit("kExpectedTrainTiles", len(t["tiles"].values), t["tiles"].values)
    emit("kExpectedAirshipLiftoff", len(t["airshipLiftoff"].values),
         t["airshipLiftoff"].values)

    out.append("// Structural facts the parser asserts at emit time, restated\n"
               "// here so the tests pin them too.\n")
    out.append("inline constexpr std::size_t kExpectedTrainPitchItems = {};\n"
               .format(res.pitch_items))
    out.append("inline constexpr std::size_t kExpectedTrainYawItems = {};\n"
               .format(res.yaw_items))
    out.append("inline constexpr std::size_t kExpectedTrainBackgroundItems = "
               "{};\n".format(res.background_items))
    out.append("inline constexpr std::size_t kExpectedTrainVariants = {};\n"
               .format(res.variants))
    out.append("inline constexpr std::size_t kExpectedAirshipLiftoffSteps = "
               "{};\n".format(len(res.liftoff_rows)))
    out.append("// The one pair of camera rows that does not mirror.\n")
    out.append("inline constexpr std::size_t kExpectedLiftoffAsymmetricLow = "
               "{};\n".format(LIFTOFF_ASYMMETRIC_PAIR[0]))
    out.append("inline constexpr std::size_t kExpectedLiftoffAsymmetricHigh = "
               "{};\n\n".format(LIFTOFF_ASYMMETRIC_PAIR[1]))
    out.append("}  // namespace ostinato::test\n")
    return "".join(out)


def render_cutscene_fixture(res):
    t = res.tables
    out = [
        _banner("world/cutscene.asm + world/ctrl.asm + world/event.asm "
                "(ROM words)"),
        "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
        "namespace ostinato::test {\n\n"
        "// The world cutscene and control tables as the cartridge stores\n"
        "// them, in index order.\n\n",
    ]

    def emit(name, values):
        out.append("inline constexpr std::array<std::uint16_t, {}> {} = {{{{\n"
                   .format(len(values), name))
        out.append(_word_array(values))
        out.append("\n}};\n\n")

    emit("kExpectedEndingSceneCircles", t["endingSceneCircles"].values)
    emit("kExpectedFigaroCastle", t["figaroCastle"].values)
    emit("kExpectedStrafeAngles", t["strafeAngles"].values)
    emit("kExpectedUnusedCutsceneWords", t["unusedCutscene"].values)

    out.append("inline constexpr std::size_t kExpectedEndingSceneCircleCount = "
               "{};\n".format(len(res.circle_rows)))
    out.append("inline constexpr std::uint16_t kExpectedCircleOpeningRadius = "
               "0x{:04X};\n".format(res.circle_radius))
    out.append("inline constexpr std::uint16_t kExpectedCircleRadiusStep = "
               "0x{:04X};\n".format(res.circle_step))
    out.append("inline constexpr std::size_t kExpectedFigaroPieceCount = {};\n"
               .format(len(res.figaro_rows)))
    out.append("// Direction masks the consumer can form but the strafe table\n"
               "// does not cover; see docs/Bugs.md.\n")
    out.append("inline constexpr std::array<std::uint8_t, {}> "
               "kExpectedStrafeMasksWithoutAnEntry = {{{{\n"
               .format(len(res.strafe_gap)))
    out.append("    " + " ".join("{},".format(m) for m in res.strafe_gap))
    out.append("\n}};\n\n")
    out.append("}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------


def run(source_root, repo_root, check_only=False):
    res = load_and_resolve(source_root)
    t = res.tables

    gen = os.path.join(repo_root, "src", "data", "generated")
    inc = os.path.join(repo_root, "include", "ostinato")
    fix = os.path.join(repo_root, "tests", "fixtures")

    outputs = [
        (os.path.join(inc, "train_yaw_type.h"),
         render_yaw_type_header(t["yaw"])),
        (os.path.join(inc, "train_background_type.h"),
         render_background_type_header(t["background"])),

        (os.path.join(gen, "train_pitch_data_1_data.inc"),
         render_curve_inc(
             t["pitch1"], "kTrainPitchData1",
             "// The train's pitch, one step per frame. Thirteen curves of 32\n"
             "// steps, flattened and indexed item * 32 + step; the script's\n"
             "// pitch byte picks the item (world/train_script.asm:358-364).\n"
             "// Values are signed offsets. The row's identity (.index) is a\n"
             "// typed field; a compile-time assert verifies index == position.\n"
             "// Included inside kTrainPitchData1 in src/data/train_ride.cpp.",
             signed=True)),
        (os.path.join(gen, "train_pitch_data_2_data.inc"),
         render_curve_inc(
             t["pitch2"], "kTrainPitchData2",
             "// The train's second pitch curve, read alongside the first and\n"
             "// selected by the same script byte (world/train_script.asm:374).\n"
             "// Thirteen curves of 32 signed steps, flattened and indexed\n"
             "// item * 32 + step. The row's identity (.index) is a typed\n"
             "// field; a compile-time assert verifies index == position.\n"
             "// Included inside kTrainPitchData2 in src/data/train_ride.cpp.",
             signed=True)),
        (os.path.join(gen, "train_yaw_data.inc"),
         render_curve_inc(
             t["yaw"], "kTrainYawData",
             "// How hard the train's track bends, one step per frame. Three\n"
             "// curves of 32 signed steps — straight, left turn, right turn —\n"
             "// flattened and indexed item * 32 + step\n"
             "// (world/train_script.asm:383-388). The row's identity (.index)\n"
             "// is a typed field; a compile-time assert verifies\n"
             "// index == position. Included inside kTrainYawData in\n"
             "// src/data/train_ride.cpp.",
             signed=True)),
        (os.path.join(gen, "train_yaw_multiplier_data.inc"),
         render_curve_inc(
             t["yawMultiplier"], "kTrainYawMultiplier",
             "// Scales the yaw curve down the screen, one step per drawn row\n"
             "// (world/train_script.asm:119). One curve of 32 signed steps —\n"
             "// the script's selector for it is zero on every step of every\n"
             "// course, so no other item exists to reach. The row's identity\n"
             "// (.index) is a typed field; a compile-time assert verifies\n"
             "// index == position. Included inside kTrainYawMultiplier in\n"
             "// src/data/train_ride.cpp.",
             signed=True)),
        (os.path.join(gen, "train_background_data.inc"),
         render_curve_inc(
             t["background"], "kTrainBackgroundData",
             "// Which tile arrangement the tunnel wears, one step per frame.\n"
             "// Five curves of 32 steps, flattened and indexed item * 32 +\n"
             "// step; each value names a train tile arrangement\n"
             "// (world/train_script.asm:397-402). The row's identity (.index)\n"
             "// is a typed field; a compile-time assert verifies\n"
             "// index == position. Included inside kTrainBackgroundData in\n"
             "// src/data/train_ride.cpp.",
             signed=False)),
        (os.path.join(gen, "train_position_multiplier_data.inc"),
         render_curve_inc(
             t["positionMultiplier"], "kTrainPositionMultipliers",
             "// How much of the pitch and yaw offset each drawn row receives —\n"
             "// the falloff that makes the tunnel recede. Read at two\n"
             "// different offsets into the same curve, and both are contract\n"
             "// (world/train_script.asm:76, :161). The row's identity (.index)\n"
             "// is a typed field; a compile-time assert verifies\n"
             "// index == position. Included inside kTrainPositionMultipliers\n"
             "// in src/data/train_ride.cpp.",
             signed=False)),
        (os.path.join(gen, "train_rotation_angle_data.inc"),
         render_curve_inc(
             t["rotationAngles"], "kTrainRotationAngles",
             "// The angles the train can be told to roll through, in degrees.\n"
             "// A script command carries a three-bit index into this curve\n"
             "// (world/train_script.asm:815-820). The row's identity (.index)\n"
             "// is a typed field; a compile-time assert verifies\n"
             "// index == position. Included inside kTrainRotationAngles in\n"
             "// src/data/train_ride.cpp.",
             signed=False)),
        (os.path.join(gen, "train_tile_data.inc"),
         render_tiles_inc(t["tiles"], res.variants)),
        (os.path.join(gen, "airship_liftoff_data.inc"),
         render_liftoff_inc(t["airshipLiftoff"], res.liftoff_rows)),

        (os.path.join(gen, "ending_scene_circle_data.inc"),
         render_circles_inc(t["endingSceneCircles"], res.circle_rows)),
        (os.path.join(gen, "figaro_castle_data.inc"),
         render_figaro_inc(t["figaroCastle"], res.figaro_rows)),
        (os.path.join(gen, "strafe_angle_data.inc"),
         render_strafe_inc(t["strafeAngles"])),
        (os.path.join(gen, "unused_cutscene_data.inc"),
         render_unused_inc(t["unusedCutscene"])),

        (os.path.join(fix, "train_ride_expected.h"), render_train_fixture(res)),
        (os.path.join(fix, "world_cutscenes_expected.h"),
         render_cutscene_fixture(res)),
    ]

    if not check_only:
        for path, text in outputs:
            _write(path, text)

    print("OK: train ride {} pitch / {} yaw / {} background curve items, "
          "{} tile arrangements ({} tiles); airship camera {} steps, one "
          "non-mirroring pair ({}, {}); {} ending-scene circles; {} Figaro "
          "pieces; {} strafe bearings ({} masks uncovered); {} unused words; "
          "every table byte-identical to the cartridge."
          .format(res.pitch_items, res.yaw_items, res.background_items,
                  res.variants, res.variants * TILES_PER_VARIANT,
                  len(res.liftoff_rows), LIFTOFF_ASYMMETRIC_PAIR[0],
                  LIFTOFF_ASYMMETRIC_PAIR[1], len(res.circle_rows),
                  len(res.figaro_rows), len(t["strafeAngles"].values),
                  len(res.strafe_gap), len(t["unusedCutscene"].values)))
    return res


def _write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source-root", required=True)
    ap.add_argument("--repo-root", required=True)
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)
    try:
        run(args.source_root, args.repo_root, check_only=args.check_only)
    except ParseError as exc:
        print("PARSE ERROR: {}".format(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
