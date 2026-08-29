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

# --- world tile properties, song tables, curves, train tile sizes ------------

# Two 256-entry word tables, contiguous: world of balance then world of ruin.
# GetWorldTileProp (world/move.asm:1366-1393) indexes
# WorldTileProp[mapIndex * 256 + tileByte], the map offset being
# (mapIndex << 8) << 1 bytes, which is exactly these two blocks.
WORLD_TILE_PROP_SNES = 0xEE9B14
WORLD_TILE_PROP_ENTRIES = 256
WORLD_TILE_PROP_TABLES = 2

# The bits every consumer reads, and the name each becomes. Anything outside
# this mask has no consumer in the world module; the parser reports which of
# those bits the corpus ever sets and never invents a meaning for one.
TILE_PROP_CONSUMED_MASK = 0x0002 | 0x0010 | 0x0020 | 0x0040 | 0x0700 | 0x2000 \
    | 0x4000 | 0x8000

# The five song tables, contiguous at ee/8389 in this order. Values are SONG::
# symbols from include/sound/song_script.inc.
WORLD_SONG_SNES = 0xEE8389
SONG_TABLES = (
    # (upstream label, entries, C++ stem, accessor doc)
    ("AirshipSongTbl", 4, "airship_song",
     "The airship's song, selected by world (world/init.asm:189)."),
    ("ChocoSongTbl", 4, "chocobo_song",
     "The chocobo's song, selected by world (world/init.asm:431)."),
    ("WorldSongTbl", 4, "world_song",
     "The overworld's song, selected by world (world/init.asm:749)."),
    ("TrainSongTbl", 2, "train_song",
     "The Magitek train ride's song (world/init.asm:982 reads entry 0 only)."),
    ("SnakeSongTbl", 2, "serpent_trench_song",
     "The Serpent Trench's song (world/init.asm:1113)."),
)

# The movement / presentation curves, in (label, source file, SNES address,
# entry count, directive) form. Each length is proven twice: counted from the
# source lines, and confirmed by the block ending exactly where the next
# labelled thing begins, with every byte compared to the ROM.
CURVE_TABLES = (
    ("TrainBattleMosaicTbl", "move.asm", 0xEE1907, 41, "byte"),
    ("BattleZoomTbl", "move.asm", 0xEE224E, 34, "word"),
    ("AirshipDirAnimOffset", "sprite.asm", 0xEE4566, 16, "byte"),
    ("CharMoveFrameTbl", "sprite.asm", 0xEE4842, 16, "byte"),
    ("CharTopHFlipTbl", "sprite.asm", 0xEE4852, 128, "byte"),
    ("CharBtmHFlipTbl", "sprite.asm", 0xEE48D2, 128, "byte"),
    ("_ee4952", "sprite.asm", 0xEE4952, 178, "byte"),
)

# The two h-flip tables are booleans in the corpus; asserted, not assumed.
HFLIP_TABLES = ("CharTopHFlipTbl", "CharBtmHFlipTbl")

# The Magitek train's per-layer tile geometry (world/train_init.asm:4-14).
TRAIN_SIZE_TABLES = (
    ("dtsize", 0xEE99D1, 13),
    ("chr_size", 0xEE99EB, 13),
)

# magitek_train_tiles.dat is NOT transcribed: its 348 words are exactly the
# running prefix sum from $2000 over the non-zero dtsize values in descending
# order, cycled across 29 tiles. The parser proves that over every entry
# instead of copying the blob.
TRAIN_TILE_FILE = "magitek_train_tiles.dat"
TRAIN_TILE_ENTRIES = 348
TRAIN_TILE_BASE = 0x2000
TRAIN_TILE_COUNT = 29

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


# --- generic labelled-table reader -------------------------------------------

_RE_DATA_DIR = re.compile(r"^(?:@[0-9a-f]{4}:)?\s*\.(byte|word)\s+(.+)$")
_RE_ADDR_PREFIX = re.compile(r"^@([0-9a-f]{4}):")
_RE_SCOPED_TERM = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)$")


