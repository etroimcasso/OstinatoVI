#!/usr/bin/env python3
"""Emit the event-trigger family — per-map event triggers + per-map startup
events — from original-src, with the vanilla ROM as the byte-identity oracle.

Two tables live in src/event/, both `.include`d into event_main.asm and linked
into the event-script block (EventScript @ ca/0000, fixed_block $02e600):

  * EventTrigger  (event/event_trigger.asm)  1,164 x 5 B behind a 416-map ptr
    table at c4/0000 (fixed_block $1a10). Each record is a tile position (x, y)
    and a 24-bit event-script offset. Consumers: field CheckEventTriggers
    (field/event.asm:5730) + world CheckEvent (world/move.asm:1309).
  * MapInitEvent  (event/map_init_event.asm)  512 x 3 B flat array at d1/fa00,
    one startup-event offset per map (full 9-bit map space). Consumer:
    field/init.asm:485; an EventReturn entry means "no startup event".

Event references are stored as `(addr - EventScript) & $ffffff` (a 24-bit offset
into the script block). Two label forms appear:

  * address-named `_cXXXXXX` — the label IS the SNES address; the offset is
    mechanical (addr - $ca0000) and is *verified* against the ROM per record.
  * named code labels — a closed set per table (event_trigger: SavePoint,
    WorldTent, EnterPhoenixCave, EnterKefkasTower, DoomGazeMagicite;
    map_init: EventReturn). Their assembled value exists only in the ROM, so it
    is read there (oracle capture) and every reference to the same label must
    agree.

The drift-proof is a full-block byte cross-check: the parser reassembles the
c4/0000 ptr table (416 map words + end word) and every record, and compares them
to the ROM byte-for-byte. Any mismatch is a hard error citing the offending
record — the parser is never adjusted to accept a deviation.

Structural guarantees, hard-errored at emit time:
  * event_trigger.inc carries ITEM_SIZE 5 / ARRAY_LENGTH $01a0 (416 map slots);
  * the .asm holds exactly 1,164 records across map slots _0.._415 (with _415
    empty), and the ptr-table math (0x342 + 5*index) matches every ROM ptr word;
  * every `_cXXXXXX` offset equals the 24-bit value assembled in the ROM;
  * all references to a named label resolve to one offset;
  * the fixed_block tail is pure padding (recorded, not emitted).

Requires the vanilla ROM (FF6_VANILLA_ROM env var, or a .smc under
<source-root>/vanilla/). Port-time tooling (NOT a build/CI dependency): the
emitted .inc / fixture are committed; CI re-verifies them via this parser's e2e
test and the C++ memcmp suite.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_event_triggers.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError

# --- fixed layout facts (all verified against the ROM at emit time) ----------

# c4/0000 (EventTriggerPtrs) / d1/fa00 (MapInitEvent), HiROM-mapped file offsets.
EVENT_TRIGGER_SNES = 0xC40000
MAP_INIT_SNES = 0xD1FA00

EVENT_TRIGGER_ITEM_SIZE = 5
EVENT_TRIGGER_MAP_SLOTS = 416          # ARRAY_LENGTH $01a0; one more than map_prop
EVENT_TRIGGER_RECORD_COUNT = 1164
EVENT_TRIGGER_BLOCK_SIZE = 0x1A10      # fixed_block $1a10

MAP_INIT_ITEM_SIZE = 3
MAP_INIT_COUNT = 512                   # full 9-bit map space

# The valid event-offset range: [0, size of the event-script block).
EVENT_BLOCK_SIZE = 0x2E600

# Closed sets of named (non-address) labels per table. Anything outside these
# (and not `_cXXXXXX`) is a hard error — a new named label must be adjudicated,
# never guessed.
EVENT_TRIGGER_NAMED = frozenset({
    "SavePoint", "WorldTent", "EnterPhoenixCave", "EnterKefkasTower",
    "DoomGazeMagicite",
})
MAP_INIT_NAMED = frozenset({"EventReturn"})

# --- source grammar ----------------------------------------------------------

_RE_MAP_LABEL = re.compile(r"^EventTrigger::_(\d+):")
_RE_MAKE_TRIGGER = re.compile(
    r"^make_event_trigger\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}\s*,\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*$")
_RE_MAP_INIT = re.compile(
    r"^map_init_ptr\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
_RE_ITEM_SIZE = re.compile(r"^ITEM_SIZE\s*=\s*(\S+)")
_RE_ARRAY_LENGTH = re.compile(r"^ARRAY_LENGTH\s*=\s*(\S+)")


class TriggerRecord(object):
    """One parsed event trigger: tile position + the referenced event label."""

    def __init__(self, pos_x, pos_y, label, lineno):
        self.pos_x = pos_x
        self.pos_y = pos_y
        self.label = label
        self.lineno = lineno
        self.offset = None  # 24-bit, filled in at resolve time


def read_event_trigger_inc(path):
    """Read event_trigger.inc for ITEM_SIZE / ARRAY_LENGTH (asserts they exist)."""
    item_size = None
    array_length = None
    with open(path, "r", encoding="utf-8") as fh:
        for idx, raw in enumerate(fh):
            code, _comment = common.strip_comment(raw)
            s = code.strip()
            if not s:
                continue
            m = _RE_ITEM_SIZE.match(s)
            if m:
                item_size = common.parse_int_literal(m.group(1))
                continue
            m = _RE_ARRAY_LENGTH.match(s)
            if m:
                array_length = common.parse_int_literal(m.group(1))
    if item_size is None or array_length is None:
        raise ParseError(path, 0, "ITEM_SIZE / ARRAY_LENGTH missing from .inc")
    return item_size, array_length


def parse_event_trigger_asm(path, map_slots=EVENT_TRIGGER_MAP_SLOTS):
    """Parse event_trigger.asm into per-map record lists.

    Returns (records, record_offsets):
      records: list[TriggerRecord] in physical (ROM) order.
      record_offsets: list[int] of length map_slots + 1 — the record index at
      which each map's records begin, plus a final end entry (== record count).
    Records are grouped by the `EventTrigger::_N:` labels; a map with no records
    between its label and the next gets a zero-length slice. The make-trigger
    macro *definition* body is skipped (`.mac ... .endmac`). map_slots is a knob
    for tests; production always passes the fixed 416.
    """
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    per_map = [None] * map_slots
    current = None      # current map index, or None before the first label
    macro_depth = 0
    records = []

    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if low.startswith(".endmac"):
            if macro_depth == 0:
                raise ParseError(path, idx + 1, ".endmac without .mac")
            macro_depth -= 1
            continue
        if macro_depth > 0:
            continue

        m = _RE_MAP_LABEL.match(s)
        if m:
            current = int(m.group(1))
            if not (0 <= current < map_slots):
                raise ParseError(path, idx + 1,
                                 "map slot _{} out of range".format(current))
            if per_map[current] is not None:
                raise ParseError(path, idx + 1,
                                 "duplicate map slot _{}".format(current))
            per_map[current] = []
            continue

        m = _RE_MAKE_TRIGGER.match(s)
        if m:
            if current is None:
                raise ParseError(path, idx + 1,
                                 "record before any EventTrigger::_N label")
            pos_x, pos_y = int(m.group(1)), int(m.group(2))
            if pos_x > 255 or pos_y > 255:
                raise ParseError(path, idx + 1,
                                 "position byte out of range {},{}"
                                 .format(pos_x, pos_y))
            rec = TriggerRecord(pos_x, pos_y, m.group(3), idx + 1)
            per_map[current].append(rec)
            records.append(rec)
            continue

        # Structural scaffolding we intentionally skip; anything else escalates.
        if (s.endswith(":") or low.startswith(".segment")
                or low.startswith(".include") or low.startswith("fixed_block")
                or low.startswith("ptr_tbl") or low.startswith("end_ptr")
                or low.startswith("end_fixed_block")):
            continue
        raise ParseError(path, idx + 1,
                         "unexpected line in event_trigger.asm: {!r}".format(s))

    missing = [i for i, v in enumerate(per_map) if v is None]
    if missing:
        raise ParseError(path, 0,
                         "map slots without a _N label: {} — escalate"
                         .format(missing[:8]))

    record_offsets = []
    running = 0
    for i in range(map_slots):
        record_offsets.append(running)
        running += len(per_map[i])
    record_offsets.append(running)  # end entry
    if running != len(records):
        raise ParseError(path, 0,
                         "record accounting mismatch {} != {}"
                         .format(running, len(records)))
    return records, record_offsets


def parse_map_init_asm(path, count=MAP_INIT_COUNT):
    """Parse map_init_event.asm into a list[(label, lineno)] of length `count`."""
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    labels = []
    macro_depth = 0
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if low.startswith(".endmac"):
            if macro_depth == 0:
                raise ParseError(path, idx + 1, ".endmac without .mac")
            macro_depth -= 1
            continue
        if macro_depth > 0:
            continue

        m = _RE_MAP_INIT.match(s)
        if m:
            labels.append((m.group(1), idx + 1))
            continue
        if (s.endswith(":") or low.startswith(".segment")
                or low.startswith(".export") or low.startswith(".include")):
            continue
        raise ParseError(path, idx + 1,
                         "unexpected line in map_init_event.asm: {!r}".format(s))

    if len(labels) != count:
        raise ParseError(path, 0,
                         "map_init entries {} != expected {}"
                         .format(len(labels), count))
    return labels


# --- ROM resolution + full-block cross-check ---------------------------------

def _read_offset(rom, pos):
    """Read a 24-bit little-endian offset from the ROM at `pos`."""
    return rom[pos] | (rom[pos + 1] << 8) | (rom[pos + 2] << 16)


def _resolve_event_offset(rom, rec_pos, label, allowed, named_offsets, path):
    """Resolve one event reference, cross-checking against the ROM.

    rec_pos is the file offset of the referenced-offset field (after any
    position bytes). `_cXXXXXX` labels resolve mechanically and are asserted
    equal to the ROM value; named labels (must be in `allowed`) resolve from the
    ROM and their value is accumulated in named_offsets for the consistency
    check. Returns the 24-bit offset.
    """
    rom_off = _read_offset(rom, rec_pos)
    mech = common.resolve_event_addr_label(label)
    if mech is not None:
        if not (0 <= mech < EVENT_BLOCK_SIZE):
            raise ParseError(path, 0,
                             "address label {} -> offset ${:06x} outside the "
                             "event block".format(label, mech))
        if mech != rom_off:
            raise ParseError(path, 0,
                             "ROM MISMATCH at file ${:06x}: label {} -> "
                             "${:06x} but ROM holds ${:06x}"
                             .format(rec_pos, label, mech, rom_off))
        return mech
    if label not in allowed:
        raise ParseError(path, 0,
                         "unknown event label {!r} (not `_cXXXXXX`, not in the "
                         "closed named set {}) — escalate"
                         .format(label, sorted(allowed)))
    if not (0 <= rom_off < EVENT_BLOCK_SIZE):
        raise ParseError(path, 0,
                         "named label {} -> ROM offset ${:06x} outside the "
                         "event block".format(label, rom_off))
    named_offsets.setdefault(label, set()).add(rom_off)
    return rom_off


class Resolved(object):
    """Everything the emitters need after ROM resolution + cross-check."""

    def __init__(self):
        self.trigger_records = None      # list[TriggerRecord] (offset filled)
        self.trigger_offsets = None      # list[int], map_slots + 1
        self.map_init_offsets = None     # list[int], MAP_INIT_COUNT
        self.event_return_offset = None  # int
        self.trigger_pad = None          # (fill_byte, pad_len)


def resolve(source_root, records, record_offsets, map_init_labels):
    """Resolve every label against the ROM and cross-check the whole c4 block."""
    rom = common.load_vanilla_rom(source_root)
    et_base = common.hirom_file_offset(EVENT_TRIGGER_SNES)
    mi_base = common.hirom_file_offset(MAP_INIT_SNES)
    ptr_bytes = (EVENT_TRIGGER_MAP_SLOTS + 1) * 2  # 416 map words + end word
    rec_base = et_base + ptr_bytes                 # records start here (0x0342)

    res = Resolved()
    named_offsets = {}

    # --- event trigger records: resolve offsets, verify x/y + bytes vs ROM ---
    for i, rec in enumerate(records):
        pos = rec_base + i * EVENT_TRIGGER_ITEM_SIZE
        if rom[pos] != rec.pos_x or rom[pos + 1] != rec.pos_y:
            raise ParseError("event_trigger.asm", rec.lineno,
                             "ROM MISMATCH: record {} x/y {},{} but ROM holds "
                             "{},{}".format(i, rec.pos_x, rec.pos_y,
                                            rom[pos], rom[pos + 1]))
        rec.offset = _resolve_event_offset(
            rom, pos + 2, rec.label, EVENT_TRIGGER_NAMED, named_offsets,
            "event_trigger.asm")

    # --- ptr table: every map word == 0x342 + 5*record_index, end word too ---
    for map_idx in range(EVENT_TRIGGER_MAP_SLOTS):
        word = rom[et_base + 2 * map_idx] | (rom[et_base + 2 * map_idx + 1] << 8)
        expect = ptr_bytes + record_offsets[map_idx] * EVENT_TRIGGER_ITEM_SIZE
        if word != expect:
            raise ParseError("event_trigger.asm", 0,
                             "PTR MISMATCH map {}: ROM ${:04x} != expected "
                             "${:04x}".format(map_idx, word, expect))
    end_word = (rom[et_base + 2 * EVENT_TRIGGER_MAP_SLOTS]
                | (rom[et_base + 2 * EVENT_TRIGGER_MAP_SLOTS + 1] << 8))
    end_expect = ptr_bytes + EVENT_TRIGGER_RECORD_COUNT * EVENT_TRIGGER_ITEM_SIZE
    if end_word != end_expect:
        raise ParseError("event_trigger.asm", 0,
                         "PTR END MISMATCH: ROM ${:04x} != expected ${:04x}"
                         .format(end_word, end_expect))

    # --- fixed_block tail: pure padding (recorded, not emitted) ---
    used = ptr_bytes + EVENT_TRIGGER_RECORD_COUNT * EVENT_TRIGGER_ITEM_SIZE
    pad_len = EVENT_TRIGGER_BLOCK_SIZE - used
    if pad_len < 0:
        raise ParseError("event_trigger.asm", 0,
                         "block overflow: used ${:x} > block ${:x}"
                         .format(used, EVENT_TRIGGER_BLOCK_SIZE))
    tail = rom[et_base + used:et_base + EVENT_TRIGGER_BLOCK_SIZE]
    if pad_len and len(set(tail)) != 1:
        raise ParseError("event_trigger.asm", 0,
                         "fixed_block tail is not uniform padding: {!r}"
                         .format(bytes(tail)))
    res.trigger_pad = (tail[0] if pad_len else None, pad_len)

    # --- map init events: resolve + verify vs ROM ---
    map_init_offsets = []
    for i, (label, lineno) in enumerate(map_init_labels):
        pos = mi_base + i * MAP_INIT_ITEM_SIZE
        off = _resolve_event_offset(
            rom, pos, label, MAP_INIT_NAMED, named_offsets, "map_init_event.asm")
        map_init_offsets.append(off)

    # --- named-label consistency (T4) ---
    for label, offs in named_offsets.items():
        if len(offs) != 1:
            raise ParseError("<event>", 0,
                             "named label {} resolves to multiple offsets {} — "
                             "escalate".format(label,
                                               ["${:06x}".format(o) for o in
                                                sorted(offs)]))
    if "EventReturn" not in named_offsets:
        raise ParseError("map_init_event.asm", 0,
                         "EventReturn never referenced — cannot resolve "
                         "kEventReturnScript")
    res.event_return_offset = next(iter(named_offsets["EventReturn"]))
    res.trigger_records = records
    res.trigger_offsets = record_offsets
    res.map_init_offsets = map_init_offsets
    return res


# --- emitters ----------------------------------------------------------------

def _banner(source_lines):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    return ("// AUTO-GENERATED by tools/asm_parser/parse_event_triggers.py\n"
            + body +
            "// (original-src pinned at 1ea47b5; values cross-checked vs the\n"
            "// vanilla ROM byte-for-byte over the whole block)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_event_triggers.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


def render_event_trigger_inc(records, event_return_offset):
    out = [_banner(["event/event_trigger.asm (EventTrigger, ROM c4/0342)"]),
           "// EventTrigger records in physical order, #included inside the\n"
           "// kEventTriggers array in src/data/event_triggers.cpp. Records are\n"
           "// shared across maps via the offset table. Coordinates are decimal;\n"
           "// the event-script reference is an opaque 24-bit offset (hex).\n\n"]
    for r in records:
        out.append(
            "    EventTrigger{{ .posX = {0}, .posY = {1}, "
            ".event = EventScriptRef::at(0x{2:05X}) }},\n"
            .format(r.pos_x, r.pos_y, r.offset))
    return "".join(out)


def render_trigger_offsets_inc(record_offsets):
    out = [_banner(["event/event_trigger.asm (EventTriggerPtrs, ROM c4/0000)"]),
           "// MapTriggerOffsetEntry rows in map-id order, #included inside the\n"
           "// offset array in src/data/event_triggers.cpp. Each row carries its\n"
           "// map id as the typed .index identity field alongside the RECORD\n"
           "// index at which that map's triggers begin. The ptr table has 416\n"
           "// map slots — one more than map_prop's 415 rows — and slot _415 is\n"
           "// empty. The final entry's .index is the map-slot count (416) and\n"
           "// its .offset the record count (end marker); a map's triggers are\n"
           "// the half-open slice [offset[map], offset[map + 1]). A compile-time\n"
           "// assert verifies .index == array position.\n\n"]
    for i, off in enumerate(record_offsets):
        out.append("    MapTriggerOffsetEntry{{ .index = {}, .offset = {} }},\n"
                   .format(i, off))
    return "".join(out)


def render_map_init_inc(map_init_offsets, event_return_offset):
    out = [_banner(["event/map_init_event.asm (MapInitEvent, ROM d1/fa00)"]),
           "// MapInitEventEntry rows in map-id order, #included inside the\n"
           "// kMapInitEvents array in src/data/event_triggers.cpp. Each row is a\n"
           "// map's startup-event reference; the map id is the typed .index\n"
           "// identity (a placeholder decimal id — real map names are welcome at\n"
           "// post-port tidy-up). kEventReturnScript marks a map with no startup\n"
           "// event (field/init.asm:509 skips a script whose first opcode is\n"
           "// $fe/return). A compile-time assert verifies .index == array\n"
           "// position.\n\n"]
    for i, off in enumerate(map_init_offsets):
        if off == event_return_offset:
            ref = "kEventReturnScript"
        else:
            ref = "EventScriptRef::at(0x{:05X})".format(off)
        out.append("    MapInitEventEntry{{ .index = {}, .record = {} }},\n"
                   .format(i, ref))
    return "".join(out)


def render_event_return_inc(event_return_offset):
    """The kEventReturnScript value expression (oracle-resolved from the ROM)."""
    return (_banner(["event/map_init_event.asm (EventReturn label, "
                     "ROM-resolved offset)"]) +
            "// The value of kEventReturnScript: the offset of the EventReturn\n"
            "// label, read from the linked ROM (the label's assembled address\n"
            "// exists nowhere in the data source). #included as the initializer\n"
            "// of the hand-written declaration in include/ostinato/"
            "event_trigger.h.\n"
            "EventScriptRef::at(0x{:05X})\n".format(event_return_offset))


def _hexbytes(values):
    return ", ".join("0x{:02X}".format(b) for b in values)


def _offset_le3(off):
    return [off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF]


def render_fixture(res):
    out = [_banner(["event/event_trigger.asm + event/map_init_event.asm "
                    "(ROM-assembled bytes)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"]

    # event trigger records (5 raw bytes each)
    out.append("// One raw 5-byte EventTrigger record (posX, posY, 24-bit LE "
               "event offset).\n"
               "struct ExpectedEventTriggerRecord {\n"
               "    std::array<std::uint8_t, 5> bytes;\n};\n\n")
    out.append("inline constexpr std::array<ExpectedEventTriggerRecord, {}>\n"
               "kExpectedEventTriggerRecords = {{{{\n"
               .format(len(res.trigger_records)))
    for r in res.trigger_records:
        row = [r.pos_x, r.pos_y] + _offset_le3(r.offset)
        out.append("    {{ {{ {} }} }},\n".format(_hexbytes(row)))
    out.append("}};\n\n")

    # trigger offset table (record indices, 417 entries)
    out.append("// The per-map trigger offset table ({} entries: 416 map slots "
               "+ end).\n".format(len(res.trigger_offsets)))
    out.append("inline constexpr std::array<std::uint16_t, {}>\n"
               "kExpectedEventTriggerOffsets = {{{{\n"
               .format(len(res.trigger_offsets)))
    for start in range(0, len(res.trigger_offsets), 12):
        chunk = res.trigger_offsets[start:start + 12]
        out.append("    " + ", ".join(str(o) for o in chunk) + ",\n")
    out.append("}};\n\n")

    # map init events (3 raw bytes each)
    out.append("// One raw 3-byte MapInitEvent record (24-bit LE event "
               "offset).\n"
               "struct ExpectedMapInitEventRecord {\n"
               "    std::array<std::uint8_t, 3> bytes;\n};\n\n")
    out.append("inline constexpr std::array<ExpectedMapInitEventRecord, {}>\n"
               "kExpectedMapInitEvents = {{{{\n"
               .format(len(res.map_init_offsets)))
    for off in res.map_init_offsets:
        out.append("    {{ {{ {} }} }},\n".format(_hexbytes(_offset_le3(off))))
    out.append("}};\n\n")

    # the resolved EventReturn offset, so the test can pin kEventReturnScript
    out.append("// The offset of the EventReturn label (kEventReturnScript), "
               "ROM-resolved.\n"
               "inline constexpr std::uint32_t kExpectedEventReturnOffset = "
               "0x{:05X};\n\n".format(res.event_return_offset))
    out.append("}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def load_and_resolve(source_root):
    event_dir = os.path.join(source_root, "src", "event")
    inc_path = os.path.join(source_root, "include", "event", "event_trigger.inc")
    et_path = os.path.join(event_dir, "event_trigger.asm")
    mi_path = os.path.join(event_dir, "map_init_event.asm")

    item_size, array_length = read_event_trigger_inc(inc_path)
    if item_size != EVENT_TRIGGER_ITEM_SIZE:
        raise ParseError(inc_path, 0, "ITEM_SIZE {} != {}"
                         .format(item_size, EVENT_TRIGGER_ITEM_SIZE))
    if array_length != EVENT_TRIGGER_MAP_SLOTS:
        raise ParseError(inc_path, 0, "ARRAY_LENGTH {} != {}"
                         .format(array_length, EVENT_TRIGGER_MAP_SLOTS))

    records, record_offsets = parse_event_trigger_asm(et_path)
    if len(records) != EVENT_TRIGGER_RECORD_COUNT:
        raise ParseError(et_path, 0, "record count {} != {}"
                         .format(len(records), EVENT_TRIGGER_RECORD_COUNT))
    map_init_labels = parse_map_init_asm(mi_path)
    return resolve(source_root, records, record_offsets, map_init_labels)


def run(source_root, repo_root, check_only=False):
    res = load_and_resolve(source_root)
    if check_only:
        fill = res.trigger_pad[0]
        print("OK: event_trigger {} records / {} map slots (+end) / map_init {} "
              "records; ptr table + all bytes match the ROM; EventReturn = "
              "$0{:04X}; fixed_block tail {} B of 0x{:02X}."
              .format(len(res.trigger_records), EVENT_TRIGGER_MAP_SLOTS,
                      len(res.map_init_offsets), res.event_return_offset,
                      res.trigger_pad[1], fill if fill is not None else 0))
        return 0

    gen = os.path.join(repo_root, "src", "data", "generated")
    fix = os.path.join(repo_root, "tests", "fixtures")
    _write(os.path.join(gen, "event_trigger_data.inc"),
           render_event_trigger_inc(res.trigger_records, res.event_return_offset))
    _write(os.path.join(gen, "event_trigger_offsets_data.inc"),
           render_trigger_offsets_inc(res.trigger_offsets))
    _write(os.path.join(gen, "map_init_event_data.inc"),
           render_map_init_inc(res.map_init_offsets, res.event_return_offset))
    _write(os.path.join(gen, "event_return_script.inc"),
           render_event_return_inc(res.event_return_offset))
    _write(os.path.join(fix, "event_trigger_expected.h"), render_fixture(res))
    print("Emitted event-trigger family (4 generated + 1 fixture) -> {}"
          .format(gen))
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
