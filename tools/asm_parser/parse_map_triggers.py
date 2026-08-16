#!/usr/bin/env python3
"""Emit the map trigger-family tables (treasure + long/short entrance) from
original-src.

Port-time tooling (NOT a build/CI dependency). Each of the three trigger tables
is a pointer-table stream: a per-map `ptr_tbl` of byte offsets into a contiguous
record `.dat`, so the records are shared across maps (many maps point at the same
record, and maps with no trigger point past the last record). The pointer offsets
live in the rip's include-guard `.inc` (`_N := <Label> + $HHHH`); the record
bytes live in the sibling `.dat`.

  * TreasureProp   trigger/treasure_prop.dat   286 x 5 B  (player.asm:779-843,
                   ptrs :910-917) — 415 map slots
  * LongEntrance   trigger/long_entrance.dat   152 x 7 B  (entrance.asm:63-165,
                   ptrs :208-210) — 512 map slots
  * ShortEntrance  trigger/short_entrance.dat  1129 x 6 B (entrance.asm:283-373,
                   ptrs :385-388) — 512 map slots

The map-address space is 9-bit (ids are masked & $01ff by every consumer): the
entrance tables cover the full 512-slot space; treasures cover the 415 defined
maps. Each emitted per-map offset is a RECORD index (byte offset / ITEM_SIZE),
plus a final end entry (== record count), so a map's triggers are the half-open
record slice [offset[map], offset[map+1]); empty maps get a zero-length slice.

Structural guarantees, hard-errored at emit time:
  * each `.dat` length is an exact multiple of its record width, and the record
    count matches the expected 286 / 152 / 1129;
  * each `.inc` scope carries the expected ITEM_SIZE and ARRAY_LENGTH, with one
    `_N :=` offset per map slot (415 / 512 / 512);
  * every byte offset is a multiple of ITEM_SIZE (so it names a whole record);
  * the record-index offsets are monotonic non-decreasing and end at the record
    count.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_map_triggers.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError

# One row per trigger table: (slug, .inc name, .dat name, item_size,
# map-slot count, record count).
TABLES = [
    ("treasure", "treasure_prop.inc", "treasure_prop.dat", 5, 415, 286),
    ("long_entrance", "long_entrance.inc", "long_entrance.dat", 7, 512, 152),
    ("short_entrance", "short_entrance.inc", "short_entrance.dat", 6, 512, 1129),
]

# A pointer-class record offset symbol: `_N := <Label> + $HHHH`. Same grammar as
# the 1.H text `.inc` reader — the detect regex flags the line, the full regex
# pins it so a malformed offset hard-errors rather than being silently miscounted.
_RE_OFFSET_DETECT = re.compile(r"^_(\d+)\s*:=")
_RE_OFFSET_FULL = re.compile(
    r"^_(\d+)\s*:=\s*[A-Za-z0-9_]+\s*\+\s*\$([0-9a-fA-F]+)\s*$")


# --- .inc scope reader -------------------------------------------------------

def read_trigger_inc(path):
    """Read a trigger `.inc` scope into (item_size, array_length, offsets).

    Captures ITEM_SIZE / ARRAY_LENGTH and the `_N :=` per-map byte offsets inside
    the single `.scope`. Field-label assignments (`PosX := Start`, `Switch :=
    Start + 2`) and the `Start := <Label>` line are ignored; anything else inside
    the scope is a hard error so a grammar surprise escalates.
    """
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    scope_depth = 0
    item_size = None
    array_length = None
    offsets = {}

    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()
        if low.startswith(".scope"):
            scope_depth += 1
            continue
        if low.startswith(".endscope"):
            if scope_depth == 0:
                raise ParseError(path, idx + 1, ".endscope without .scope")
            scope_depth -= 1
            continue
        if scope_depth == 0:
            continue  # include guard, .global, .list — outside the scope

        # inside the scope
        if _RE_OFFSET_DETECT.match(s):
            full = _RE_OFFSET_FULL.match(s)
            if not full:
                raise ParseError(path, idx + 1,
                                 "malformed offset symbol: {!r}".format(s))
            n = int(full.group(1))
            if n in offsets:
                raise ParseError(path, idx + 1,
                                 "duplicate offset symbol _{}".format(n))
            offsets[n] = int(full.group(2), 16)
            continue
        if ":=" in s:
            continue  # field label (PosX := Start) or Start := <Label>
        if "=" in s:
            name, _sep, rhs = s.partition("=")
            name = name.strip()
            rhs = rhs.strip()
            if name == "ITEM_SIZE":
                item_size = common.parse_int_literal(rhs)
                if item_size is None:
                    raise ParseError(path, idx + 1,
                                     "malformed ITEM_SIZE {!r}".format(rhs))
            elif name == "ARRAY_LENGTH":
                array_length = common.parse_int_literal(rhs)
                if array_length is None:
                    raise ParseError(path, idx + 1,
                                     "malformed ARRAY_LENGTH {!r}".format(rhs))
            # any other scope-local assignment is ignored on purpose
            continue
        raise ParseError(path, idx + 1,
                         "unexpected line inside scope: {!r}".format(s))

    return item_size, array_length, offsets


def _resolve_table(source_root, item_size, array_length, record_count, inc_name,
                   dat_name):
    """Read one trigger table and return (records, record_offsets).

    records: list[bytes], each ITEM_SIZE wide, in physical `.dat` order.
    record_offsets: list[int] of length array_length + 1 — record-index offsets
    (byte offset / ITEM_SIZE) per map slot, plus the end entry (== record_count).
    All structural guarantees in the module docstring are asserted here.
    """
    trigger_dir = os.path.join(source_root, "src", "field", "trigger")
    inc_path = os.path.join(source_root, "include", "field", inc_name)
    dat_path = os.path.join(trigger_dir, dat_name)

    parsed_item_size, parsed_array_length, offsets = read_trigger_inc(inc_path)
    if parsed_item_size != item_size:
        raise ParseError(inc_path, 0,
                         "ITEM_SIZE {} != expected {}"
                         .format(parsed_item_size, item_size))
    if parsed_array_length != array_length:
        raise ParseError(inc_path, 0,
                         "ARRAY_LENGTH {} != expected {}"
                         .format(parsed_array_length, array_length))
    if sorted(offsets) != list(range(array_length)):
        raise ParseError(inc_path, 0,
                         "offset symbol space {} != 0..{} — escalate"
                         .format(sorted(offsets), array_length - 1))

    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != record_count * item_size:
        raise ParseError(dat_path, 0,
                         "{} bytes, expected {} ({} x {}) — wrong artifact"
                         .format(len(data), record_count * item_size,
                                 record_count, item_size))

    records = [data[i * item_size:(i + 1) * item_size]
               for i in range(record_count)]

    # Convert byte offsets to record indices; assert whole-record alignment.
    record_offsets = []
    for i in range(array_length):
        byte_off = offsets[i]
        if byte_off % item_size != 0:
            raise ParseError(inc_path, 0,
                             "map {} offset ${:04x} not a multiple of ITEM_SIZE "
                             "{} — escalate".format(i, byte_off, item_size))
        record_offsets.append(byte_off // item_size)
    record_offsets.append(record_count)  # end entry

    for i in range(len(record_offsets) - 1):
        if record_offsets[i] > record_offsets[i + 1]:
            raise ParseError(inc_path, 0,
                             "map offsets not monotonic at {} ({} > {}) — "
                             "escalate".format(i, record_offsets[i],
                                               record_offsets[i + 1]))
    if record_offsets[array_length] != record_count:
        raise ParseError(inc_path, 0,
                         "end offset {} != record count {} — escalate"
                         .format(record_offsets[array_length], record_count))
    if record_offsets[array_length - 1] > record_count:
        raise ParseError(inc_path, 0,
                         "last map offset {} exceeds record count {} — escalate"
                         .format(record_offsets[array_length - 1], record_count))

    return records, record_offsets


# --- banner / heads ----------------------------------------------------------

def _banner(source_lines):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    return ("// AUTO-GENERATED by tools/asm_parser/parse_map_triggers.py\n"
            + body +
            "// (original-src pinned at 1ea47b5)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_map_triggers.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


def _fixture_head(struct_text):
    return ("#pragma once\n\n"
            "#include <array>\n"
            "#include <cstdint>\n\n"
            "namespace ostinato::test {\n\n" + struct_text + "\n")


def _hexbytes(data):
    return ", ".join("0x{:02X}".format(b) for b in data)


def _offset_block(offsets, indent="    ", per_line=12):
    out = []
    for start in range(0, len(offsets), per_line):
        chunk = offsets[start:start + per_line]
        out.append(indent + ", ".join(str(o) for o in chunk) + ",\n")
    return "".join(out)


# --- record renderers --------------------------------------------------------

def render_treasure_inc(records):
    out = [_banner(["field/trigger/treasure_prop.dat (TreasureProp, "
                    "ROM ed/8634, player.asm:915)"]),
           "// TreasureProperty records in physical order, #included inside the\n"
           "// kTreasureProperties array in src/data/map_triggers.cpp. Records are\n"
           "// shared across maps via the offset table. Coordinates and the\n"
           "// type-dependent content byte are decimal; the packed switch word is\n"
           "// raw hex.\n\n"]
    for r in records:
        out.append(
            "    TreasureProperty{{\n"
            "        .posX    = {0},\n"
            "        .posY    = {1},\n"
            "        .trigger = TreasureSwitch{{{{ 0x{2:02X}, 0x{3:02X} }}}},\n"
            "        .content = {4},\n"
            "    }},\n".format(r[0], r[1], r[2], r[3], r[4]))
    return "".join(out)


def render_long_entrance_inc(records):
    out = [_banner(["field/trigger/long_entrance.dat (LongEntrance, "
                    "ROM ed/f882, entrance.asm:213)"]),
           "// LongEntrance records in physical order, #included inside the\n"
           "// kLongEntrances array in src/data/map_triggers.cpp. Records are\n"
           "// shared across maps via the offset table. Coordinates are decimal;\n"
           "// the packed run byte and destination word are raw hex.\n\n"]
    for r in records:
        out.append(
            "    LongEntrance{{\n"
            "        .srcX        = {0},\n"
            "        .srcY        = {1},\n"
            "        .run         = EntranceRun{{ 0x{2:02X} }},\n"
            "        .destination = EntranceDestination{{{{ 0x{3:02X}, 0x{4:02X} }}}},\n"
            "        .destX       = {5},\n"
            "        .destY       = {6},\n"
            "    }},\n".format(r[0], r[1], r[2], r[3], r[4], r[5], r[6]))
    return "".join(out)


def render_short_entrance_inc(records):
    out = [_banner(["field/trigger/short_entrance.dat (ShortEntrance, "
                    "ROM df/bf02, entrance.asm:391)"]),
           "// ShortEntrance records in physical order, #included inside the\n"
           "// kShortEntrances array in src/data/map_triggers.cpp. Records are\n"
           "// shared across maps via the offset table. Coordinates are decimal;\n"
           "// the packed destination word is raw hex.\n\n"]
    for r in records:
        out.append(
            "    ShortEntrance{{\n"
            "        .srcX        = {0},\n"
            "        .srcY        = {1},\n"
            "        .destination = EntranceDestination{{{{ 0x{2:02X}, 0x{3:02X} }}}},\n"
            "        .destX       = {4},\n"
            "        .destY       = {5},\n"
            "    }},\n".format(r[0], r[1], r[2], r[3], r[4], r[5]))
    return "".join(out)


_RECORD_RENDERERS = {
    "treasure": render_treasure_inc,
    "long_entrance": render_long_entrance_inc,
    "short_entrance": render_short_entrance_inc,
}


# --- offset renderers --------------------------------------------------------

def render_offsets_inc(slug, offsets, map_slots):
    label = {
        "treasure": "TreasurePropPtrs (player.asm:910)",
        "long_entrance": "LongEntrancePtrs (entrance.asm:208)",
        "short_entrance": "ShortEntrancePtrs (entrance.asm:385)",
    }[slug]
    src = {"treasure": "trigger/treasure_prop.dat",
           "long_entrance": "trigger/long_entrance.dat",
           "short_entrance": "trigger/short_entrance.dat"}[slug]
    out = [_banner(["field/{} — {}".format(src, label)]),
           "// MapTriggerOffsetEntry rows in map-id order, #included inside the\n"
           "// offset array in src/data/map_triggers.cpp. Each row carries its map\n"
           "// id as the typed .index identity field alongside the RECORD-array\n"
           "// index at which that map's records begin. The final entry's .index\n"
           "// is the map-slot count ({0}) and its .offset the record count (end\n"
           "// marker); a map's records are the half-open slice\n"
           "// [offset[map], offset[map + 1]), so empty maps get a zero-length\n"
           "// slice. A compile-time assert verifies .index == array position.\n\n"
           .format(map_slots)]
    for i, off in enumerate(offsets):
        out.append("    MapTriggerOffsetEntry{{ .index = {}, .offset = {} }},\n"
                   .format(i, off))
    return "".join(out)


# --- fixture renderers -------------------------------------------------------

def render_fixture(slug, records, offsets, item_size):
    type_name = {
        "treasure": "Treasure",
        "long_entrance": "LongEntrance",
        "short_entrance": "ShortEntrance",
    }[slug]
    src = {
        "treasure": "field/trigger/treasure_prop.dat + treasure_prop.inc",
        "long_entrance": "field/trigger/long_entrance.dat + long_entrance.inc",
        "short_entrance": "field/trigger/short_entrance.dat + short_entrance.inc",
    }[slug]
    struct = (
        "// One raw {0}-byte {1} record.\n"
        "struct Expected{1}Record {{\n"
        "    std::array<std::uint8_t, {0}> bytes;\n"
        "}};\n".format(item_size, type_name))
    out = [_banner([src]), _fixture_head(struct)]
    out.append("inline constexpr std::array<Expected{}Record, {}>\n"
               "kExpected{}Records = {{{{\n".format(
                   type_name, len(records), type_name))
    for r in records:
        out.append("    {{ {{ {} }} }},\n".format(_hexbytes(r)))
    out.append("}};\n\n")
    out.append("// The per-map record-index offset table ({} entries: map slots "
               "+ end).\n".format(len(offsets)))
    out.append("inline constexpr std::array<std::uint16_t, {}>\n"
               "kExpected{}Offsets = {{{{\n".format(len(offsets), type_name))
    out.append(_offset_block(offsets))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def _outputs(repo_root):
    gen = os.path.join(repo_root, "src", "data", "generated")
    fix = os.path.join(repo_root, "tests", "fixtures")
    return gen, fix


def run(source_root, repo_root, check_only=False):
    resolved = []
    for slug, inc_name, dat_name, item_size, map_slots, record_count in TABLES:
        records, offsets = _resolve_table(
            source_root, item_size, map_slots, record_count, inc_name, dat_name)
        resolved.append((slug, item_size, map_slots, records, offsets))

    if check_only:
        for slug, item_size, map_slots, records, offsets in resolved:
            print("OK: {} {} records ({} B each) / {} map slots (+end); "
                  "all structural asserts passed."
                  .format(slug, len(records), item_size, map_slots))
        return 0

    gen, fix = _outputs(repo_root)
    for slug, item_size, map_slots, records, offsets in resolved:
        _write(os.path.join(gen, "{}_data.inc".format(slug)),
               _RECORD_RENDERERS[slug](records))
        _write(os.path.join(gen, "{}_offsets_data.inc".format(slug)),
               render_offsets_inc(slug, offsets, map_slots))
        _write(os.path.join(fix, "{}_expected.h".format(slug)),
               render_fixture(slug, records, offsets, item_size))
    print("Emitted 3 trigger tables (9 files) -> {}"
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