def _eval_data_term(term, path, lineno, songs):
    """Resolve one .byte/.word term.

    Three forms appear across the tables in scope: an integer literal, a product
    of integer literals (dtsize writes its pixel counts as `10*10`), and a
    SONG:: symbol. Anything else is a hard error rather than a guess.
    """
    term = term.strip()
    if not term:
        raise ParseError(path, lineno, "empty data term")

    scoped = _RE_SCOPED_TERM.match(term)
    if scoped:
        scope, member = scoped.group(1), scoped.group(2)
        if scope != "SONG" or songs is None:
            raise ParseError(path, lineno,
                             "unexpected scoped term {!r}".format(term))
        value = songs.value_of(member)
        if value is None:
            raise ParseError(path, lineno,
                             "unknown SONG symbol {!r}".format(member))
        return value

    product = 1
    for factor in term.split("*"):
        lit = common.parse_int_literal(factor.strip())
        if lit is None:
            raise ParseError(path, lineno,
                             "unparsable data term {!r}".format(term))
        product *= lit
    return product


def read_labeled_table(path, label, directive, expected, songs=None):
    """Read the .byte/.word table under `label`, returning (address, values).

    `label` is either a symbol (`dtsize`) or the address label the source puts
    on the first data line (`@9d14`, for the second tile-property table, which
    upstream leaves unnamed). The body ends at the first line that is not a data
    directive of the requested kind, so the entry count comes from the source
    rather than from a constant here; the caller's expected count is asserted
    against it and every value is then compared to the ROM.
    """
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    start = None
    wanted = "{}:".format(label)
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if s == wanted:
            start = idx + 1
            break
        if label.startswith("@") and s.startswith(wanted):
            start = idx          # the label sits on the first data line itself
            break
    if start is None:
        raise ParseError(path, 0, "label {} not found".format(wanted))

    address = None
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
            # A stacked alias label above the data (train_init.asm writes
            # `_ee99d1:` then `dtsize:`) is scaffolding, not a body line.
            if _RE_LABEL.match(s):
                continue
            raise ParseError(path, idx + 1,
                             "unexpected line under {}: {!r}".format(label, s))
        if m.group(1) != directive:
            if values:
                break
            raise ParseError(path, idx + 1,
                             "{} opens with .{} but .{} was expected"
                             .format(label, m.group(1), directive))

        addr_match = _RE_ADDR_PREFIX.match(s)
        if addr_match and address is None:
            address = int(addr_match.group(1), 16)
        for term in m.group(2).split(","):
            values.append(_eval_data_term(term, path, idx + 1, songs))

    if address is None:
        raise ParseError(path, 0, "{} has no @addr: line".format(label))
    if len(values) != expected:
        raise ParseError(path, 0,
                         "{} holds {} entries, expected {}"
                         .format(label, len(values), expected))
    return address, values


def _assert_rom_table(rom, snes, values, width, name):
    """Every entry of a parsed table must match the ROM at its documented
    address. Width is 1 for .byte tables, 2 for little-endian .word tables."""
    base = common.hirom_file_offset(snes)
    if width == 1:
        actual = list(rom[base:base + len(values)])
    else:
        actual = [_read_u16(rom, base + i * 2) for i in range(len(values))]
    if actual != list(values):
        for i, (got, want) in enumerate(zip(actual, values)):
            if got != want:
                raise ParseError(name, 0,
                                 "ROM MISMATCH at {}[{}]: ROM ${:0{w}x} != "
                                 "source ${:0{w}x}"
                                 .format(name, i, got, want, w=width * 2))
        raise ParseError(name, 0, "ROM MISMATCH: {} length differs".format(name))


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


# --- world tile properties, songs, curves, train sizes -----------------------


class ResolvedTiles(object):
    """The s2 units, parsed from source and confirmed against the ROM."""

    def __init__(self):
        self.tile_props = None       # list[list[int]], one per world
        self.residual_bits = None    # {bit mask: count} for unconsumed bits set
        self.songs = None            # list[(label, stem, doc, [(name, value)])]
        self.curves = None           # {label: (address, [values])}
        self.train_sizes = None      # {label: (address, [values])}
        self.train_tile_offsets = None   # the derived (not transcribed) sequence


