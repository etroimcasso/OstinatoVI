#!/usr/bin/env python3
"""Emit the map-metadata core tables (mechanics half) from original-src.

Port-time tooling (NOT a build/CI dependency). Reads four rip .dat binaries plus
the two bg-animation .byte source files and emits the port's typed surfaces:

  * MapProperties     map_prop.dat            415 x 33 B  (LoadMapProp copies the
                      record whole to $0520-$0540, map.asm:150-169)
  * MapParallax       map_parallax.dat        21 x 8 B    (scroll.asm:117-196)
  * MapPaletteAnim    map_pal_anim_prop.dat   10 x 12 B   (InitPalAnim copies two
                      6-byte slots per entry, anim.asm:26-64)
  * InitialNpcSwitch  init_npc_switch.dat     128 B raw   (obj.asm:176-192)
  * MapBGAnimProp     map_bg_anim_prop.asm    20 indexes  (pointer-table stream of
                      10-byte sub-records; anim.asm:277-346, ptrs :565-571)
  * MapBG3AnimProp    map_bg3_anim_prop.asm   6 x 20 B    (anim.asm:382-395,
                      ptrs :573-579)

The two bg-animation tables are ca65 `.byte` source with a pointer table built by
the `ptr_tbl`/`end_ptr` macros — the pointers are macro-generated, not literal
words, so the parser derives each index's byte offset by summing the parsed
record bodies in physical order. Four bg1/bg2 indexes (11/12/17/19) share one
body (the labels alias); the consumer's fixed 8-sub-record read spills a short
body into the next one. Both are preserved by storing the stream contiguously
with per-index offsets.

Structural guarantees, hard-errored at emit time:
  * each .dat length is an exact multiple of its record width;
  * the MapProperties layout-id group's spare bits (30-31) are 0 on every map,
    the invariant the packed-group decode relies on;
  * the two bg-anim files parse to exactly 20 and 6 pointer-table indexes with a
    fully-covered 0..N-1 index space, and each bg3 body is exactly 20 bytes;
  * the bg1/bg2 alias family 11/12/17/19 resolves to one shared offset.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_map_metadata.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError

MAP_PROP_ROWS = 415
MAP_PROP_WIDTH = 33
PARALLAX_ROWS = 21
PARALLAX_WIDTH = 8
PAL_ANIM_ROWS = 10
PAL_ANIM_WIDTH = 12          # two 6-byte slots
PAL_ANIM_SLOT_WIDTH = 6
INIT_NPC_SWITCH_BYTES = 128
BG_ANIM_INDEXES = 20         # MapBGAnimProp pointer-table entry count
BG3_ANIM_INDEXES = 6         # MapBG3AnimProp pointer-table entry count
BG3_ANIM_RECORD_WIDTH = 20   # fixed 20-byte bg3 record

_BG_ANIM_LABEL = re.compile(r"^MapBGAnimProp::_(\d+):$")
_BG3_ANIM_LABEL = re.compile(r"^MapBG3AnimProp::_(\d+):$")


# --- byte helpers ------------------------------------------------------------

def _word(data, offset):
    return data[offset] | (data[offset + 1] << 8)


def _signed(byte):
    """A byte as a signed two's-complement int8 (for the parallax speeds)."""
    return byte - 256 if byte >= 128 else byte


def _read_dat(path, width, rows):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) != width * rows:
        raise ParseError(path, 0,
                         "{} bytes, expected {} ({} x {} — wrong artifact)"
                         .format(len(data), width * rows, rows, width))
    return data


# --- bg-animation .byte source ------------------------------------------------

