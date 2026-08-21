#!/usr/bin/env python3
"""Emit the world-map modification system, the vehicle event references, and the
world sine table from original-src, with the vanilla ROM as the byte oracle.

Four units live in src/world/, all `.include`d into world_main.asm:

  * WorldModDataPtrs (world/world_data.asm:15-18) — three 24-bit faraddrs at
    ee/b260 naming each world's modification list. Entry N+1 doubles as the
    terminator: ModifyMap (world/init.asm:1931-1934) takes the length as a
    16-bit subtraction of consecutive pointers, valid because all three targets
    share bank $ce. Only two of the three are maps; the third is the end.
  * The modification lists (world/init.asm:2010-2014) — world_1_mod.dat and
    world_2_mod.dat, contiguous at ce/f600, 4-byte chunks of
    {u16 event-bit index, u16 patch offset}. A chunk whose event bit is clear is
    skipped (init.asm:1951); a set bit applies the patch record at the offset.
    The offset is relative to the start of the world_mod block, not to the pool
    (init.asm:1954-1955 adds the low word of WorldModDataPtrs).
  * VehicleEvent_00..06 (world/world_data.asm:21-40) — seven 24-bit
    `label - EventScript` offsets at ee/b269, the same block-relative form as
    every other event reference in the port. Consumers re-add the base.
  * WorldSineTbl (world/world_data.asm:53) — 271 bytes at ef/fef1,
    floor(|sin(2*pi*x/360)| * 255) for x = 0..270, per the generator comment.

The patch tile pool (WorldModTiles) is deliberately NOT emitted: it holds tilemap
tile indices, so it ships as a runtime-loaded data asset rather than compiled-in
bytes. Its bytes are still covered here — the whole-block ROM cross-check below
spans them, which is a port-time proof independent of how they reach the runtime.

Cross-checks, all hard errors:
  * The three ROM blocks are reassembled from the .dat files and the parsed
    symbols, and every byte is compared to the vanilla ROM: world_mod
    (ce/f600, fixed_block $0500), world_data (ee/b260, fixed_block $30), and
    world_sine (ef/fef1). Both fixed_block tails must be uniform padding.
  * Each vehicle event's offset is read from the ROM AND derived independently
    from the `; ca/XXXX` address comment on the target `.proc` in
    event/event_main.asm (reached through that file's `.export ... :=` alias).
    The two must agree, so neither source is trusted alone.
  * The sine table matches the generator formula over all 271 entries.
  * No event-bit index sets bit 15 (masked off by the consumer at
    init.asm:1945), every patch offset lands inside the pool, and every patch
    record's header and payload fit within the block.

Requires the vanilla ROM (FF6_VANILLA_ROM env var, or a .smc under
<source-root>/vanilla/). Port-time tooling (NOT a build/CI dependency): the
emitted .inc / fixture are committed; CI re-verifies them via this parser's e2e
test and the C++ memcmp suite.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_world_data.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import math
import os
import re
import sys

import common
from common import ParseError

# --- fixed layout facts (all verified against the ROM at emit time) ----------

# world_mod (ce/f600), world_data (ee/b260), world_sine (ef/fef1).
WORLD_MOD_SNES = 0xCEF600
WORLD_DATA_SNES = 0xEEB260
WORLD_SINE_SNES = 0xEFFEF1

WORLD_MOD_BLOCK_SIZE = 0x0500   # fixed_block $0500
WORLD_DATA_BLOCK_SIZE = 0x30    # fixed_block $30

MODIFICATION_SIZE = 4           # {u16 event bit, u16 patch offset}
FARADDR_SIZE = 3

WORLD_SINE_LENGTH = 271         # x = 0..270

VEHICLE_EVENT_COUNT = 7

# The modification pointer table names two worlds plus the end terminator.
MOD_PTR_SYMBOLS = ("World1ModData", "World2ModData", "WorldModDataEnd")
WORLD_MOD_COUNT = len(MOD_PTR_SYMBOLS) - 1

# The .dat files backing the two modification lists and the (unemitted) pool,
# in the physical order init.asm links them.
MOD_LIST_FILES = ("world_1_mod.dat", "world_2_mod.dat")
MOD_POOL_FILE = "world_mod_tiles.dat"

# The valid event-offset range: [0, size of the event-script block).
EVENT_BLOCK_SIZE = 0x2E600

# --- source grammar ----------------------------------------------------------

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
_RE_VEHICLE_LABEL = re.compile(r"^VehicleEvent_(\d+):$")
_RE_FARADDR_PLAIN = re.compile(r"^\.faraddr\s+([A-Za-z_][A-Za-z0-9_]*)$")
_RE_FARADDR_EVENT = re.compile(
    r"^\.faraddr\s+([A-Za-z_][A-Za-z0-9_]*)\s*-\s*EventScript$")
_RE_INCBIN = re.compile(r"^\.incbin\s+\"([^\"]+)\"$")
_RE_FIXED_BLOCK = re.compile(r"^fixed_block\s+(\S+)$")
_RE_SEGMENT = re.compile(r"^\.segment\s+\"([^\"]+)\"$")
_RE_EXPORT_ALIAS = re.compile(
    r"^\.export\s+([A-Za-z_][A-Za-z0-9_]*)\s*:=\s*([A-Za-z_][A-Za-z0-9_]*)$")
_RE_PROC = re.compile(r"^\.proc\s+([A-Za-z_][A-Za-z0-9_]*)")
_RE_ADDR_COMMENT = re.compile(r"^([0-9a-f]{2})/([0-9a-f]{4})$")


class WorldData(object):
    """Everything parsed out of world/world_data.asm."""

    def __init__(self):
        self.mod_ptr_symbols = []    # list[str] in table order
        self.vehicle_events = []     # list[(ordinal, event_script_symbol)]
        self.sine_file = None        # incbin filename behind WorldSineTbl
        self.data_block_size = None  # the fixed_block operand


def parse_world_data_asm(path):
    """Parse world_data.asm: the mod pointer table, vehicle events, sine incbin.

    Every line must match the expected grammar; anything unrecognised is a hard
    error rather than a silent skip, so a source change surfaces here.
    """
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    out = WorldData()
    current = None  # the label whose body we are inside

    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()

        m = _RE_FIXED_BLOCK.match(s)
        if m:
            out.data_block_size = common.parse_int_literal(m.group(1))
            if out.data_block_size is None:
                raise ParseError(path, idx + 1,
                                 "unparsable fixed_block operand {!r}"
                                 .format(m.group(1)))
            continue

        m = _RE_LABEL.match(s)
        if m:
            current = m.group(1)
            vm = _RE_VEHICLE_LABEL.match(s)
            if vm:
                out.vehicle_events.append([int(vm.group(1)), None])
            continue

        m = _RE_FARADDR_EVENT.match(s)
        if m:
            if not out.vehicle_events or out.vehicle_events[-1][1] is not None:
                raise ParseError(path, idx + 1,
                                 "event faraddr outside a VehicleEvent_NN label")
            out.vehicle_events[-1][1] = m.group(1)
            continue

        m = _RE_FARADDR_PLAIN.match(s)
        if m:
            if current != "WorldModDataPtrs":
                raise ParseError(path, idx + 1,
                                 "plain faraddr outside WorldModDataPtrs "
                                 "(inside {!r})".format(current))
            out.mod_ptr_symbols.append(m.group(1))
            continue

        m = _RE_INCBIN.match(s)
        if m:
            if current != "WorldSineTbl":
                raise ParseError(path, idx + 1,
                                 "unexpected incbin under {!r}".format(current))
            out.sine_file = m.group(1)
            continue

        # Structural scaffolding we intentionally skip; anything else escalates.
        if (low.startswith(".import") or low.startswith(".segment")
                or low.startswith("end_fixed_block")):
            continue
        raise ParseError(path, idx + 1,
                         "unexpected line in world_data.asm: {!r}".format(s))

    if tuple(out.mod_ptr_symbols) != MOD_PTR_SYMBOLS:
        raise ParseError(path, 0,
                         "WorldModDataPtrs targets {} != expected {}"
                         .format(tuple(out.mod_ptr_symbols), MOD_PTR_SYMBOLS))
    if len(out.vehicle_events) != VEHICLE_EVENT_COUNT:
        raise ParseError(path, 0,
                         "vehicle events {} != expected {}"
                         .format(len(out.vehicle_events), VEHICLE_EVENT_COUNT))
    for position, (ordinal, symbol) in enumerate(out.vehicle_events):
        if ordinal != position:
            raise ParseError(path, 0,
                             "VehicleEvent_{:02d} out of order at position {}"
                             .format(ordinal, position))
        if symbol is None:
            raise ParseError(path, 0,
                             "VehicleEvent_{:02d} has no faraddr".format(ordinal))
    if out.sine_file is None:
        raise ParseError(path, 0, "WorldSineTbl incbin missing")
    if out.data_block_size != WORLD_DATA_BLOCK_SIZE:
        raise ParseError(path, 0,
                         "world_data fixed_block ${:x} != expected ${:x}"
                         .format(out.data_block_size or 0,
                                 WORLD_DATA_BLOCK_SIZE))
    return out


def parse_world_mod_segment(path):
    """Parse the `world_mod` segment out of world/init.asm.

    Returns (block_size, [(label, incbin_filename), ...]) in link order, with
    WorldModDataEnd recorded as a bodyless label so the pointer arithmetic can
    be checked against the .dat sizes.
    """
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    inside = False
    block_size = None
    entries = []      # list[[label, filename_or_None]]

    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        low = s.lower()

        m = _RE_SEGMENT.match(s)
        if m:
            inside = (m.group(1) == "world_mod")
            continue
        if not inside:
            continue
        if low.startswith(".popseg"):
            inside = False
            continue

        m = _RE_FIXED_BLOCK.match(s)
        if m:
            block_size = common.parse_int_literal(m.group(1))
            continue

        m = _RE_LABEL.match(s)
        if m:
            entries.append([m.group(1), None])
            continue

        m = _RE_INCBIN.match(s)
        if m:
            if not entries or entries[-1][1] is not None:
                raise ParseError(path, idx + 1,
                                 "incbin without a preceding fresh label")
            entries[-1][1] = m.group(1)
            continue

        if low.startswith("end_fixed_block"):
            continue
        raise ParseError(path, idx + 1,
                         "unexpected line in the world_mod segment: {!r}"
                         .format(s))

    if block_size != WORLD_MOD_BLOCK_SIZE:
        raise ParseError(path, 0,
                         "world_mod fixed_block ${:x} != expected ${:x}"
                         .format(block_size or 0, WORLD_MOD_BLOCK_SIZE))
    expected = [("World1ModData", MOD_LIST_FILES[0]),
                ("World2ModData", MOD_LIST_FILES[1]),
                ("WorldModDataEnd", None),
                ("WorldModTiles", MOD_POOL_FILE)]
    if [tuple(e) for e in entries] != expected:
        raise ParseError(path, 0,
                         "world_mod segment layout {} != expected {}"
                         .format([tuple(e) for e in entries], expected))
    return block_size, entries


def parse_event_proc_addresses(path):
    """Map every `EventScript_*` export to its target's documented SNES address.

    event_main.asm carries `.export EventScript_X := Y` alias lines and, above
    each `.proc Y`, an address comment of the form `; ca/XXXX`. Returns
    {export_symbol: snes_address}. The address is a second opinion only — the
    ROM is the authority, and the two are asserted equal at resolve time.
    """
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    aliases = {}       # export symbol -> proc name
    proc_addrs = {}    # proc name -> snes address
    pending_addr = None

    for idx, raw in enumerate(lines):
        code, comment = common.strip_comment(raw)
        s = code.strip()

        if comment is not None and not s:
            m = _RE_ADDR_COMMENT.match(comment.strip())
            pending_addr = (int(m.group(1), 16) << 16 | int(m.group(2), 16)
                            if m else None)
            continue
        if not s:
            pending_addr = None
            continue

        m = _RE_EXPORT_ALIAS.match(s)
        if m:
            aliases[m.group(1)] = m.group(2)
            continue

        m = _RE_PROC.match(s)
        if m:
            if pending_addr is not None:
                name = m.group(1)
                if name in proc_addrs:
                    raise ParseError(path, idx + 1,
                                     "duplicate .proc {}".format(name))
                proc_addrs[name] = pending_addr
        pending_addr = None

    return {export: proc_addrs[proc]
            for export, proc in aliases.items() if proc in proc_addrs}


# --- resolution + full-block cross-check -------------------------------------


def _read_u16(buf, pos):
    return buf[pos] | (buf[pos + 1] << 8)


def _read_u24(buf, pos):
    return buf[pos] | (buf[pos + 1] << 8) | (buf[pos + 2] << 16)


class Modification(object):
    """One 4-byte modification chunk: the gating event bit + the patch offset."""

    def __init__(self, world, event_bit, patch_offset):
        self.world = world
        self.event_bit = event_bit
        self.patch_offset = patch_offset


class Resolved(object):
    """Everything the emitters need after parsing + ROM cross-check."""

    def __init__(self):
        self.modifications = None    # list[Modification], both worlds contiguous
        self.mod_offsets = None      # list[int], WORLD_MOD_COUNT + 1
        self.vehicle_offsets = None  # list[(ordinal, symbol, offset)]
        self.sine = None             # bytes, WORLD_SINE_LENGTH
        self.mod_pad = None          # (fill_byte, pad_len)
        self.data_pad = None         # (fill_byte, pad_len)
        self.pool_length = None      # bytes in the (unemitted) patch pool


def _parse_modifications(list_bytes, pool_start, block_used, path):
    """Decode both modification lists into chunks, asserting the grammar."""
    modifications = []
    mod_offsets = []
    running = 0

    for world, blob in enumerate(list_bytes):
        if len(blob) % MODIFICATION_SIZE:
            raise ParseError(path, 0,
                             "world {} modification list {} B is not a whole "
                             "number of {}-byte chunks"
                             .format(world, len(blob), MODIFICATION_SIZE))
        mod_offsets.append(running)
        for pos in range(0, len(blob), MODIFICATION_SIZE):
            event_bit = _read_u16(blob, pos)
            patch_offset = _read_u16(blob, pos + 2)
            if event_bit & 0x8000:
                raise ParseError(path, 0,
                                 "world {} chunk {} sets event-bit high bit "
                                 "(${:04x}) — the consumer masks it off; "
                                 "escalate"
                                 .format(world, pos // MODIFICATION_SIZE,
                                         event_bit))
            if not (pool_start <= patch_offset < block_used):
                raise ParseError(path, 0,
                                 "world {} chunk {} patch offset ${:04x} is "
                                 "outside the pool [${:04x}, ${:04x})"
                                 .format(world, pos // MODIFICATION_SIZE,
                                         patch_offset, pool_start, block_used))
            modifications.append(Modification(world, event_bit, patch_offset))
            running += 1

    mod_offsets.append(running)
    return modifications, mod_offsets


def _assert_patch_records_fit(modifications, block, block_used, path):
    """Every referenced patch record's header and payload must fit the block.

    The record is a u16 destination, one packed `wwww hhhh` size byte, then
    width * height tile bytes (init.asm:1957-1982). Decoding the records is a
    later concern; this only proves each reference lands on a well-formed,
    in-bounds record so a grammar break cannot pass silently.
    """
    for mod in modifications:
        header_end = mod.patch_offset + 3
        if header_end > block_used:
            raise ParseError(path, 0,
                             "patch record at ${:04x} has no room for its "
                             "3-byte header".format(mod.patch_offset))
        packed = block[mod.patch_offset + 2]
        width = (packed >> 4) & 0x0F
        height = packed & 0x0F
        if width == 0 or height == 0:
            raise ParseError(path, 0,
                             "patch record at ${:04x} has a zero dimension "
                             "({}x{})".format(mod.patch_offset, width, height))
        if header_end + width * height > block_used:
            raise ParseError(path, 0,
                             "patch record at ${:04x} ({}x{}) runs past the "
                             "block end".format(mod.patch_offset, width, height))


def _assert_sine_formula(sine, path):
    """The table is floor(|sin(2*pi*x/360)| * 255) for x = 0..270."""
    for x, value in enumerate(sine):
        expected = math.floor(abs(math.sin(2.0 * math.pi * x / 360.0) * 255.0))
        if value != expected:
            raise ParseError(path, 0,
                             "sine[{}] = {} but the generator formula gives {}"
                             .format(x, value, expected))


def _uniform_tail(rom, base, used, block_size, name):
    """Assert a fixed_block tail is uniform padding; return (fill, length)."""
    pad_len = block_size - used
    if pad_len < 0:
        raise ParseError(name, 0,
                         "block overflow: used ${:x} > block ${:x}"
                         .format(used, block_size))
    tail = rom[base + used:base + block_size]
    if pad_len and len(set(tail)) != 1:
        raise ParseError(name, 0,
                         "fixed_block tail is not uniform padding: {!r}"
                         .format(bytes(tail)))
    return (tail[0] if pad_len else None, pad_len)


def resolve(source_root, world_data, mod_lists, pool, sine, export_addrs):
    """Cross-check every block against the ROM and resolve the vehicle events."""
    rom = common.load_vanilla_rom(source_root)
    mod_base = common.hirom_file_offset(WORLD_MOD_SNES)
    data_base = common.hirom_file_offset(WORLD_DATA_SNES)
    sine_base = common.hirom_file_offset(WORLD_SINE_SNES)

    res = Resolved()
    res.pool_length = len(pool)

    # --- world_mod block: lists + pool, every byte, then the padding tail -----
    block = b"".join(mod_lists) + pool
    block_used = len(block)
    if rom[mod_base:mod_base + block_used] != block:
        raise ParseError("world_mod", 0,
                         "ROM MISMATCH: the reassembled world_mod block "
                         "({} B at ce/f600) differs from the ROM".format(block_used))
    res.mod_pad = _uniform_tail(rom, mod_base, block_used,
                                WORLD_MOD_BLOCK_SIZE, "world_mod")

    pool_start = sum(len(b) for b in mod_lists)
    res.modifications, res.mod_offsets = _parse_modifications(
        mod_lists, pool_start, block_used, "world_mod")
    _assert_patch_records_fit(res.modifications, block, block_used, "world_mod")

    # --- world_data block: the mod pointers must match the .dat accounting ----
    running = WORLD_MOD_SNES
    for i, symbol in enumerate(MOD_PTR_SYMBOLS):
        rom_addr = _read_u24(rom, data_base + i * FARADDR_SIZE)
        if rom_addr != running:
            raise ParseError("world_data.asm", 0,
                             "PTR MISMATCH {}: ROM ${:06x} != expected ${:06x}"
                             .format(symbol, rom_addr, running))
        if i < len(mod_lists):
            running += len(mod_lists[i])

    # --- vehicle events: ROM value and the source address comment must agree --
    vehicle_offsets = []
    veh_base = data_base + len(MOD_PTR_SYMBOLS) * FARADDR_SIZE
    for ordinal, symbol in world_data.vehicle_events:
        rom_offset = _read_u24(rom, veh_base + ordinal * FARADDR_SIZE)
        if not (0 <= rom_offset < EVENT_BLOCK_SIZE):
            raise ParseError("world_data.asm", 0,
                             "VehicleEvent_{:02d} -> ROM offset ${:06x} outside "
                             "the event block".format(ordinal, rom_offset))
        if symbol not in export_addrs:
            raise ParseError("world_data.asm", 0,
                             "no documented address for {} in event_main.asm — "
                             "escalate".format(symbol))
        documented = export_addrs[symbol] - common.EVENT_SCRIPT_BASE
        if documented != rom_offset:
            raise ParseError("world_data.asm", 0,
                             "VehicleEvent_{:02d} ({}): ROM holds ${:06x} but "
                             "event_main.asm documents ${:06x} — escalate"
                             .format(ordinal, symbol, rom_offset, documented))
        vehicle_offsets.append((ordinal, symbol, rom_offset))
    res.vehicle_offsets = vehicle_offsets

    res.data_pad = _uniform_tail(
        rom, data_base,
        (len(MOD_PTR_SYMBOLS) + VEHICLE_EVENT_COUNT) * FARADDR_SIZE,
        WORLD_DATA_BLOCK_SIZE, "world_data")

    # --- sine table: ROM bytes, then the generator formula over all entries ---
    if len(sine) != WORLD_SINE_LENGTH:
        raise ParseError("world_sine.dat", 0,
                         "sine table {} B != expected {}"
                         .format(len(sine), WORLD_SINE_LENGTH))
    if rom[sine_base:sine_base + WORLD_SINE_LENGTH] != sine:
        raise ParseError("world_sine.dat", 0,
                         "ROM MISMATCH: world_sine.dat differs from ef/fef1")
    _assert_sine_formula(sine, "world_sine.dat")
    res.sine = sine
    return res


# --- emitters ----------------------------------------------------------------

# The upstream `.proc` name behind each vehicle event, mapped to the enumerator
# it becomes. Derived from the labels themselves; the parser asserts the
# export symbol it sees is one of these, so a renamed or added event escalates
# instead of silently emitting an unnamed row.
VEHICLE_EVENT_NAMES = {
    "EventScript_AirshipDeck": "AIRSHIP_DECK",
    "EventScript_WorldTent": "WORLD_TENT",
    "EventScript_AirshipGround": "AIRSHIP_GROUND",
    "EventScript_EnterPhoenixCave": "ENTER_PHOENIX_CAVE",
    "EventScript_EnterKefkasTower": "ENTER_KEFKAS_TOWER",
    "EventScript_EnterGogosLair": "ENTER_GOGOS_LAIR",
    "EventScript_DoomGazeDefeated": "DOOM_GAZE_DEFEATED",
}


def _banner(source_lines):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    return ("// AUTO-GENERATED by tools/asm_parser/parse_world_data.py\n"
            + body +
            "// (original-src pinned at 1ea47b5; every byte cross-checked\n"
            "// against the vanilla ROM over the whole block)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_world_data.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


def render_modifications_inc(modifications):
    out = [_banner(["world/world_1_mod.dat + world/world_2_mod.dat "
                    "(ROM ce/f600)"]),
           "// The modification chunks of both worlds, contiguous and in physical\n"
           "// order, #included inside the kWorldMapModifications array in\n"
           "// src/data/world_map.cpp. A world's chunks are the half-open slice\n"
           "// named by the offset table. The event bit is a decimal index into\n"
           "// the event-bit array; the patch reference is an opaque offset from\n"
           "// the start of the modification block (hex), resolved against the\n"
           "// patch pool.\n\n"]
    for mod in modifications:
        out.append(
            "    WorldMapModification{{ .bit = EventBitRef::of({0}), "
            ".patch = WorldTilePatchRef::at(0x{1:04X}) }},\n"
            .format(mod.event_bit, mod.patch_offset))
    return "".join(out)


def render_mod_offsets_inc(mod_offsets):
    out = [_banner(["world/world_data.asm (WorldModDataPtrs, ROM ee/b260)"]),
           "// WorldModDataEntry rows in world-map order, #included inside the\n"
           "// offset array in src/data/world_map.cpp. Each row carries its world\n"
           "// map id as the .index identity alongside the CHUNK index at which\n"
           "// that world's modification list begins. Only two worlds have a list;\n"
           "// the final entry's .index is the world count and its .firstChunk the\n"
           "// chunk count (end marker), mirroring the ROM pointer table whose\n"
           "// third entry is the length terminator. A world's chunks are the\n"
           "// half-open slice [firstChunk[world], firstChunk[world + 1]). A\n"
           "// compile-time assert verifies .index == array position.\n\n"]
    for i, first in enumerate(mod_offsets):
        out.append("    WorldModDataEntry{{ .index = {}, .firstChunk = {} }},\n"
                   .format(i, first))
    return "".join(out)


def render_vehicle_events_inc(vehicle_offsets):
    out = [_banner(["world/world_data.asm (VehicleEvent_00..06, ROM ee/b269)"]),
           "// The event script each world-map vehicle action runs, #included\n"
           "// inside the kWorldVehicleEvents array in src/data/world_map.cpp.\n"
           "// Rows are positional pairs in ROM order; the reference is an opaque\n"
           "// offset into the event-script block, the same form every other event\n"
           "// reference in the port uses.\n\n"]
    for _ordinal, symbol, offset in vehicle_offsets:
        out.append(
            "    {{ WorldVehicleEvent::{0}, EventScriptRef::at(0x{1:05X}) }},\n"
            .format(VEHICLE_EVENT_NAMES[symbol], offset))
    return "".join(out)


def render_sine_inc(sine):
    out = [_banner(["world/world_sine.dat (WorldSineTbl, ROM ef/fef1)"]),
           "// {} WorldSineEntry rows (WorldSineTbl, ROM ef/fef1). The row's\n"
           "// identity (.index, the decimal degree 0..{}) is a typed field, not\n"
           "// the array position — a compile-time assert in\n"
           "// src/data/world_map.cpp verifies index == position for every entry.\n"
           "// .amplitude is floor(|sin(2*pi*index/360)| * 255), a magnitude, so it\n"
           "// reads as decimal; the generator formula is asserted over every entry\n"
           "// at emit time. Included inside the hand-written kWorldSine array in\n"
           "// src/data/world_map.cpp, which backs the worldSine() / worldCosine()\n"
           "// accessors that own the angle reduction.\n\n"
           .format(len(sine), len(sine) - 1)]
    for degree, amplitude in enumerate(sine):
        out.append("    {{ .index = {:3d}, .amplitude = {:3d} }},\n"
                   .format(degree, amplitude))
    return "".join(out)


def _hexbytes(values):
    return ", ".join("0x{:02X}".format(b) for b in values)


def render_fixture(res):
    out = [_banner(["world/world_1_mod.dat + world_2_mod.dat + world_data.asm + "
                    "world_sine.dat (ROM-assembled bytes)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"]

    # modification chunks (4 raw bytes each)
    out.append("// One raw 4-byte modification chunk (u16 event-bit index, u16\n"
               "// patch offset), both worlds contiguous in physical order.\n"
               "struct ExpectedWorldMapModification {\n"
               "    std::array<std::uint8_t, 4> bytes;\n};\n\n")
    out.append("inline constexpr std::array<ExpectedWorldMapModification, {}>\n"
               "kExpectedWorldMapModifications = {{{{\n"
               .format(len(res.modifications)))
    for mod in res.modifications:
        row = [mod.event_bit & 0xFF, (mod.event_bit >> 8) & 0xFF,
               mod.patch_offset & 0xFF, (mod.patch_offset >> 8) & 0xFF]
        out.append("    {{ {{ {} }} }},\n".format(_hexbytes(row)))
    out.append("}};\n\n")

    # the per-world chunk offset table, in the same self-labeling row shape
    out.append("// Mirrors ostinato::WorldModDataEntry (src/data/world_map.h)\n"
               "// without depending on it: the per-world modification offset\n"
               "// table ({} entries: {} worlds + end).\n"
               "struct ExpectedWorldModDataEntry {{\n"
               "    std::uint16_t index;\n"
               "    std::uint16_t firstChunk;\n}};\n\n"
               .format(len(res.mod_offsets), WORLD_MOD_COUNT))
    out.append("inline constexpr std::array<ExpectedWorldModDataEntry, {}>\n"
               "kExpectedWorldModOffsets = {{{{\n".format(len(res.mod_offsets)))
    for i, first in enumerate(res.mod_offsets):
        out.append("    {{ .index = {}, .firstChunk = {} }},\n".format(i, first))
    out.append("}};\n\n")

    # vehicle events (3 raw bytes each)
    out.append("// One raw 3-byte vehicle event reference (24-bit LE offset into\n"
               "// the event-script block), in ROM order.\n"
               "struct ExpectedWorldVehicleEvent {\n"
               "    std::array<std::uint8_t, 3> bytes;\n};\n\n")
    out.append("inline constexpr std::array<ExpectedWorldVehicleEvent, {}>\n"
               "kExpectedWorldVehicleEvents = {{{{\n"
               .format(len(res.vehicle_offsets)))
    for _ordinal, _symbol, offset in res.vehicle_offsets:
        row = [offset & 0xFF, (offset >> 8) & 0xFF, (offset >> 16) & 0xFF]
        out.append("    {{ {{ {} }} }},\n".format(_hexbytes(row)))
    out.append("}};\n\n")

    # the sine table, in the same self-labeling row shape
    out.append("// Mirrors ostinato::WorldSineEntry (src/data/world_map.h)\n"
               "// without depending on it: the world sine table ({} entries,\n"
               "// degree 0..{}).\n"
               "struct ExpectedWorldSineEntry {{\n"
               "    std::uint16_t index;\n"
               "    std::uint8_t amplitude;\n}};\n\n"
               .format(WORLD_SINE_LENGTH, WORLD_SINE_LENGTH - 1))
    out.append("inline constexpr std::array<ExpectedWorldSineEntry, {}>\n"
               "kExpectedWorldSine = {{{{\n".format(len(res.sine)))
    for degree, amplitude in enumerate(res.sine):
        out.append("    {{ .index = {:3d}, .amplitude = {:3d} }},\n"
                   .format(degree, amplitude))
    out.append("}};\n\n")

    out.append("}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def load_and_resolve(source_root):
    world_dir = os.path.join(source_root, "src", "world")
    wd_path = os.path.join(world_dir, "world_data.asm")
    init_path = os.path.join(world_dir, "init.asm")
    event_main = os.path.join(source_root, "src", "event", "event_main.asm")

    world_data = parse_world_data_asm(wd_path)
    parse_world_mod_segment(init_path)

    unknown = [s for _o, s in world_data.vehicle_events
               if s not in VEHICLE_EVENT_NAMES]
    if unknown:
        raise ParseError(wd_path, 0,
                         "vehicle event target(s) with no enumerator name: {} — "
                         "escalate".format(unknown))

    mod_lists = [_read_blob(os.path.join(world_dir, name))
                 for name in MOD_LIST_FILES]
    pool = _read_blob(os.path.join(world_dir, MOD_POOL_FILE))
    sine = _read_blob(os.path.join(world_dir, world_data.sine_file))

    export_addrs = parse_event_proc_addresses(event_main)
    return resolve(source_root, world_data, mod_lists, pool, sine, export_addrs)


def _read_blob(path):
    if not os.path.isfile(path):
        raise ParseError(path, 0,
                         "missing rip output — run `make rip` in original-src")
    with open(path, "rb") as fh:
        return fh.read()


def run(source_root, repo_root, check_only=False):
    res = load_and_resolve(source_root)
    if check_only:
        print("OK: {} modification chunks over {} worlds (+end) / {} vehicle "
              "events / sine {} entries; world_mod + world_data + world_sine all "
              "match the ROM; pool {} B (not emitted); mod tail {} B of 0x{:02X}, "
              "data tail {} B of 0x{:02X}."
              .format(len(res.modifications), WORLD_MOD_COUNT,
                      len(res.vehicle_offsets), len(res.sine), res.pool_length,
                      res.mod_pad[1], res.mod_pad[0] or 0,
                      res.data_pad[1], res.data_pad[0] or 0))
        return 0

    gen = os.path.join(repo_root, "src", "data", "generated")
    fix = os.path.join(repo_root, "tests", "fixtures")
    _write(os.path.join(gen, "world_mod_data.inc"),
           render_modifications_inc(res.modifications))
    _write(os.path.join(gen, "world_mod_offsets_data.inc"),
           render_mod_offsets_inc(res.mod_offsets))
    _write(os.path.join(gen, "world_vehicle_events_data.inc"),
           render_vehicle_events_inc(res.vehicle_offsets))
    _write(os.path.join(gen, "world_sine_data.inc"), render_sine_inc(res.sine))
    _write(os.path.join(fix, "world_data_expected.h"), render_fixture(res))
    print("Emitted world-map data (4 generated + 1 fixture) -> {}".format(gen))
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