def _derive_train_tile_offsets(dtsize):
    """Rebuild magitek_train_tiles.dat instead of transcribing it.

    The ROM baked a precomputed prefix sum the SNES could not afford at load
    time: starting from $2000, it walks the non-zero dtsize values in descending
    order, cycling that 12-step sequence across 29 tiles, emitting the running
    total before each step. Returns the derived sequence for comparison.
    """
    steps = sorted((v for v in dtsize if v), reverse=True)
    offsets = []
    running = TRAIN_TILE_BASE
    for _tile in range(TRAIN_TILE_COUNT):
        for step in steps:
            offsets.append(running)
            running += step
    return offsets


def _assert_train_tile_derivation(source_root, dtsize):
    """Prove every entry of magitek_train_tiles.dat is derived, then drop it."""
    path = os.path.join(source_root, "src", "world", TRAIN_TILE_FILE)
    blob = _read_blob(path)
    if len(blob) != TRAIN_TILE_ENTRIES * 2:
        raise ParseError(TRAIN_TILE_FILE, 0,
                         "{} B is not {} words"
                         .format(len(blob), TRAIN_TILE_ENTRIES))
    actual = [_read_u16(blob, i * 2) for i in range(TRAIN_TILE_ENTRIES)]
    derived = _derive_train_tile_offsets(dtsize)
    if len(derived) != TRAIN_TILE_ENTRIES:
        raise ParseError(TRAIN_TILE_FILE, 0,
                         "derivation produced {} entries, expected {}"
                         .format(len(derived), TRAIN_TILE_ENTRIES))
    for i, (got, want) in enumerate(zip(actual, derived)):
        if got != want:
            raise ParseError(TRAIN_TILE_FILE, 0,
                             "DERIVATION MISMATCH at [{}]: file ${:04x} != "
                             "prefix sum ${:04x} — the blob carries authored "
                             "information after all; stop and re-audit"
                             .format(i, got, want))
    return derived


def _residual_tile_prop_bits(tables):
    """T-2: report which unconsumed bits the corpus sets, never name them."""
    counts = {}
    for values in tables:
        for value in values:
            residual = value & ~TILE_PROP_CONSUMED_MASK & 0xFFFF
            bit = 1
            while bit <= 0x8000:
                if residual & bit:
                    counts[bit] = counts.get(bit, 0) + 1
                bit <<= 1
    return counts