def _read_byte_bodies(path, label_re, expected_indexes):
    """Parse a MapBG(3)AnimProp .byte source into ordered bodies + an offset map.

    Consecutive labels before a `.byte` block alias one body (11/12/17/19). The
    returned value is:
        stream:  bytes, the bodies concatenated in physical (pointer) order
        offsets: list[int] of length expected_indexes+1 — offsets[i] is the byte
                 offset of index i into the stream; offsets[-1] == len(stream)
    """
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    bodies = []           # list[(list[int] label_indexes, bytes)]
    pending = []          # label indexes awaiting their body
    current = bytearray()  # bytes accumulated for the current body

    def flush():
        if current:
            bodies.append((list(pending), bytes(current)))
            pending.clear()
            current.clear()

    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()
        if low == ".list off" or low == ".list on":
            continue
        m = label_re.match(s)
        if m:
            # A new label: the previous body (if any) belongs to the labels
            # pending before it, so flush before starting this label's group.
            flush()
            pending.append(int(m.group(1)))
            continue
        if low.startswith(".byte"):
            arg = s[len(".byte"):].strip()
            for token in arg.split(","):
                value = common.parse_int_literal(token.strip())
                if value is None or not (0 <= value <= 0xFF):
                    raise ParseError(path, idx + 1,
                                     "malformed .byte value {!r}".format(token))
                current.append(value)
            continue
        raise ParseError(path, idx + 1,
                         "unexpected line {!r} (label or .byte expected)"
                         .format(s))
    flush()

    # Build the contiguous stream and the per-index offset table.
    stream = bytearray()
    offset_by_index = {}
    for label_indexes, body in bodies:
        body_offset = len(stream)
        for index in label_indexes:
            if index in offset_by_index:
                raise ParseError(path, 0,
                                 "duplicate label index {}".format(index))
            offset_by_index[index] = body_offset
        stream.extend(body)

    if sorted(offset_by_index) != list(range(expected_indexes)):
        raise ParseError(path, 0,
                         "label index space {} != 0..{} — escalate"
                         .format(sorted(offset_by_index), expected_indexes - 1))

    offsets = [offset_by_index[i] for i in range(expected_indexes)]
    offsets.append(len(stream))
    return bytes(stream), offsets


# --- table reads (with structural asserts) -----------------------------------

def read_map_prop(path):
    data = _read_dat(path, MAP_PROP_WIDTH, MAP_PROP_ROWS)
    rows = []
    for i in range(MAP_PROP_ROWS):
        rec = data[i * MAP_PROP_WIDTH:(i + 1) * MAP_PROP_WIDTH]
        # The layout-id group (+13..+16) is a 30-bit stream with two spare high
        # bits; the packed-group decode assumes those spare bits are 0.
        layout = rec[13] | rec[14] << 8 | rec[15] << 16 | rec[16] << 24
        if (layout >> 30) & 0x03:
            raise ParseError(path, 0,
                             "row {}: layout spare bits (30-31) set ({:#010x}) — "
                             "escalate, never guess".format(i, layout))
        rows.append(rec)
    return rows


def read_parallax(path):
    data = _read_dat(path, PARALLAX_WIDTH, PARALLAX_ROWS)
    return [data[i * PARALLAX_WIDTH:(i + 1) * PARALLAX_WIDTH]
            for i in range(PARALLAX_ROWS)]


def read_pal_anim(path):
    data = _read_dat(path, PAL_ANIM_WIDTH, PAL_ANIM_ROWS)
    return [data[i * PAL_ANIM_WIDTH:(i + 1) * PAL_ANIM_WIDTH]
            for i in range(PAL_ANIM_ROWS)]


def read_init_npc_switch(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) != INIT_NPC_SWITCH_BYTES:
        raise ParseError(path, 0,
                         "{} bytes, expected {} — wrong artifact"
                         .format(len(data), INIT_NPC_SWITCH_BYTES))
    return list(data)


