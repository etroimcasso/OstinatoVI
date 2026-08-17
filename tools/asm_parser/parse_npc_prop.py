#!/usr/bin/env python3
"""Emit the per-map NPC-properties table from original-src, with the vanilla ROM
as the byte-identity oracle.

`event/npc_prop.asm` places every field-map NPC: 2,193 records of 9 bytes each
(1,953 normal via make_npc + 240 special via make_special_npc) behind a 416-map
pointer table at c4/1a10 (fixed_block $50b0). The records assemble into the same
event-script block the trigger family lives in (EventScript @ ca/0000). Consumer:
field InitNPCs (field/obj.asm:254).

Each record is authored as a macro sequence — make_npc / make_special_npc opens
it, a run of set_npc_* calls override individual properties, end_npc packs the
nine bytes. This parser replays that macro grammar to recover each record's
semantic fields (so the emitted rows name their gfx, palette, speed, movement,
etc. instead of writing raw packed bytes), then packs the nine bytes exactly as
end_npc does and cross-checks every byte against the ROM. The macro
packing logic, the pointer-table math, and every event-label offset are all
verified against ground truth over the whole corpus; any mismatch is a hard error
citing the offending record — the parser is never adjusted to accept a deviation.

Event references (normal/animated records) are stored as (addr - EventScript) &
$ffffff, a 24-bit offset. Address-named `_cXXXXXX` labels resolve mechanically
(addr - $ca0000); `GameEnding` (the one named code label here) and the implicit
EventReturn default resolve from the ROM. Special records store VRAM position +
master-object info in place of the event pointer.

This parser also emits the 13 value-space enum headers the table's typed surface
needs (the 9 NPC_* enums from npc_prop.inc, ObjectSpeed + EventVehicle from
event_cmd.inc, and MapSpriteGfx + MapSpritePal from include/gfx/) per the
Parser-Mediated Transcription Discipline — every transcribed value is machine
emitted. EventDir is reused from the shipped header, not re-emitted.

Requires the vanilla ROM (FF6_VANILLA_ROM env var, or a .smc under
<source-root>/vanilla/). Port-time tooling (NOT a build/CI dependency): the
emitted .inc / fixture / enum headers are committed; CI re-verifies them via this
parser's e2e test and the C++ memcmp suite.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_npc_prop.py --source-root PATH --repo-root PATH
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

# c4/1a10 (NPCPropPtrs); records begin at c4/1d52 (== ptr base + 417*2 = $0342).
NPC_PROP_SNES = 0xC41A10
NPC_ITEM_SIZE = 9
NPC_MAP_SLOTS = 416                # ARRAY_LENGTH $01a0; one more than map_prop
NPC_BLOCK_SIZE = 0x50B0            # fixed_block $50b0
NPC_RECORD_COUNT = 2193            # 1,953 make_npc + 240 make_special_npc

# The valid event-offset range: [0, size of the event-script block).
EVENT_BLOCK_SIZE = 0x2E600

# The one named code label a set_npc_event references (the rest are `_cXXXXXX`);
# EventReturn is the implicit default for a record with no set_npc_event.
NPC_NAMED = frozenset({"GameEnding", "EventReturn"})

# The switch id stored in a record is (switch_id - $0300); make_npc asserts the
# id is >= $0300.
SWITCH_BIAS = 0x0300

# --- generic enum reader (a single .enum block from a shared .inc) ------------
#
# The shared value-space files (event_cmd.inc, the gfx tables) carry richer ca65
# than common.parse_ca65_constants accepts whole-file (macros with .repeat, etc.),
# so a targeted reader extracts exactly the one enum we need. The grammar handled
# is the simple member forms these enums use: bare (auto-increment), = literal,
# and = ALIAS (a same-enum earlier member). Anything else is a hard error.


class EnumMember(object):
    def __init__(self, name, value, kind, symbol=None):
        self.name = name      # upstream member name
        self.value = value    # resolved integer
        self.kind = kind      # 'bare' | 'literal' | 'alias'
        self.symbol = symbol  # referenced member for 'alias'


class NamedEnum(object):
    def __init__(self, name):
        self.name = name
        self.members = []      # list[EnumMember] in source order
        self._by_name = {}

    def add(self, m):
        self.members.append(m)
        self._by_name[m.name] = m.value

    def value_of(self, name):
        return self._by_name.get(name)


_RE_DOC_VALUE = re.compile(r"^=\s*(\S+)")


def read_named_enum(path, enum_name):
    """Extract one `.enum enum_name ... .endenum` block from a ca65 file.

    Returns a NamedEnum with every member (including aliases and any MASK/helper
    members) so callers can both emit a filtered header and resolve values. Other
    enums / directives in the file are skipped. Hard-errors on any member grammar
    the port does not model, citing path:line.
    """
    result = NamedEnum(enum_name)
    state = "outside"     # 'outside' | 'target' | 'other'
    counter = 0
    with open(path, "r", encoding="utf-8") as fh:
        for idx, raw in enumerate(fh):
            code, comment = common.strip_comment(raw)
            s = code.strip()
            if not s:
                continue
            low = s.lower()
            if low.startswith(".enum"):
                parts = s.split(None, 1)
                nm = parts[1].strip() if len(parts) > 1 else ""
                state = "target" if nm == enum_name else "other"
                counter = 0
                continue
            if low.startswith(".endenum"):
                if state == "target":
                    return result
                state = "outside"
                continue
            if state != "target":
                continue
            # a member line inside the target enum
            value, kind, sym, counter = _parse_member(
                s, comment, path, idx + 1, result, counter)
            result.add(EnumMember(s.split("=", 1)[0].strip(), value, kind, sym))
    raise ParseError(path, 0, "enum {} not found".format(enum_name))


def _eval_rhs(rhs, enum, path, lineno, member):
    """Evaluate an enum member RHS: a literal, a `LIT << LIT` shift (the only
    operator these value-space enums use), or a same-enum alias. Returns
    (value, kind, symbol)."""
    lit = common.parse_int_literal(rhs)
    if lit is not None:
        return lit, "literal", None
    if "<<" in rhs:
        parts = [p.strip() for p in rhs.split("<<")]
        if len(parts) == 2:
            a = common.parse_int_literal(parts[0])
            b = common.parse_int_literal(parts[1])
            if a is not None and b is not None:
                return (a << b), "literal", None
    if enum.value_of(rhs) is not None:
        return enum.value_of(rhs), "alias", rhs
    raise ParseError(path, lineno,
                     "enum member {} = {!r}: not a literal, a `LIT << LIT` "
                     "shift, or a prior member".format(member, rhs))


def _parse_member(s, comment, path, lineno, enum, counter):
    if "=" in s and not re.search(r"[=!<>]=", s):
        name, rhs = (x.strip() for x in s.split("=", 1))
        value, kind, sym = _eval_rhs(rhs, enum, path, lineno, name)
    else:
        name, value, kind, sym = s, counter, "bare", None
    # The upstream author's own inline value comment is a free structural check.
    if comment:
        m = _RE_DOC_VALUE.match(comment)
        if m:
            doc = common.parse_int_literal(m.group(1))
            if doc is not None and doc != value:
                raise ParseError(path, lineno,
                                 "member {} computed {} != documented {}"
                                 .format(name, value, doc))
    return value, kind, sym, value + 1


# --- record model ------------------------------------------------------------


class NpcRecord(object):
    """One parsed NPC: its variant, the properties the source set, and (after
    resolution) the resolved event offset. Stores enough to both pack the nine
    bytes and emit a self-labeling builder row."""

    def __init__(self, variant, pos_x, pos_y, switch_id, lineno):
        self.variant = variant            # 'npc' | 'animated' | 'special'
        self.pos_x = pos_x
        self.pos_y = pos_y
        self.switch_id = switch_id         # $0300-based id from make_npc
        self.lineno = lineno
        # event (normal/animated): (label, offset) — offset filled at resolve.
        self.event_label = None            # None => defaults to EventReturn
        self.event_offset = None
        # each optional property: attr -> (member_name, stored_value)
        self.props = {}
        # special-only sub-fields
        self.vram_x = 0
        self.vram_y = 0
        self.h_flip = False
        self.is_32x32 = False
        self.master = None                 # (id, offset, dir_name, dir_value)
        self.is_slave = 0                  # byte 2 bit 1; set_npc_master -> 2


# --- source parser (replays the macro grammar) -------------------------------

_RE_MAP_LABEL = re.compile(r"^NPCProp::_(\d+):")
_RE_MAKE_NPC = re.compile(
    r"^make_npc\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}\s*,\s*(\$[0-9a-fA-F]+)\s*$")
_RE_MAKE_SPECIAL = re.compile(
    r"^make_special_npc\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}\s*,\s*(\$[0-9a-fA-F]+)\s*,"
    r"\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}\s*$")


def parse_npc_asm(path, enums, map_slots=NPC_MAP_SLOTS):
    """Parse npc_prop.asm into per-map record lists by replaying the macro
    grammar. `enums` maps upstream enum name -> NamedEnum for value resolution.
    Returns (records, record_offsets) exactly like the trigger parser."""
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    per_map = [None] * map_slots
    current = None
    macro_depth = 0
    records = []
    rec = None        # record under construction (between make_* and end_npc)

    def resolve_enum(enum_name, member, lineno):
        val = enums[enum_name].value_of(member)
        if val is None:
            raise ParseError(path, lineno,
                             "{}::{} not defined".format(enum_name, member))
        return val

    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        lineno = idx + 1
        low = s.lower()

        # Macro *definitions* at the top of the file are skipped wholesale.
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if low.startswith(".endmac"):
            if macro_depth == 0:
                raise ParseError(path, lineno, ".endmac without .mac")
            macro_depth -= 1
            continue
        if macro_depth > 0:
            continue

        m = _RE_MAP_LABEL.match(s)
        if m:
            if rec is not None:
                raise ParseError(path, lineno, "map label inside an open record")
            current = int(m.group(1))
            if not (0 <= current < map_slots):
                raise ParseError(path, lineno,
                                 "map slot _{} out of range".format(current))
            if per_map[current] is not None:
                raise ParseError(path, lineno,
                                 "duplicate map slot _{}".format(current))
            per_map[current] = []
            continue

        if s.startswith("make_npc") or s.startswith("make_special_npc"):
            if rec is not None:
                raise ParseError(path, lineno, "make_* before end_npc")
            if current is None:
                raise ParseError(path, lineno, "record before any NPCProp::_N")
            rec = _begin_record(s, lineno, path)
            continue

        if s.startswith("end_npc"):
            if rec is None:
                raise ParseError(path, lineno, "end_npc without make_*")
            per_map[current].append(rec)
            records.append(rec)
            rec = None
            continue

        if rec is not None:
            _apply_set(rec, s, lineno, path, resolve_enum)
            continue

        # Structural scaffolding between records; anything else escalates.
        if (s.endswith(":") or low.startswith(".segment")
                or low.startswith(".include") or low.startswith(".global")
                or low.startswith("fixed_block") or low.startswith("ptr_tbl")
                or low.startswith("end_ptr") or low.startswith("end_fixed_block")
                or low.startswith("reset_npc_prop")
                or low.startswith("_npc_") or low.startswith(".define")):
            continue
        raise ParseError(path, lineno,
                         "unexpected line in npc_prop.asm: {!r}".format(s))

    if rec is not None:
        raise ParseError(path, 0, "unterminated record (missing end_npc)")
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
    record_offsets.append(running)
    if running != len(records):
        raise ParseError(path, 0, "record accounting mismatch")
    return records, record_offsets


def _begin_record(s, lineno, path):
    m = _RE_MAKE_SPECIAL.match(s)
    if m:
        rec = NpcRecord("special", int(m.group(1)), int(m.group(2)),
                        _switch_id(m.group(3), lineno, path), lineno)
        rec.vram_x = int(m.group(4))
        rec.vram_y = int(m.group(5))
        return rec
    m = _RE_MAKE_NPC.match(s)
    if m:
        return NpcRecord("npc", int(m.group(1)), int(m.group(2)),
                         _switch_id(m.group(3), lineno, path), lineno)
    raise ParseError(path, lineno, "malformed make_* line: {!r}".format(s))


def _switch_id(token, lineno, path):
    val = common.parse_int_literal(token)
    if val is None or val < SWITCH_BIAS:
        raise ParseError(path, lineno,
                         "switch id {} must be >= $0300".format(token))
    return val


def _apply_set(rec, s, lineno, path, resolve_enum):
    """Apply one set_npc_* macro to the record under construction."""
    parts = s.split(None, 1)
    op = parts[0]
    arg = parts[1].strip() if len(parts) > 1 else ""

    if op == "set_npc_event":
        rec.event_label = arg
    elif op == "set_npc_dir":
        rec.props["dir"] = (arg, resolve_enum("EVENT_DIR", arg, lineno))
    elif op == "set_npc_speed":
        v = resolve_enum("OBJ_SPEED", arg, lineno)
        if v > 3:
            raise ParseError(path, lineno,
                             "speed {} exceeds the 2-bit NPC field".format(arg))
        rec.props["speed"] = (arg, v)
    elif op == "set_npc_gfx":
        _apply_gfx(rec, arg, lineno, path, resolve_enum)
    elif op == "set_npc_vehicle":
        _apply_vehicle(rec, arg, lineno, path, resolve_enum)
    elif op == "set_npc_movement":
        rec.props["movement"] = (arg, resolve_enum("NPC_MOVEMENT", arg, lineno))
    elif op == "set_npc_layer_priority":
        rec.props["layerPriority"] = (
            arg, resolve_enum("NPC_LAYER_PRIORITY", arg, lineno))
    elif op == "set_npc_sprite_priority":
        rec.props["spritePriority"] = (
            arg, resolve_enum("NPC_SPRITE_PRIORITY", arg, lineno))
    elif op == "set_npc_bg2_scroll":
        rec.props["scroll"] = ("BG2", resolve_enum("NPC_SCROLL", "BG2", lineno))
    elif op == "set_npc_no_react":
        rec.props["react"] = ("NONE", resolve_enum("NPC_REACT", "NONE", lineno))
    elif op == "set_npc_anim":
        _apply_anim(rec, arg, lineno, path, resolve_enum)
    elif op == "set_npc_32x32":
        rec.is_32x32 = True
    elif op == "set_npc_h_flip":
        rec.h_flip = True
    elif op == "set_npc_master":
        _apply_master(rec, arg, lineno, path, resolve_enum)
    elif op == "_npc_is_slave":
        # a raw override: a master reference whose slave bit is cleared.
        m = re.match(r"^\.set\s+(\S+)$", arg)
        val = common.parse_int_literal(m.group(1)) if m else None
        if val is None:
            raise ParseError(path, lineno,
                             "malformed _npc_is_slave override {!r}".format(s))
        rec.is_slave = val
    else:
        raise ParseError(path, lineno, "unknown set macro {!r}".format(op))


def _apply_gfx(rec, arg, lineno, path, resolve_enum):
    fields = [a.strip() for a in arg.split(",")]
    gfx = fields[0]
    rec.props["gfx"] = (gfx, resolve_enum("MAP_SPRITE_GFX", gfx, lineno))
    # blank palette => the gfx name IS the palette alias (set_npc_gfx macro).
    pal = fields[1] if len(fields) > 1 and fields[1] else gfx
    pal_val = resolve_enum("MAP_SPRITE_PAL", pal, lineno)
    if pal_val > 7:
        raise ParseError(path, lineno,
                         "palette {} = {} exceeds the 3-bit NPC pal field"
                         .format(pal, pal_val))
    rec.props["pal"] = (pal, pal_val)


def _apply_vehicle(rec, arg, lineno, path, resolve_enum):
    fields = [a.strip() for a in arg.split(",")]
    veh = fields[0]
    rec.props["vehicle"] = (veh, resolve_enum("EVENT_VEHICLE", veh, lineno))
    if len(fields) > 1 and fields[1]:
        # the second arg is SHOW_RIDER ($80); packed as the show-rider bit.
        rec.props["showRider"] = (fields[1],
                                  resolve_enum("EVENT_VEHICLE", fields[1], lineno))


def _apply_anim(rec, arg, lineno, path, resolve_enum):
    fields = [a.strip() for a in arg.split(",")]
    rec.props["animType"] = (fields[0],
                             resolve_enum("NPC_ANIM_TYPE", fields[0], lineno))
    rec.props["animFrame"] = (fields[1],
                              resolve_enum("NPC_ANIM_FRAME", fields[1], lineno))
    if len(fields) > 2 and fields[2]:
        if rec.variant == "special":
            raise ParseError(path, lineno,
                             "special NPC cannot take an animation speed")
        rec.props["animSpeed"] = (
            fields[2], resolve_enum("NPC_ANIM_SPEED", fields[2], lineno))
    # a make_npc with an animation frame is the 'animated' variant.
    if rec.variant == "npc":
        rec.variant = "animated"


def _apply_master(rec, arg, lineno, path, resolve_enum):
    fields = [a.strip() for a in arg.split(",")]
    if len(fields) != 3:
        raise ParseError(path, lineno, "set_npc_master needs 3 args")
    mid = common.parse_int_literal(fields[0])
    moff = common.parse_int_literal(fields[1])
    if mid is None or moff is None or not (0 <= mid <= 0x1F) or \
            not (0 <= moff <= 7):
        raise ParseError(path, lineno, "master id/offset out of range")
    dir_val = resolve_enum("NPC_MASTER_OFFSET_DIR", fields[2], lineno)
    rec.master = (mid, moff, fields[2], dir_val)
    rec.is_slave = 2                       # set_npc_master marks the object a slave


# --- byte packing (mirrors end_npc exactly) ----------------------------------

def _stored_switch(rec):
    return rec.switch_id - SWITCH_BIAS


def _prop(rec, key, default=0):
    entry = rec.props.get(key)
    return entry[1] if entry is not None else default


def pack_record(rec):
    """Pack the nine bytes exactly as end_npc does. For normal/animated records
    rec.event_offset must already be resolved."""
    b = [0] * 9
    switch = _stored_switch(rec)
    pal = _prop(rec, "pal")            # 0-7, shifted <<2 into byte 2
    scroll = _prop(rec, "scroll")      # already $00/$20
    if rec.variant == "special":
        b[0] = (rec.vram_x | (rec.vram_y << 4)) | (0x80 if rec.h_flip else 0)
        mid, moff = (rec.master[0], rec.master[1]) if rec.master else (0, 0)
        b[1] = mid | (moff << 5)
        master_dir = rec.master[3] if rec.master else 0
        b[2] = (master_dir | rec.is_slave | (pal << 2)
                | ((switch & 3) << 6) | scroll)
    else:
        off = rec.event_offset
        b[0] = off & 0xFF
        b[1] = (off >> 8) & 0xFF
        b[2] = (((off >> 16) & 0xFF) | (pal << 2)
                | ((switch & 3) << 6) | scroll)
    b[3] = (switch >> 2) & 0xFF
    if rec.variant == "special":
        b[4] = rec.pos_x | 0x80
    else:
        b[4] = rec.pos_x | (0x80 if _prop(rec, "showRider") == 0x80 else 0)
    b[5] = rec.pos_y | (_prop(rec, "speed", 2) << 6)
    b[6] = _prop(rec, "gfx")
    sprite_pri = _prop(rec, "spritePriority")   # already $00/$10/$20
    movement = _prop(rec, "movement")           # 0-4
    layer = _prop(rec, "layerPriority")         # already $00/$08/$10/$18
    anim_frame = _prop(rec, "animFrame")        # already $00/$20/$40/$60
    anim_type = _prop(rec, "animType")          # 0-3
    if rec.variant == "special":
        b[7] = sprite_pri | movement
    elif anim_frame:
        b[7] = _prop(rec, "animSpeed") | sprite_pri | movement
    else:
        b[7] = ((_prop(rec, "vehicle") << 1) & 0xC0) | sprite_pri | movement
    if rec.variant == "special":
        b[8] = anim_type | (0x04 if rec.is_32x32 else 0) | layer | anim_frame
    elif anim_frame:
        b[8] = anim_type | _prop(rec, "react") | layer | anim_frame
    else:
        b[8] = _prop(rec, "dir", 2) | _prop(rec, "react") | layer
    for i, v in enumerate(b):
        if not (0 <= v <= 0xFF):
            raise ParseError("npc_prop.asm", rec.lineno,
                             "byte {} = {} out of range".format(i, v))
    return b


# --- ROM resolution + full-block cross-check ---------------------------------

class Resolved(object):
    def __init__(self):
        self.records = None
        self.record_offsets = None
        self.event_return_offset = None
        self.pad = None
        self.packed = None       # list[list[int]] the 9 bytes per record


def resolve(source_root, records, record_offsets):
    """Resolve event labels, pack every record, and cross-check the whole NPC
    block (ptr table + records + fixed_block tail) against the ROM."""
    rom = common.load_vanilla_rom(source_root)
    ptr_base = common.hirom_file_offset(NPC_PROP_SNES)
    ptr_bytes = (NPC_MAP_SLOTS + 1) * 2
    rec_base = ptr_base + ptr_bytes
    named_offsets = {}

    # --- resolve event offsets for normal/animated records ---
    # In an NPC record byte 2 is shared (event bank bits 0-1 | pal | switch |
    # scroll), so the event offset is bytes 0-1 plus byte 2's low two bits — not
    # a clean 3-byte read. Address-named labels resolve mechanically and are
    # verified by the full 9-byte pack comparison below; named labels
    # (EventReturn / GameEnding) are read from the ROM.
    for i, rec in enumerate(records):
        if rec.variant == "special":
            continue
        label = rec.event_label if rec.event_label is not None else "EventReturn"
        pos = rec_base + i * NPC_ITEM_SIZE
        rom_off = rom[pos] | (rom[pos + 1] << 8) | ((rom[pos + 2] & 0x03) << 16)
        mech = common.resolve_event_addr_label(label)
        if mech is not None:
            if not (0 <= mech < EVENT_BLOCK_SIZE):
                raise ParseError("npc_prop.asm", rec.lineno,
                                 "label {} -> ${:06x} outside the event block"
                                 .format(label, mech))
            rec.event_offset = mech
        else:
            if label not in NPC_NAMED:
                raise ParseError("npc_prop.asm", rec.lineno,
                                 "unknown event label {!r} — escalate"
                                 .format(label))
            if not (0 <= rom_off < EVENT_BLOCK_SIZE):
                raise ParseError("npc_prop.asm", rec.lineno,
                                 "named label {} -> ${:06x} outside block"
                                 .format(label, rom_off))
            named_offsets.setdefault(label, set()).add(rom_off)
            rec.event_offset = rom_off

    # --- pack every record and compare every byte to the ROM ---
    packed = []
    for i, rec in enumerate(records):
        want = pack_record(rec)
        pos = rec_base + i * NPC_ITEM_SIZE
        have = list(rom[pos:pos + NPC_ITEM_SIZE])
        if want != have:
            raise ParseError("npc_prop.asm", rec.lineno,
                             "ROM MISMATCH record {} (map order): packed {} but "
                             "ROM holds {}".format(
                                 i, ["0x%02X" % x for x in want],
                                 ["0x%02X" % x for x in have]))
        packed.append(want)

    # --- ptr table: every map word == $0342 + 9*record_index, end word too ---
    for map_idx in range(NPC_MAP_SLOTS):
        word = (rom[ptr_base + 2 * map_idx]
                | (rom[ptr_base + 2 * map_idx + 1] << 8))
        expect = ptr_bytes + record_offsets[map_idx] * NPC_ITEM_SIZE
        if word != expect:
            raise ParseError("npc_prop.asm", 0,
                             "PTR MISMATCH map {}: ROM ${:04x} != ${:04x}"
                             .format(map_idx, word, expect))
    end_word = (rom[ptr_base + 2 * NPC_MAP_SLOTS]
                | (rom[ptr_base + 2 * NPC_MAP_SLOTS + 1] << 8))
    end_expect = ptr_bytes + NPC_RECORD_COUNT * NPC_ITEM_SIZE
    if end_word != end_expect:
        raise ParseError("npc_prop.asm", 0,
                         "PTR END MISMATCH: ROM ${:04x} != ${:04x}"
                         .format(end_word, end_expect))

    # --- fixed_block tail: pure padding (recorded, not emitted) ---
    used = ptr_bytes + NPC_RECORD_COUNT * NPC_ITEM_SIZE
    pad_len = NPC_BLOCK_SIZE - used
    if pad_len < 0:
        raise ParseError("npc_prop.asm", 0,
                         "block overflow: used ${:x} > block ${:x}"
                         .format(used, NPC_BLOCK_SIZE))
    tail = rom[ptr_base + used:ptr_base + NPC_BLOCK_SIZE]
    if pad_len and len(set(tail)) != 1:
        raise ParseError("npc_prop.asm", 0,
                         "fixed_block tail not uniform padding: {!r}"
                         .format(bytes(tail)))

    # --- named-label consistency (T4) ---
    for label, offs in named_offsets.items():
        if len(offs) != 1:
            raise ParseError("npc_prop.asm", 0,
                             "named label {} resolves to multiple offsets {}"
                             .format(label, sorted("${:06x}".format(o)
                                                   for o in offs)))
    if "EventReturn" not in named_offsets:
        raise ParseError("npc_prop.asm", 0, "EventReturn never resolved")

    res = Resolved()
    res.records = records
    res.record_offsets = record_offsets
    res.event_return_offset = next(iter(named_offsets["EventReturn"]))
    res.pad = (tail[0] if pad_len else None, pad_len)
    res.packed = packed
    return res


# --- emitters: enum headers --------------------------------------------------

def _enum_banner(source_desc):
    return ("// AUTO-GENERATED by tools/asm_parser/parse_npc_prop.py — "
            "DO NOT EDIT.\n"
            "// Regenerate from original-src (pinned at 1ea47b5); hand edits "
            "will be lost.\n"
            "// Source: {}\n".format(source_desc))


def render_enum_header(enum, type_name, underlying, source_desc,
                       exclude=frozenset(), note=None):
    """Render a NamedEnum as a C++ enum class header. Excluded members are kept
    out of the surface (e.g. bitmask helpers, or bits modeled elsewhere) but were
    still read for value resolution."""
    kept = [m for m in enum.members if m.name not in exclude]
    width = max((len(m.name) for m in kept), default=1)
    lines = [_enum_banner(source_desc), "#pragma once\n\n#include <cstdint>\n\n"]
    if note:
        lines.append("".join("// {}\n".format(l) for l in note))
    lines.append("namespace ostinato {\n\n")
    lines.append("enum class {} : {} {{\n".format(type_name, underlying))
    for m in kept:
        if m.kind == "alias" and m.symbol not in exclude:
            rhs = m.symbol
        else:
            rhs = "0x{:02X}".format(m.value)
        lines.append("    {:<{w}} = {},\n".format(m.name, rhs, w=width))
    lines.append("};\n\n}  // namespace ostinato\n")
    return "".join(lines)


ENUM_HEADERS = [
    # (upstream enum, C++ type, header stem, source-file key, exclude, note)
    ("NPC_SCROLL", "NpcScroll", "npc_scroll", "npc_prop", {"MASK"},
     ["Which background layer an NPC scrolls with (npc_prop byte 2 bit 5).",
      "BG1 is the default; the value is the packed bit, not a logical 0/1."]),
    ("NPC_MOVEMENT", "NpcMovement", "npc_movement", "npc_prop", {"MASK"},
     ["An NPC's autonomous movement behaviour (npc_prop byte 7 bits 0-3)."]),
    ("NPC_SPRITE_PRIORITY", "NpcSpritePriority", "npc_sprite_priority",
     "npc_prop", {"MASK"},
     ["Sprite-vs-sprite draw priority (npc_prop byte 7 bits 4-5). The value is",
      "the packed field (already shifted into bits 4-5)."]),
    ("NPC_LAYER_PRIORITY", "NpcLayerPriority", "npc_layer_priority",
     "npc_prop", {"MASK"},
     ["Sprite-vs-background layer priority (npc_prop byte 8 bits 3-4). The",
      "value is the packed field (already shifted into bits 3-4)."]),
    ("NPC_REACT", "NpcReact", "npc_react", "npc_prop", {"MASK"},
     ["Whether an NPC turns to face the player when activated (npc_prop byte 8",
      "bit 2). The value is the packed bit."]),
    ("NPC_ANIM_TYPE", "NpcAnimType", "npc_anim_type", "npc_prop", {"MASK"},
     ["How an animated NPC cycles frames (npc_prop byte 8 bits 0-1)."]),
    ("NPC_ANIM_FRAME", "NpcAnimFrame", "npc_anim_frame", "npc_prop", {"MASK"},
     ["An animated NPC's frame mode (npc_prop byte 8 bits 5-7). A nonzero value",
      "marks the record as animated. The value is the packed field."]),
    ("NPC_ANIM_SPEED", "NpcAnimSpeed", "npc_anim_speed", "npc_prop", {"MASK"},
     ["An animated NPC's frame rate (npc_prop byte 7 bits 6-7, non-special",
      "animated records only). The value is the packed field."]),
    ("NPC_MASTER_OFFSET_DIR", "NpcMasterOffsetDir", "npc_master_offset_dir",
     "npc_prop", set(),
     ["Which way a special NPC's slave sprite is offset from its master",
      "(npc_prop byte 2 bit 0)."]),
    ("OBJ_SPEED", "ObjectSpeed", "object_speed", "event_cmd", set(),
     ["Object movement speed (shared event-command space). The NPC speed field",
      "(npc_prop byte 5 bits 6-7) is 2-bit, so only SLOWER..FAST occur there."]),
    ("EVENT_VEHICLE", "EventVehicle", "event_vehicle", "event_cmd",
     {"SHOW_RIDER", "HIDE_RIDER"},
     ["Which vehicle an NPC rides (npc_prop byte 7 bits 6-7 on normal records).",
      "SHOW_RIDER ($80) is a separate bit, surfaced as the show-rider flag, not",
      "a vehicle; HIDE_RIDER is an alias of NONE."]),
    ("MAP_SPRITE_GFX", "MapSpriteGfx", "map_sprite_gfx", "map_sprite_gfx",
     set(),
     ["A field NPC's sprite graphics set (npc_prop byte 6; also the actor",
      "index). 165 sprites, TERRA=0 .. SMALL_BIRD_LEFT=164."]),
    ("MAP_SPRITE_PAL", "MapSpritePal", "map_sprite_pal", "map_sprite_pal",
     {"MASK"},
     ["A field NPC's palette (npc_prop byte 2 bits 2-4). Eight base palettes",
      "(0-7); the many aliases name the per-sprite default a palette-less",
      "set_npc_gfx picks (LOCKE = MERCHANT = BROWN_SOLDIER = 1, etc.)."]),
]

ENUM_SOURCE_FILES = {
    "npc_prop": os.path.join("include", "event", "npc_prop.inc"),
    "event_cmd": os.path.join("include", "event_cmd.inc"),
    "map_sprite_gfx": os.path.join("include", "gfx", "map_sprite_gfx.inc"),
    "map_sprite_pal": os.path.join("include", "gfx", "map_sprite_pal.inc"),
}

ENUM_SOURCE_DESC = {
    "npc_prop": "original-src/include/event/npc_prop.inc  (ca65 .enum {})",
    "event_cmd": "original-src/include/event_cmd.inc  (ca65 .enum {})",
    "map_sprite_gfx":
        "original-src/include/gfx/map_sprite_gfx.inc  (ca65 .enum {})",
    "map_sprite_pal":
        "original-src/include/gfx/map_sprite_pal.inc  (ca65 .enum {})",
}


def load_value_enums(source_root):
    """Read every value-space enum this unit needs, keyed by upstream name."""
    enums = {}
    for upstream, _t, _s, src_key, _ex, _n in ENUM_HEADERS:
        path = os.path.join(source_root, ENUM_SOURCE_FILES[src_key])
        enums[upstream] = read_named_enum(path, upstream)
    # EVENT_DIR is reused (shipped header), but the packer still needs its values.
    enums["EVENT_DIR"] = read_named_enum(
        os.path.join(source_root, "include", "const.inc"), "EVENT_DIR")
    return enums


# --- emitters: data .inc + fixture -------------------------------------------

def _data_banner(source_lines):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    return ("// AUTO-GENERATED by tools/asm_parser/parse_npc_prop.py\n"
            + body +
            "// (original-src pinned at 1ea47b5; every record packed and every\n"
            "// byte cross-checked against the vanilla ROM over the whole\n"
            "// block)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_npc_prop.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


# The canonical order fields appear in an emitted builder row (readable + stable).
_EMIT_ORDER = ["pos", "switchId", "event", "gfx", "pal", "speed", "movement",
               "spritePriority", "layerPriority", "scroll", "dir", "react",
               "vehicle", "showRider", "animType", "animFrame", "animSpeed",
               "vramPos", "hFlip", "is32x32", "master"]

_ENUM_TYPE = {
    "gfx": "MapSpriteGfx", "pal": "MapSpritePal", "speed": "ObjectSpeed",
    "movement": "NpcMovement", "spritePriority": "NpcSpritePriority",
    "layerPriority": "NpcLayerPriority", "scroll": "NpcScroll",
    "dir": "EventDir", "react": "NpcReact", "vehicle": "EventVehicle",
    "animType": "NpcAnimType", "animFrame": "NpcAnimFrame",
    "animSpeed": "NpcAnimSpeed",
}


def _emit_fields(rec):
    """The named builder-arg fields for one record, source-faithful (only the
    properties the record actually set; the builder defaults supply the rest)."""
    out = ["pos = {{{}, {}}}".format(rec.pos_x, rec.pos_y),
           "switchId = 0x{:04X}".format(rec.switch_id)]
    if rec.variant != "special" and rec.event_label is not None:
        out.append("event = EventScriptRef::at(0x{:05X})".format(
            rec.event_offset))
    for key in ["gfx", "pal", "speed", "movement", "spritePriority",
                "layerPriority", "scroll", "dir", "react", "vehicle",
                "animType", "animFrame", "animSpeed"]:
        if key in rec.props:
            member = rec.props[key][0]
            out.append("{} = {}::{}".format(key, _ENUM_TYPE[key], member))
    if "showRider" in rec.props:
        out.append("showRider = true")
    if rec.variant == "special":
        out.append("vramPos = {{{}, {}}}".format(rec.vram_x, rec.vram_y))
        if rec.h_flip:
            out.append("hFlip = true")
        if rec.is_32x32:
            out.append("is32x32 = true")
        if rec.master is not None:
            mid, moff, dname, _dv = rec.master
            # set_npc_master marks the object a slave; the plain-NpcMaster
            # builder defaults isSlave false (so an omitted master packs zero),
            # so a slave master states it. The 105 records that clear the bit
            # (a master reference that is not a slave) leave it defaulted.
            slave = ", .isSlave = true" if rec.is_slave else ""
            out.append("master = {{ .id = {}, .offset = {}, "
                       ".dir = NpcMasterOffsetDir::{}{} }}".format(
                           mid, moff, dname, slave))
    return out


def render_npc_data_inc(records):
    out = [_data_banner(["event/npc_prop.asm (NPCProp records, ROM c4/1d52)"]),
           "// NPC records in physical order, #included inside the kNpcRecords\n"
           "// array in src/data/npc_properties.cpp. Each row is a named builder\n"
           "// (::npc / ::animated / ::special) that packs to the exact 9 ROM\n"
           "// bytes; only the properties a record overrides are named, the rest\n"
           "// take the builder defaults (matching reset_npc_prop). Coordinates,\n"
           "// switch ids, master offsets are decimal/hex idioms; event scripts\n"
           "// are opaque offsets; every value space is a named enum.\n\n"]
    for rec in records:
        fields = _emit_fields(rec)
        body = ", ".join(".{}".format(f) for f in fields)
        out.append("    NpcProperties::{}({{ {} }}),\n".format(rec.variant, body))
    return "".join(out)


def render_offsets_inc(record_offsets):
    out = [_data_banner(["event/npc_prop.asm (NPCPropPtrs, ROM c4/1a10)"]),
           "// MapTriggerOffsetEntry rows in map-id order, #included inside the\n"
           "// offset array in src/data/npc_properties.cpp. Each row carries its\n"
           "// map id as the typed .index identity alongside the RECORD index at\n"
           "// which that map's NPCs begin. The ptr table has 416 map slots — one\n"
           "// more than map_prop's 415 rows — and slot _415 is empty; world maps\n"
           "// 0-2 have no NPCs. The final entry's .index is the map-slot count\n"
           "// (416) and its .offset the record count (end marker); a map's NPCs\n"
           "// are the half-open slice [offset[map], offset[map + 1]).\n\n"]
    for i, off in enumerate(record_offsets):
        out.append("    MapTriggerOffsetEntry{{ .index = {}, .offset = {} }},\n"
                   .format(i, off))
    return "".join(out)


def _hexbytes(values):
    return ", ".join("0x{:02X}".format(b) for b in values)


def render_fixture(res):
    out = [_data_banner(["event/npc_prop.asm (ROM-assembled bytes)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n",
           "// One raw 9-byte NPCProp record.\n"
           "struct ExpectedNpcRecord {\n"
           "    std::array<std::uint8_t, 9> bytes;\n};\n\n"]
    out.append("inline constexpr std::array<ExpectedNpcRecord, {}>\n"
               "kExpectedNpcRecords = {{{{\n".format(len(res.packed)))
    for row in res.packed:
        out.append("    {{ {{ {} }} }},\n".format(_hexbytes(row)))
    out.append("}};\n\n")
    out.append("// The per-map NPC offset table ({} entries: 416 map slots + "
               "end).\n".format(len(res.record_offsets)))
    out.append("inline constexpr std::array<std::uint16_t, {}>\n"
               "kExpectedNpcOffsets = {{{{\n".format(len(res.record_offsets)))
    for start in range(0, len(res.record_offsets), 12):
        chunk = res.record_offsets[start:start + 12]
        out.append("    " + ", ".join(str(o) for o in chunk) + ",\n")
    out.append("}};\n\n")
    out.append("}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def load_and_resolve(source_root):
    enums = load_value_enums(source_root)
    asm = os.path.join(source_root, "src", "event", "npc_prop.asm")
    records, record_offsets = parse_npc_asm(asm, enums)
    if len(records) != NPC_RECORD_COUNT:
        raise ParseError(asm, 0, "record count {} != {}"
                         .format(len(records), NPC_RECORD_COUNT))
    res = resolve(source_root, records, record_offsets)
    return enums, res


def run(source_root, repo_root, check_only=False):
    enums, res = load_and_resolve(source_root)
    if check_only:
        specials = sum(1 for r in res.records if r.variant == "special")
        animated = sum(1 for r in res.records if r.variant == "animated")
        fill = res.pad[0]
        print("OK: npc_prop {} records ({} special, {} animated, {} normal) / "
              "{} map slots (+end); all 9 bytes + ptr table match the ROM; "
              "EventReturn = $0{:04X}; fixed_block tail {} B of 0x{:02X}."
              .format(len(res.records), specials, animated,
                      len(res.records) - specials - animated, NPC_MAP_SLOTS,
                      res.event_return_offset, res.pad[1],
                      fill if fill is not None else 0))
        return 0

    gen = os.path.join(repo_root, "src", "data", "generated")
    fix = os.path.join(repo_root, "tests", "fixtures")
    inc = os.path.join(repo_root, "include", "ostinato")
    for upstream, type_name, stem, src_key, exclude, note in ENUM_HEADERS:
        underlying = "std::uint8_t"
        _write(os.path.join(inc, stem + ".h"),
               render_enum_header(enums[upstream], type_name, underlying,
                                  ENUM_SOURCE_DESC[src_key].format(upstream),
                                  exclude=exclude, note=note))
    _write(os.path.join(gen, "npc_prop_data.inc"),
           render_npc_data_inc(res.records))
    _write(os.path.join(gen, "npc_prop_offsets_data.inc"),
           render_offsets_inc(res.record_offsets))
    _write(os.path.join(fix, "npc_prop_expected.h"), render_fixture(res))
    print("Emitted npc_prop family (13 enum headers + 2 generated + 1 fixture) "
          "-> {}".format(repo_root))
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