def resolve_world_tiles(source_root, rom):
    """Parse and ROM-verify every s2 unit."""
    world_dir = os.path.join(source_root, "src", "world")
    song_inc = os.path.join(source_root, "include", "sound", "song_script.inc")

    parsed_songs = common.parse_ca65_constants(song_inc)
    song_enum = parsed_songs.enum("SONG")
    if song_enum is None:
        raise ParseError(song_inc, 0, "no SONG enum in song_script.inc")

    res = ResolvedTiles()

    # --- tile properties: two 256-word tables, the second one unnamed --------
    tile_path = os.path.join(world_dir, "tile_prop.asm")
    balance_addr, balance = read_labeled_table(
        tile_path, "WorldTileProp", "word", WORLD_TILE_PROP_ENTRIES)
    ruin_addr, ruin = read_labeled_table(
        tile_path, "@9d14", "word", WORLD_TILE_PROP_ENTRIES)
    if balance_addr != (WORLD_TILE_PROP_SNES & 0xFFFF):
        raise ParseError(tile_path, 0,
                         "WorldTileProp at @{:04x}, expected @{:04x}"
                         .format(balance_addr, WORLD_TILE_PROP_SNES & 0xFFFF))
    if ruin_addr != balance_addr + WORLD_TILE_PROP_ENTRIES * 2:
        raise ParseError(tile_path, 0,
                         "the second tile-property table starts at @{:04x}, "
                         "not directly after the first (@{:04x})"
                         .format(ruin_addr,
                                 balance_addr + WORLD_TILE_PROP_ENTRIES * 2))
    _assert_rom_table(rom, WORLD_TILE_PROP_SNES, balance + ruin, 2,
                      "WorldTileProp")
    res.tile_props = [balance, ruin]
    res.residual_bits = _residual_tile_prop_bits(res.tile_props)

    # --- song tables: contiguous, values resolved through the SONG enum ------
    by_value = {}
    for member in song_enum.members:
        by_value.setdefault(member.value, member.name)

    init_path = os.path.join(world_dir, "init.asm")
    songs = []
    running = WORLD_SONG_SNES
    for label, count, stem, doc in SONG_TABLES:
        addr, values = read_labeled_table(init_path, label, "byte", count,
                                          songs=song_enum)
        if addr != (running & 0xFFFF):
            raise ParseError(init_path, 0,
                             "{} at @{:04x}, expected @{:04x}"
                             .format(label, addr, running & 0xFFFF))
        _assert_rom_table(rom, running, values, 1, label)
        rows = []
        for value in values:
            name = by_value.get(value)
            if name is None:
                raise ParseError(init_path, 0,
                                 "{} holds ${:02x}, which no SONG symbol names "
                                 "— escalate".format(label, value))
            rows.append((name, value))
        songs.append((label, stem, doc, rows))
        running += count
    res.songs = songs

    # --- movement / presentation curves --------------------------------------
    curves = {}
    for label, source, snes, count, directive in CURVE_TABLES:
        path = os.path.join(world_dir, source)
        addr, values = read_labeled_table(path, label, directive, count)
        if addr != (snes & 0xFFFF):
            raise ParseError(path, 0,
                             "{} at @{:04x}, expected @{:04x}"
                             .format(label, addr, snes & 0xFFFF))
        _assert_rom_table(rom, snes, values, 1 if directive == "byte" else 2,
                          label)
        if label in HFLIP_TABLES and any(v not in (0, 1) for v in values):
            raise ParseError(path, 0,
                             "{} holds a value outside 0/1 — it is not a "
                             "boolean table; escalate".format(label))
        curves[label] = (snes, values)
    res.curves = curves

    # --- train tile geometry + the derivation proof --------------------------
    train_path = os.path.join(world_dir, "train_init.asm")
    train_sizes = {}
    for label, snes, count in TRAIN_SIZE_TABLES:
        addr, values = read_labeled_table(train_path, label, "word", count)
        if addr != (snes & 0xFFFF):
            raise ParseError(train_path, 0,
                             "{} at @{:04x}, expected @{:04x}"
                             .format(label, addr, snes & 0xFFFF))
        _assert_rom_table(rom, snes, values, 2, label)
        train_sizes[label] = (snes, values)
    res.train_sizes = train_sizes

    dtsize = train_sizes["dtsize"][1]
    chr_size = train_sizes["chr_size"][1]
    for i, (pixels, side) in enumerate(zip(dtsize, chr_size)):
        if pixels != side * side:
            raise ParseError(train_path, 0,
                             "dtsize[{}] = {} but chr_size[{}] = {} squares to "
                             "{} — escalate".format(i, pixels, i, side,
                                                    side * side))
    res.train_tile_offsets = _assert_train_tile_derivation(source_root, dtsize)
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


# --- emitters: the s2 units ---------------------------------------------------