def read_bg3_anim(path):
    stream, offsets = _read_byte_bodies(path, _BG3_ANIM_LABEL, BG3_ANIM_INDEXES)
    # bg3 records are fixed 20 bytes; every index must span exactly one record.
    for i in range(BG3_ANIM_INDEXES):
        span = offsets[i + 1] - offsets[i]
        if span != BG3_ANIM_RECORD_WIDTH:
            raise ParseError(path, 0,
                             "bg3 index {} spans {} bytes, expected {} — escalate"
                             .format(i, span, BG3_ANIM_RECORD_WIDTH))
    return [stream[i * BG3_ANIM_RECORD_WIDTH:(i + 1) * BG3_ANIM_RECORD_WIDTH]
            for i in range(BG3_ANIM_INDEXES)]


# --- banner / fixture head ---------------------------------------------------

def _banner(source_lines):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    return ("// AUTO-GENERATED by tools/asm_parser/parse_map_metadata.py\n"
            + body +
            "// (original-src pinned at 1ea47b5)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_map_metadata.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


def _fixture_head(struct_text):
    return ("#pragma once\n\n"
            "#include <array>\n"
            "#include <cstdint>\n\n"
            "namespace ostinato::test {\n\n" + struct_text + "\n")


def _hexbytes(data):
    return ", ".join("0x{:02X}".format(b) for b in data)


# --- MapProperties -----------------------------------------------------------

def render_map_prop_inc(rows):
    out = [_banner(["field/map_prop.dat (MapProp, ROM ed/8f00, map.asm:163)"]),
           "// MapPropertiesEntry rows in map-id order, #included inside the\n"
           "// kMapProperties array in src/data/map_properties.cpp. Identity is\n"
           "// the decimal .index (a placeholder map id — there is no corpus\n"
           "// name source; real map names are a planned later cleanup). Packed\n"
           "// bytes are carried raw inside their typed wrappers; indices,\n"
           "// quantities, and the placeholder song id are decimal; the two\n"
           "// unknown bytes and the color-math mode are raw hex.\n\n"]
    for i, r in enumerate(rows):
        out.append(
            "    MapPropertiesEntry{{  // [{0}]\n"
            "        .index = {0},\n"
            "        .record = MapProperties{{\n"
            "            .titleIndex        = {1},\n"
            "            .effectFlags       = MapEffectFlags{{0x{2:02X}}},\n"
            "            .battleBackground  = MapBattleBackground{{0x{3:02X}}},\n"
            "            .unknown3          = 0x{4:02X},\n"
            "            .tilePropIndex     = {5},\n"
            "            .battleFlags       = MapBattleFlags{{0x{6:02X}}},\n"
            "            .windowMask        = {7},\n"
            "            .graphics          = MapGraphicsIds{{{{ {8} }}}},\n"
            "            .layouts           = MapLayoutIds{{{{ {9} }}}},\n"
            "            .overlayIndex      = {10},\n"
            "            .bg2ScrollX        = {11},\n"
            "            .bg2ScrollY        = {12},\n"
            "            .bg3ScrollX        = {13},\n"
            "            .bg3ScrollY        = {14},\n"
            "            .parallaxIndex     = {15},\n"
            "            .bgSizes           = MapBgSizes{{{{ {16} }}}},\n"
            "            .paletteIndex      = {17},\n"
            "            .palAnimIndex      = {18},\n"
            "            .animation         = MapAnimationIndexes{{0x{19:02X}}},\n"
            "            .songId            = {20},\n"
            "            .unknown29         = 0x{21:02X},\n"
            "            .scrollRangeWidth  = {22},\n"
            "            .scrollRangeHeight = {23},\n"
            "            .colorMathMode     = 0x{24:02X},\n"
            "        }},\n"
            "    }},\n".format(
                i, r[0], r[1], r[2], r[3], r[4], r[5], r[6],
                _hexbytes(r[7:13]), _hexbytes(r[13:17]),
                r[17], r[18], r[19], r[20], r[21], r[22],
                _hexbytes(r[23:25]), r[25], r[26], r[27], r[28], r[29],
                r[30], r[31], r[32]))
    return "".join(out)


def render_map_prop_fixture(rows):
    struct = (
        "// One raw 33-byte map_prop record alongside its decimal map id.\n"
        "struct ExpectedMapProperties {\n"
        "    std::uint16_t index;\n"
        "    std::array<std::uint8_t, 33> bytes;\n"
        "};\n")
    out = [_banner(["field/map_prop.dat (MapProp, ROM ed/8f00)"]),
           _fixture_head(struct),
           "inline constexpr std::array<ExpectedMapProperties, {}>\n"
           "kExpectedMapProperties = {{{{\n".format(len(rows))]
    for i, r in enumerate(rows):
        out.append("    {{ .index = {:>3}, .bytes = {{ {} }} }},\n"
                   .format(i, _hexbytes(r)))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- MapParallax -------------------------------------------------------------

def render_parallax_inc(rows):
    out = [_banner(["field/map_parallax.dat (MapParallax, ROM c0/fe40, "
                    "scroll.asm:192)"]),
           "// MapParallaxEntry rows in parallax-index order, #included inside\n"
           "// the kMapParallax array in src/data/map_properties.cpp. The scroll\n"
           "// speeds are signed decimal (sign-extended x16 to fixed point by the\n"
           "// consumer); the multipliers are decimal.\n\n"]
    for i, r in enumerate(rows):
        out.append(
            "    MapParallaxEntry{{  // [{0}]\n"
            "        .index = {0},\n"
            "        .record = MapParallax{{\n"
            "            .bg2SpeedX      = {1},\n"
            "            .bg2SpeedY      = {2},\n"
            "            .bg3SpeedX      = {3},\n"
            "            .bg3SpeedY      = {4},\n"
            "            .bg2MultiplierX = {5},\n"
            "            .bg2MultiplierY = {6},\n"
            "            .bg3MultiplierX = {7},\n"
            "            .bg3MultiplierY = {8},\n"
            "        }},\n"
            "    }},\n".format(
                i, _signed(r[0]), _signed(r[1]), _signed(r[2]), _signed(r[3]),
                r[4], r[5], r[6], r[7]))
    return "".join(out)


def render_parallax_fixture(rows):
    struct = (
        "// One raw 8-byte parallax record alongside its decimal index.\n"
        "struct ExpectedMapParallax {\n"
        "    std::uint16_t index;\n"
        "    std::array<std::uint8_t, 8> bytes;\n"
        "};\n")
    out = [_banner(["field/map_parallax.dat (MapParallax, ROM c0/fe40)"]),
           _fixture_head(struct),
           "inline constexpr std::array<ExpectedMapParallax, {}>\n"
           "kExpectedMapParallax = {{{{\n".format(len(rows))]
    for i, r in enumerate(rows):
        out.append("    {{ .index = {:>2}, .bytes = {{ {} }} }},\n"
                   .format(i, _hexbytes(r)))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- MapPaletteAnimation -----------------------------------------------------

def _pal_anim_slot_literal(slot):
    return (
        "PaletteAnimationSlot{{\n"
        "                .control        = PaletteAnimationControl{{0x{0:02X}}},\n"
        "                .frameDuration  = {1},\n"
        "                .colorOffset    = {2},\n"
        "                .colorByteCount = {3},\n"
        "                .romColorOffset = {4},\n"
        "            }}".format(slot[0], slot[1], slot[2], slot[3],
                               _word(slot, 4)))


def render_pal_anim_inc(rows):
    out = [_banner(["field/map_pal_anim_prop.dat (MapPalAnimProp, ROM cx/9825, "
                    "anim.asm:581)"]),
           "// MapPaletteAnimationEntry rows in index order, #included inside\n"
           "// the kMapPaletteAnimations array in src/data/map_animations.cpp.\n"
           "// Each entry is two 6-byte slots; the control byte is carried raw\n"
           "// inside PaletteAnimationControl, the durations/offsets/counts are\n"
           "// decimal.\n\n"]
    for i, r in enumerate(rows):
        slot0 = _pal_anim_slot_literal(r[0:PAL_ANIM_SLOT_WIDTH])
        slot1 = _pal_anim_slot_literal(r[PAL_ANIM_SLOT_WIDTH:PAL_ANIM_WIDTH])
        out.append(
            "    MapPaletteAnimationEntry{{  // [{0}]\n"
            "        .index = {0},\n"
            "        .record = MapPaletteAnimation{{\n"
            "            .slots = {{{{\n"
            "            {1},\n"
            "            {2},\n"
            "            }}}},\n"
            "        }},\n"
            "    }},\n".format(i, slot0, slot1))
    return "".join(out)


def render_pal_anim_fixture(rows):
    struct = (
        "// One raw 12-byte palette-animation entry alongside its decimal index.\n"
        "struct ExpectedMapPaletteAnimation {\n"
        "    std::uint16_t index;\n"
        "    std::array<std::uint8_t, 12> bytes;\n"
        "};\n")
    out = [_banner(["field/map_pal_anim_prop.dat (MapPalAnimProp, ROM cx/9825)"]),
           _fixture_head(struct),
           "inline constexpr std::array<ExpectedMapPaletteAnimation, {}>\n"
           "kExpectedMapPaletteAnimation = {{{{\n".format(len(rows))]
    for i, r in enumerate(rows):
        out.append("    {{ .index = {:>2}, .bytes = {{ {} }} }},\n"
                   .format(i, _hexbytes(r)))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- InitialNpcSwitches ------------------------------------------------------

def render_init_npc_inc(values):
    out = [_banner(["field/init_npc_switch.dat (ROM c0/e0a0, obj.asm:187)"]),
           "// The 128 raw initial NPC event-bit bytes as one constexpr array,\n"
           "// #included inside an anonymous namespace in\n"
           "// src/data/map_properties.cpp. These are opaque bit flags seeded at\n"
           "// new game; the per-bit meanings are event-domain state ported in a\n"
           "// later phase.\n\n"
           "constexpr std::uint8_t kInitialNpcSwitches[" + str(len(values))
           + "] = {\n"]
    for start in range(0, len(values), 12):
        out.append("    " + _hexbytes(values[start:start + 12]) + ",\n")
    out.append("};\n")
    return "".join(out)


def render_init_npc_fixture(values):
    out = [_banner(["field/init_npc_switch.dat (ROM c0/e0a0)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"
           "// The raw 128 initial NPC event-bit bytes.\n"
           "inline constexpr std::array<std::uint8_t, 128>\n"
           "kExpectedInitialNpcSwitches = {{\n"]
    for start in range(0, len(values), 12):
        chunk = values[start:start + 12]
        out.append("    " + _hexbytes(chunk) + ",\n")
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- MapBGAnimProp (bg1/bg2 stream) ------------------------------------------

def render_bg_anim_stream_inc(stream):
    out = [_banner(["field/map_bg_anim_prop.asm (MapBGAnimProp, ROM cx/91ff, "
                    "anim.asm:569)"]),
           "// The contiguous bg1/bg2 animation byte stream as one constexpr\n"
           "// array, #included inside an anonymous namespace in\n"
           "// src/data/map_animations.cpp. Bodies are stored back-to-back in\n"
           "// pointer-table order; kBgAnimationOffsets gives each index's start.\n\n"
           "constexpr std::uint8_t kBgAnimationStream[" + str(len(stream))
           + "] = {\n"]
    for start in range(0, len(stream), 12):
        out.append("    " + _hexbytes(stream[start:start + 12]) + ",\n")
    out.append("};\n")
    return "".join(out)


def render_bg_anim_offsets_inc(offsets):
    out = [_banner(["field/map_bg_anim_prop.asm (MapBGAnimPropPtrs, "
                    "anim.asm:565)"]),
           "// BgAnimationOffsetEntry rows in animation-index order, #included\n"
           "// inside the kBgAnimationOffsets array in\n"
           "// src/data/map_animations.cpp. Each row carries its animation index\n"
           "// as .index; indexes 11/12/17/19 alias one body and share an offset;\n"
           "// the final row (index 20) is the end offset (== stream length).\n"
           "// Decimal byte offsets.\n\n"]
    for i, off in enumerate(offsets):
        out.append("    BgAnimationOffsetEntry{{ .index = {}, .offset = {} }},\n"
                   .format(i, off))
    return "".join(out)


def render_bg_anim_fixture(stream, offsets):
    n = str(len(stream))
    m = str(len(offsets))
    out = [_banner(["field/map_bg_anim_prop.asm (MapBGAnimProp, ROM cx/91ff)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n",
           "namespace ostinato::test {\n\n",
           "// The contiguous bg1/bg2 animation byte stream (" + n + " bytes).\n",
           "inline constexpr std::array<std::uint8_t, " + n + ">\n",
           "kExpectedBgAnimationStream = {{\n"]
    for start in range(0, len(stream), 12):
        out.append("    " + _hexbytes(stream[start:start + 12]) + ",\n")
    out.append("}};\n\n")
    out.append("// The 20 index offsets plus the end offset.\n")
    out.append("inline constexpr std::array<std::uint32_t, " + m + ">\n")
    out.append("kExpectedBgAnimationOffsets = {{\n")
    out.append("    " + ", ".join(str(o) for o in offsets) + ",\n")
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- MapBG3AnimProp ----------------------------------------------------------

def render_bg3_anim_inc(records):
    out = [_banner(["field/map_bg3_anim_prop.asm (MapBG3AnimProp, ROM cx/97ad, "
                    "anim.asm:577)"]),
           "// Bg3AnimationRecordEntry rows in index order, #included inside the\n"
           "// kBg3Animations array in src/data/map_animations.cpp. Each record\n"
           "// is a fixed 20 bytes; the animation speed, graphics size, and eight\n"
           "// frame graphics offsets are decimal words.\n\n"]
    for i, r in enumerate(records):
        frames = ", ".join(str(_word(r, 4 + 2 * f)) for f in range(8))
        out.append(
            "    Bg3AnimationRecordEntry{{  // [{0}]\n"
            "        .index = {0},\n"
            "        .record = Bg3AnimationRecord{{\n"
            "            .animSpeed = {1},\n"
            "            .gfxSize   = {2},\n"
            "            .frames    = {{{{ {3} }}}},\n"
            "        }},\n"
            "    }},\n".format(i, _word(r, 0), _word(r, 2), frames))
    return "".join(out)


def render_bg3_anim_fixture(records):
    struct = (
        "// One raw 20-byte bg3 animation record alongside its decimal index.\n"
        "struct ExpectedBg3Animation {\n"
        "    std::uint16_t index;\n"
        "    std::array<std::uint8_t, 20> bytes;\n"
        "};\n")
    out = [_banner(["field/map_bg3_anim_prop.asm (MapBG3AnimProp, ROM cx/97ad)"]),
           _fixture_head(struct),
           "inline constexpr std::array<ExpectedBg3Animation, {}>\n"
           "kExpectedBg3Animation = {{{{\n".format(len(records))]
    for i, r in enumerate(records):
        out.append("    {{ .index = {:>1}, .bytes = {{ {} }} }},\n"
                   .format(i, _hexbytes(r)))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def _paths(source_root):
    j = os.path.join
    field = j(source_root, "src", "field")
    return {
        "map_prop": j(field, "map_prop.dat"),
        "parallax": j(field, "map_parallax.dat"),
        "pal_anim": j(field, "map_pal_anim_prop.dat"),
        "init_npc": j(field, "init_npc_switch.dat"),
        "bg_anim": j(field, "map_bg_anim_prop.asm"),
        "bg3_anim": j(field, "map_bg3_anim_prop.asm"),
    }


def _outputs(repo_root):
    gen = os.path.join(repo_root, "src", "data", "generated")
    fix = os.path.join(repo_root, "tests", "fixtures")
    j = os.path.join
    return {
        "map_prop_inc": j(gen, "map_properties_data.inc"),
        "map_prop_fix": j(fix, "map_properties_expected.h"),
        "parallax_inc": j(gen, "map_parallax_data.inc"),
        "parallax_fix": j(fix, "map_parallax_expected.h"),
        "pal_anim_inc": j(gen, "map_pal_anim_data.inc"),
        "pal_anim_fix": j(fix, "map_pal_anim_expected.h"),
        "init_npc_inc": j(gen, "init_npc_switch_data.inc"),
        "init_npc_fix": j(fix, "init_npc_switch_expected.h"),
        "bg_anim_stream_inc": j(gen, "map_bg_anim_stream_data.inc"),
        "bg_anim_offsets_inc": j(gen, "map_bg_anim_offsets_data.inc"),
        "bg_anim_fix": j(fix, "map_bg_anim_expected.h"),
        "bg3_anim_inc": j(gen, "map_bg3_anim_data.inc"),
        "bg3_anim_fix": j(fix, "map_bg3_anim_expected.h"),
    }


def run(source_root, repo_root, check_only=False):
    p = _paths(source_root)

    map_prop = read_map_prop(p["map_prop"])
    parallax = read_parallax(p["parallax"])
    pal_anim = read_pal_anim(p["pal_anim"])
    init_npc = read_init_npc_switch(p["init_npc"])
    bg_stream, bg_offsets = _read_byte_bodies(
        p["bg_anim"], _BG_ANIM_LABEL, BG_ANIM_INDEXES)
    bg3_anim = read_bg3_anim(p["bg3_anim"])

    # The bg1/bg2 alias family (11/12/17/19) must resolve to one shared offset.
    alias = {bg_offsets[i] for i in (11, 12, 17, 19)}
    if len(alias) != 1:
        raise ParseError(p["bg_anim"], 0,
                         "bg1/bg2 indexes 11/12/17/19 do not share one offset "
                         "({}) — escalate".format(sorted(alias)))

    if check_only:
        print("OK: map_prop {} / parallax {} / pal_anim {} / init_npc {} / "
              "bg_anim {} indexes ({} B) / bg3_anim {}; all structural asserts "
              "passed.".format(
                  len(map_prop), len(parallax), len(pal_anim), len(init_npc),
                  BG_ANIM_INDEXES, len(bg_stream), len(bg3_anim)))
        return 0

    o = _outputs(repo_root)
    _write(o["map_prop_inc"], render_map_prop_inc(map_prop))
    _write(o["map_prop_fix"], render_map_prop_fixture(map_prop))
    _write(o["parallax_inc"], render_parallax_inc(parallax))
    _write(o["parallax_fix"], render_parallax_fixture(parallax))
    _write(o["pal_anim_inc"], render_pal_anim_inc(pal_anim))
    _write(o["pal_anim_fix"], render_pal_anim_fixture(pal_anim))
    _write(o["init_npc_inc"], render_init_npc_inc(init_npc))
    _write(o["init_npc_fix"], render_init_npc_fixture(init_npc))
    _write(o["bg_anim_stream_inc"], render_bg_anim_stream_inc(bg_stream))
    _write(o["bg_anim_offsets_inc"], render_bg_anim_offsets_inc(bg_offsets))
    _write(o["bg_anim_fix"], render_bg_anim_fixture(bg_stream, bg_offsets))
    _write(o["bg3_anim_inc"], render_bg3_anim_inc(bg3_anim))
    _write(o["bg3_anim_fix"], render_bg3_anim_fixture(bg3_anim))
    print("Emitted 6 tables (13 files) -> {}"
          .format(os.path.join(repo_root, "src", "data", "generated")))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root", default="original-src",
                    help="disassembly root")
    ap.add_argument("--repo-root", default=".", help="repo root for outputs")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)
    try:
        return run(args.source_root, args.repo_root, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