# The C++ identity each curve becomes: the .inc stem and the array it fills.
# Names are descriptive, taken from the upstream comment above each table
# rather than from its address label.
CURVE_EMIT = {
    "TrainBattleMosaicTbl": ("train_battle_mosaic", "kTrainBattleMosaicCurve",
                             "world/move.asm:265-269",
                             ["The mosaic strength stepped through when a "
                              "battle starts on the",
                              "Magitek train ride: it blurs in, holds, and "
                              "blurs back out."]),
    "AirshipDirAnimOffset": ("airship_dir_anim_offset",
                             "kAirshipDirectionAnimationOffsets",
                             "world/sprite.asm:798-804",
                             ["Frame offsets for the airship's facing, four "
                              "rows of four: not turning,",
                              "turning right, turning left, and an unused "
                              "fourth row. Within a row",
                              "the columns are unused, straight, up, down."]),
    "CharMoveFrameTbl": ("char_move_frame", "kCharacterMoveFrames",
                         "world/sprite.asm:1134-1139",
                         ["The sprite frame shown at each step of the "
                          "character's walk cycle, four",
                          "frames per facing."]),
    "CharTopHFlipTbl": ("char_top_hflip", "kCharacterTopHalfFlips",
                        "world/sprite.asm:1143-1152",
                        ["Whether the character's top sprite half is drawn "
                         "mirrored, per action."]),
    "CharBtmHFlipTbl": ("char_btm_hflip", "kCharacterBottomHalfFlips",
                        "world/sprite.asm:1156-1165",
                        ["Whether the character's bottom sprite half is drawn "
                         "mirrored, per action."]),
    "_ee4952": ("grounded_airship_size", "kGroundedAirshipSizeCurve",
                "world/sprite.asm:1169-1182",
                ["The size and position curve for the airship sitting on the "
                 "ground, read",
                 "as it grows and shrinks (world/sprite.asm:986)."]),
}

TRAIN_SIZE_EMIT = {
    "dtsize": ("train_layer_pixel_count", "kTrainLayerPixelCounts",
               "world/train_init.asm:3-7",
               ["Pixels per tile in each Magitek train graphics layer — the "
                "square of the",
                "layer's tile side. Layer 0 is empty; the loader walks layers "
                "downward",
                "(world/train_init.asm:41-46)."]),
    "chr_size": ("train_layer_tile_side", "kTrainLayerTileSides",
                 "world/train_init.asm:11-14",
                 ["The tile height and width of each Magitek train graphics "
                  "layer, in pixels",
                  "(world/train_init.asm:99-107)."]),
}

WORLD_TILE_PROP_EMIT = (
    ("world_tile_prop_balance", "kWorldOfBalanceTileProperties",
     "WORLD_OF_BALANCE", "world/tile_prop.asm:3-36"),
    ("world_tile_prop_ruin", "kWorldOfRuinTileProperties",
     "WORLD_OF_RUIN", "world/tile_prop.asm:40-72"),
)


def render_song_id_header(song_enum):
    width = max(len(m.name) for m in song_enum.members)
    lines = [
        "// AUTO-GENERATED by tools/asm_parser/parse_world_data.py — "
        "DO NOT EDIT.\n"
        "// Regenerate from original-src (pinned at 1ea47b5); hand edits will "
        "be lost.\n"
        "// Source: include/sound/song_script.inc (the SONG enum)\n"
        "#pragma once\n\n#include <cstdint>\n\n",
        "// Every song the sound driver can be asked to play. The id is the "
        "index the\n"
        "// driver takes directly; NONE is the sentinel the game writes when "
        "no song is\n"
        "// selected, not a track.\n",
        "namespace ostinato {\n\n",
        "enum class SongId : std::uint8_t {\n",
    ]
    for member in song_enum.members:
        lines.append("    {:<{w}} = 0x{:02X},\n"
                     .format(member.name, member.value, w=width))
    lines.append("};\n\n")
    lines.append("// The number of real tracks (SILENCE .. ENDING_THEME_2); "
                 "NONE sits outside\n"
                 "// the run as a sentinel.\n"
                 "inline constexpr std::uint8_t kSongCount = {};\n\n"
                 .format(sum(1 for m in song_enum.members
                             if m.name != "NONE")))
    lines.append("}  // namespace ostinato\n")
    return "".join(lines)


def render_tile_prop_inc(values, array_name, source_desc, world_enum, snes):
    out = [_banner(["{} (ROM ee/{:04x})".format(source_desc, snes & 0xFFFF)]),
           "// The {} rows of {}, #included inside that array in\n"
           "// src/data/world_tiles.cpp. The row's identity (.index, the "
           "decimal tile\n"
           "// index the world tilemap stores) is a typed field, not the array "
           "position —\n"
           "// a compile-time assert verifies index == position for every "
           "entry. The\n"
           "// property word is the raw ROM value behind a wrapper whose "
           "accessors name\n"
           "// each bit; it reads as hex because it is a packed word, not a "
           "magnitude.\n"
           "// Keyed by WorldMapId::{}.\n\n"
           .format(len(values), array_name, world_enum)]
    for index, value in enumerate(values):
        out.append("    {{ .index = {:3d}, .properties = "
                   "WorldTileProperties::of(0x{:04X}) }},\n"
                   .format(index, value))
    return "".join(out)


def render_song_inc(rows, label, array_name, doc):
    out = [_banner(["world/init.asm ({}, ROM ee/{:04x})"
                    .format(label, WORLD_SONG_SNES & 0xFFFF)]),
           "// {}\n"
           "// The {} rows of {}, #included inside that array in\n"
           "// src/data/world_tiles.cpp. Each row names its song; the identity\n"
           "// (.index) is the decimal slot the consumer indexes with. A\n"
           "// compile-time assert verifies index == position.\n\n"
           .format(doc, len(rows), array_name)]
    for index, (name, _value) in enumerate(rows):
        out.append("    {{ .index = {}, .song = SongId::{} }},\n"
                   .format(index, name))
    return "".join(out)


def render_curve_inc(values, label, array_name, source_desc, doc, boolean):
    out = [_banner(["{} ({})".format(source_desc, label)]),
           "".join("// {}\n".format(line) for line in doc)
           + "//\n"
           "// The {} rows of {}, #included inside that array in\n"
           "// src/data/world_tiles.cpp. The row's identity (.index) is a "
           "typed field,\n"
           "// not the array position; a compile-time assert verifies\n"
           "// index == position for every entry.\n\n"
           .format(len(values), array_name)]
    for index, value in enumerate(values):
        if boolean:
            out.append("    {{ .index = {:3d}, .flipped = {} }},\n"
                       .format(index, "true" if value else "false"))
        else:
            out.append("    {{ .index = {:3d}, .value = {:3d} }},\n"
                       .format(index, value))
    return "".join(out)


def render_battle_zoom_inc(values):
    out = [_banner(["world/move.asm:1435-1441 (BattleZoomTbl, ROM ee/224e)"]),
           "// The battle-transition zoom: each step sets the Mode 7 zoom "
           "level and the\n"
           "// screen brightness for one frame (world/move.asm:1414-1417). The "
           "row's\n"
           "// identity (.index, the decimal step) is a typed field; a "
           "compile-time\n"
           "// assert verifies index == position. Included inside\n"
           "// kBattleZoomSteps in src/data/world_tiles.cpp.\n\n"]
    for index, word in enumerate(values):
        out.append("    {{ .index = {:2d}, .zoomLevel = {:3d}, "
                   ".screenBrightness = {:3d} }},\n"
                   .format(index, word & 0xFF, (word >> 8) & 0xFF))
    return "".join(out)


def render_train_size_inc(values, label, array_name, source_desc, doc):
    out = [_banner(["{} ({})".format(source_desc, label)]),
           "".join("// {}\n".format(line) for line in doc)
           + "//\n"
           "// The {} rows of {}, #included inside that array in\n"
           "// src/data/world_tiles.cpp. The row's identity (.index, the "
           "decimal layer)\n"
           "// is a typed field; a compile-time assert verifies "
           "index == position.\n\n"
           .format(len(values), array_name)]
    for index, value in enumerate(values):
        out.append("    {{ .index = {:2d}, .value = {:3d} }},\n"
                   .format(index, value))
    return "".join(out)


def render_tiles_fixture(tiles):
    out = [_banner(["world/tile_prop.asm + world/init.asm song tables + "
                    "world/move.asm + world/sprite.asm curves + "
                    "world/train_init.asm (ROM bytes)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"]

    out.append("// The two world tile-property tables, as raw ROM words in "
               "index order.\n"
               "struct ExpectedWorldTilePropTable {\n"
               "    std::array<std::uint16_t, "
               + str(WORLD_TILE_PROP_ENTRIES) + "> words;\n};\n\n")
    out.append("inline constexpr std::array<ExpectedWorldTilePropTable, "
               + str(WORLD_TILE_PROP_TABLES) + ">\n"
               "kExpectedWorldTileProps = { {\n")
    for values in tiles.tile_props:
        out.append("    { {\n")
        for chunk in range(0, len(values), 8):
            out.append("        "
                       + ", ".join("0x{:04X}".format(v)
                                   for v in values[chunk:chunk + 8]) + ",\n")
        out.append("    } },\n")
    out.append("} };\n\n")

    out.append("// Each world song table, as the raw ROM song ids the driver "
               "receives.\n")
    for _label, stem, _doc, rows in tiles.songs:
        out.append("inline constexpr std::array<std::uint8_t, "
                   + str(len(rows)) + ">\n"
                   "kExpected" + _pascal(stem) + " = { { "
                   + _hexbytes([v for _n, v in rows]) + " } };\n")
    out.append("\n")

    out.append("// Each movement / presentation curve, as raw ROM bytes.\n")
    for label, (_snes, values) in sorted(tiles.curves.items()):
        if label == "BattleZoomTbl":
            continue
        stem = CURVE_EMIT[label][0]
        out.append("inline constexpr std::array<std::uint8_t, "
                   + str(len(values)) + ">\n"
                   "kExpected" + _pascal(stem) + " = { {\n")
        for chunk in range(0, len(values), 12):
            out.append("    " + _hexbytes(values[chunk:chunk + 12]) + ",\n")
        out.append("} };\n")
    out.append("\n")

    zoom = tiles.curves["BattleZoomTbl"][1]
    out.append("// The battle zoom steps, as the raw ROM words (low byte zoom "
               "level, high\n// byte screen brightness).\n"
               "inline constexpr std::array<std::uint16_t, "
               + str(len(zoom)) + ">\nkExpectedBattleZoom = { {\n")
    for chunk in range(0, len(zoom), 8):
        out.append("    " + ", ".join("0x{:04X}".format(v)
                                      for v in zoom[chunk:chunk + 8]) + ",\n")
    out.append("} };\n\n")

    for label, (_snes, values) in tiles.train_sizes.items():
        stem = TRAIN_SIZE_EMIT[label][0]
        out.append("inline constexpr std::array<std::uint16_t, "
                   + str(len(values)) + ">\n"
                   "kExpected" + _pascal(stem) + " = { { "
                   + ", ".join(str(v) for v in values) + " } };\n")
    out.append("\n")

    out.append("// The offsets magitek_train_tiles.dat holds, rebuilt from the "
               "layer pixel\n"
               "// counts rather than transcribed: the file is a precomputed "
               "prefix sum, and\n"
               "// the parser proves the derivation over every entry at emit "
               "time.\n"
               "inline constexpr std::array<std::uint16_t, "
               + str(len(tiles.train_tile_offsets)) + ">\n"
               "kExpectedTrainTileOffsets = { {\n")
    for chunk in range(0, len(tiles.train_tile_offsets), 8):
        out.append("    " + ", ".join(
            "0x{:04X}".format(v)
            for v in tiles.train_tile_offsets[chunk:chunk + 8]) + ",\n")
    out.append("} };\n\n")

    out.append("}  // namespace ostinato::test\n")
    return "".join(out)


def _pascal(stem):
    return "".join(part.capitalize() for part in stem.split("_"))


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
    res = resolve(source_root, world_data, mod_lists, pool, sine, export_addrs)
    tiles = resolve_world_tiles(source_root, common.load_vanilla_rom(source_root))
    return res, tiles


def _read_blob(path):
    if not os.path.isfile(path):
        raise ParseError(path, 0,
                         "missing rip output — run `make rip` in original-src")
    with open(path, "rb") as fh:
        return fh.read()


def _residual_report(residual_bits):
    """T-2 surfaces as a printed report, never as an invented name."""
    if not residual_bits:
        return "no tile-property bit outside the consumed mask is ever set"
    return ("tile-property bits with NO consumer are set in the corpus: "
            + ", ".join("${:04x} x{}".format(bit, count)
                        for bit, count in sorted(residual_bits.items()))
            + " — reported, not named")


def run(source_root, repo_root, check_only=False):
    res, tiles = load_and_resolve(source_root)
    if check_only:
        print("OK: {} modification chunks over {} worlds (+end) / {} vehicle "
              "events / sine {} entries; world_mod + world_data + world_sine all "
              "match the ROM; pool {} B (not emitted); mod tail {} B of 0x{:02X}, "
              "data tail {} B of 0x{:02X}."
              .format(len(res.modifications), WORLD_MOD_COUNT,
                      len(res.vehicle_offsets), len(res.sine), res.pool_length,
                      res.mod_pad[1], res.mod_pad[0] or 0,
                      res.data_pad[1], res.data_pad[0] or 0))
        print("OK: {} x {} tile-property entries / {} song tables / {} curves / "
              "{} train size tables; every value matches the ROM; "
              "magitek_train_tiles.dat derived {}/{}."
              .format(WORLD_TILE_PROP_TABLES, WORLD_TILE_PROP_ENTRIES,
                      len(tiles.songs), len(tiles.curves),
                      len(tiles.train_sizes), len(tiles.train_tile_offsets),
                      TRAIN_TILE_ENTRIES))
        print("NOTE: " + _residual_report(tiles.residual_bits))
        return 0

    gen = os.path.join(repo_root, "src", "data", "generated")
    fix = os.path.join(repo_root, "tests", "fixtures")
    inc = os.path.join(repo_root, "include", "ostinato")
    _write(os.path.join(gen, "world_mod_data.inc"),
           render_modifications_inc(res.modifications))
    _write(os.path.join(gen, "world_mod_offsets_data.inc"),
           render_mod_offsets_inc(res.mod_offsets))
    _write(os.path.join(gen, "world_vehicle_events_data.inc"),
           render_vehicle_events_inc(res.vehicle_offsets))
    _write(os.path.join(gen, "world_sine_data.inc"), render_sine_inc(res.sine))
    _write(os.path.join(fix, "world_data_expected.h"), render_fixture(res))

    # --- s2 units -------------------------------------------------------------
    song_inc = os.path.join(source_root, "include", "sound", "song_script.inc")
    song_enum = common.parse_ca65_constants(song_inc).enum("SONG")
    _write(os.path.join(inc, "song_id.h"), render_song_id_header(song_enum))

    for table, ((stem, array_name, world_enum, source_desc), values) in \
            enumerate(zip(WORLD_TILE_PROP_EMIT, tiles.tile_props)):
        _write(os.path.join(gen, stem + "_data.inc"),
               render_tile_prop_inc(
                   values, array_name, source_desc, world_enum,
                   WORLD_TILE_PROP_SNES
                   + table * WORLD_TILE_PROP_ENTRIES * 2))

    for label, stem, doc, rows in tiles.songs:
        _write(os.path.join(gen, "world_song_" + stem.replace("_song", "")
                            + "_data.inc"),
               render_song_inc(rows, label, "k" + _pascal(stem) + "s", doc))

    for label, (_snes, values) in tiles.curves.items():
        if label == "BattleZoomTbl":
            _write(os.path.join(gen, "battle_zoom_data.inc"),
                   render_battle_zoom_inc(values))
            continue
        stem, array_name, source_desc, doc = CURVE_EMIT[label]
        _write(os.path.join(gen, stem + "_data.inc"),
               render_curve_inc(values, label, array_name, source_desc, doc,
                                label in HFLIP_TABLES))

    for label, (_snes, values) in tiles.train_sizes.items():
        stem, array_name, source_desc, doc = TRAIN_SIZE_EMIT[label]
        _write(os.path.join(gen, stem + "_data.inc"),
               render_train_size_inc(values, label, array_name, source_desc,
                                     doc))

    _write(os.path.join(fix, "world_tiles_expected.h"),
           render_tiles_fixture(tiles))
    print("Emitted world-map data + world tile/song/curve data -> {}"
          .format(gen))
    print("NOTE: " + _residual_report(tiles.residual_bits))
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
