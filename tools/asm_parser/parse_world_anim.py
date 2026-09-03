#!/usr/bin/env python3
"""Emit the world animation frame id space and the frame-sequence tables.

Port-time tooling (NOT a build/CI dependency). The world map draws its vehicles,
chocobos and characters from 108 sprite-composition records: each one says how
many hardware sprites a frame uses and gives four bytes per sprite. Those records
arrange copyrighted artwork, so they stay in the player's cartridge and the port
reads them there; what this script emits is the id space that names them, the
small tables that step through them, and the structural facts the tests pin.

Sources:

  * src/world/world_anim.asm — the 108 records and the pointer table addressing
    them. The pointer table is `.repeat`-generated, so the offsets are label
    arithmetic rather than literals and have to be resolved from the label
    layout.
  * src/world/sprite.asm — the four frame-sequence tables, which hold indices
    into the frame space rather than composition data and so compile in.
  * The vanilla cartridge — the oracle. Both blocks are reassembled from the
    source and compared to the bytes at their address; the sequence tables are
    compared entry by entry at theirs.

Emitted artifacts:

  * WorldAnimFrameId  include/ostinato/world_anim_frame_id.h
  * sequence rows     src/data/generated/world_anim_dismount_chocobo_frames_data.inc
                      src/data/generated/world_anim_smoking_airship_frames_data.inc
                      src/data/generated/world_anim_bird_frames_data.inc
  * fixture           tests/fixtures/world_anim_expected.h

The fixture carries structure only — counts, extents, and which records store
more sprite rows than they declare. None of the composition bytes are written
here or anywhere else in the repository: they are cartridge content, and the
tests read them from the cartridge the same way the game does.

Structural guarantees, hard-errored at emit time:
  * every one of the 108 frame labels appears exactly once, and the records they
    name tile the block with no gap and no overlap;
  * the block is exactly as long as the copy routine's own length;
  * every record is a count byte followed by whole four-byte sprite rows;
  * the records storing more rows than they declare are exactly the eight the
    corpus has — a ninth is a corpus change, not something to absorb;
  * every frame-sequence value names a frame that exists;
  * the two labels over the smoking-airship run are one contiguous table;
  * the reassembled pointer table and record block are byte-identical to the
    cartridge, and so is every sequence table.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_world_anim.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError

# Bytes per sprite in a record: x, y, tile index, then the attribute byte
# (world/world_anim.asm:14-24).
SPRITE_ROW_BYTES = 4

# The records that store more sprite rows than their count byte declares. Both
# draw routines read exactly `count` rows (world/sprite.asm:423-489, :495-...),
# so the extra rows never reach the screen — but they are real cartridge bytes
# inside the block that gets copied whole, so they are carried. A record joining
# or leaving this set is a corpus change and stops the parse.
SURPLUS_ROW_FRAMES = (0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x6A, 0x6B)

# Frames per row of the smoking-airship table, and the index the second label
# sits at. The airship's altitude picks a row in steps of six
# (world/sprite.asm:1841-1854); the step within a row runs 0-6, one wider than
# the row, so each row's read can fall through onto the next row's leading zero
# (world/sprite.asm:1876-1888).
SMOKING_AIRSHIP_ROW_STRIDE = 6
SMOKING_AIRSHIP_SECOND_LABEL_AT = 18

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
_RE_FRAME_LABEL = re.compile(r"^WorldAnimSprite_([0-9a-f]{2}):$")
_RE_ADDR_PREFIX = re.compile(r"^@([0-9a-f]{4}):")
_RE_BYTE_DIR = re.compile(r"^(?:@[0-9a-f]{4}:)?\s*\.byte\s+(.+)$")
_RE_REPEAT = re.compile(r"^\.repeat\s+(\$[0-9a-f]+|\d+),\s*i$")
_RE_BANK = re.compile(
    r"^\s*bank_ee:\s*start\s*=\s*\$([0-9a-fA-F]+)", re.M)
_RE_COPY_LENGTH = re.compile(r"cpx\s+#\$([0-9a-f]{4})")

# The section comments world_anim.asm puts above each run of related frames.
# They name groups, never individual frames, so they ride along as comments on
# the emitted enum and nothing is invented past them.
#
# A heading is taken only when it sits directly above the frame label it opens
# and names that frame: the file's own record-format documentation is written in
# the same `$xx: text` shape (world/world_anim.asm:16-19) and would otherwise be
# read as headings for the first four frames. The closing `$` of a range is
# optional because one heading omits it (`$5f-61: bird`).
_RE_GROUP_COMMENT = re.compile(r"^\$([0-9a-f]{2})(?:-\$?([0-9a-f]{2}))?:\s*(.+)$")


class Record(object):
    """One sprite-composition record: the frames naming it, and its bytes."""

    def __init__(self, frames, address, values):
        self.frames = frames      # frame indices whose pointer resolves here
        self.address = address    # bank-relative address of the count byte
        self.values = values      # count byte followed by the sprite rows

    @property
    def declared_sprites(self):
        return self.values[0]

    @property
    def stored_rows(self):
        return (len(self.values) - 1) // SPRITE_ROW_BYTES


class FrameSequence(object):
    """One compiled-in table of frame indices."""

    def __init__(self, name, array, label, line, address, values, doc):
        self.name = name        # human name used in messages
        self.array = array      # emitted C++ array name
        self.label = label      # upstream label
        self.line = line        # 1-based line the label sits on
        self.address = address  # bank-relative address
        self.values = values
        self.doc = doc          # the emitted table's own comment


# --- source reading ----------------------------------------------------------

def read_bank_base(source_root):
    """The SNES address bank $ee starts at, from the linker's memory map."""
    path = os.path.join(source_root, "cfg", "ff6-en.cfg")
    if not os.path.isfile(path):
        raise ParseError(path, 0, "linker config not found")
    with open(path, "r", encoding="utf-8") as fh:
        match = _RE_BANK.search(fh.read())
    if match is None:
        raise ParseError(path, 0,
                         "bank_ee is not in the memory map — the config's shape "
                         "changed upstream")
    return int(match.group(1), 16)


def read_copy_lengths(source_root):
    """The two lengths LoadAnimFrames copies, in the order it copies them.

    The routine moves the pointer table and then the record block, each with a
    literal length (world/sprite.asm:2731-2753). Those literals are the block
    extents this parser checks its own reassembly against, so they come from the
    source rather than being restated here.
    """
    path = os.path.join(source_root, "src", "world", "sprite.asm")
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    start = None
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        if code.strip() == "LoadAnimFrames:":
            start = idx
            break
    if start is None:
        raise ParseError(path, 0, "LoadAnimFrames not found")

    lengths = []
    for idx in range(start, min(start + 60, len(lines))):
        code, _comment = common.strip_comment(lines[idx])
        for match in _RE_COPY_LENGTH.finditer(code):
            lengths.append(int(match.group(1), 16))
    if len(lengths) < 2:
        raise ParseError(path, start + 1,
                         "LoadAnimFrames states {} copy lengths, expected 2"
                         .format(len(lengths)))
    return lengths[0], lengths[1]


def _byte_values(text, path, lineno):
    values = []
    for term in text.split(","):
        value = common.parse_int_literal(term.strip())
        if value is None:
            raise ParseError(path, lineno,
                             "unparsable .byte term {!r}".format(term.strip()))
        if not 0 <= value <= 0xFF:
            raise ParseError(path, lineno,
                             "byte value {} out of range".format(value))
        values.append(value)
    return values


def read_frame_count(path, lines):
    """The frame count, from the pointer table's own `.repeat` directive."""
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        match = _RE_REPEAT.match(code.strip())
        if match is None:
            continue
        count = common.parse_int_literal(match.group(1))
        if count is None:
            raise ParseError(path, idx + 1,
                             "unparsable repeat count {!r}".format(match.group(1)))
        return count
    raise ParseError(path, 0,
                     "the pointer table has no .repeat directive — the table's "
                     "shape changed upstream")


def _group_heading(comment, frame):
    """The heading `comment` gives `frame`, or None if it does not give one."""
    if not comment:
        return None
    match = _RE_GROUP_COMMENT.match(comment.strip())
    if match is None or int(match.group(1), 16) != frame:
        return None
    return match.group(3).strip()


def read_records(source_root):
    """Every record in world_anim.asm, in address order, plus the group names.

    A frame label carrying no data of its own resolves to the next record that
    does: WorldAnimSprite_00 is a zero-length label at the block base, and
    _5a/_5b are two labels on one record. Both are preserved as they are — the
    pointer table holds a row per frame either way.
    """
    path = os.path.join(source_root, "src", "world", "world_anim.asm")
    if not os.path.isfile(path):
        raise ParseError(path, 0, "world_anim.asm not found")
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    frame_count = read_frame_count(path, lines)
    groups = {}

    records = []
    pending = []
    current = None
    seen_block_label = False
    last_comment = None

    for idx, raw in enumerate(lines):
        code, comment = common.strip_comment(raw)
        stripped = code.strip()
        if not stripped:
            # A comment on its own line stands as the heading candidate for
            # whatever label follows; a blank line between the two is common and
            # does not break the pairing.
            if comment:
                last_comment = comment
            continue

        frame = _RE_FRAME_LABEL.match(stripped)
        if frame is not None:
            if not seen_block_label:
                raise ParseError(path, idx + 1,
                                 "frame label before the WorldAnimSprites base")
            index = int(frame.group(1), 16)
            heading = _group_heading(last_comment, index)
            if heading is not None:
                groups[index] = heading
            pending.append(index)
            current = None
            last_comment = None
            continue

        label = _RE_LABEL.match(stripped)
        if label is not None:
            if label.group(1) == "WorldAnimSprites":
                seen_block_label = True
            current = None
            last_comment = None
            continue

        data = _RE_BYTE_DIR.match(stripped)
        if data is not None:
            values = _byte_values(data.group(1), path, idx + 1)
            if current is None:
                if not pending:
                    raise ParseError(path, idx + 1,
                                     "record data with no frame label above it")
                address = _RE_ADDR_PREFIX.match(stripped)
                if address is None:
                    raise ParseError(path, idx + 1,
                                     "record for frame {} opens without an "
                                     "@addr: annotation".format(pending[0]))
                current = Record(frames=pending, address=int(address.group(1), 16),
                                 values=[])
                records.append(current)
                pending = []
            current.values.extend(values)
            last_comment = None
            continue

        # Anything else (directives, the pointer table's own body, separators)
        # closes an open record without starting one.
        current = None
        last_comment = None

    if pending:
        raise ParseError(path, 0,
                         "frame label(s) {} carry no record and none follows"
                         .format(", ".join(str(f) for f in pending)))
    return path, frame_count, records, groups


def read_frame_sequences(source_root, frame_count):
    """The four compiled-in tables of frame indices, from sprite.asm.

    Read with a local label walker rather than a shared one: this is the only
    consumer, and the tables sit between routine bodies where a general reader
    would have to guess at the terminator.
    """
    path = os.path.join(source_root, "src", "world", "sprite.asm")
    if not os.path.isfile(path):
        raise ParseError(path, 0, "sprite.asm not found")
    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    wanted = {
        "_ee4de3": ("dismount-chocobo frames", "kWorldAnimDismountChocoboFrames",
                    "The frames of the dismounting-chocobo cycle, in the order\n"
                    "UpdateSpriteAnim_0a steps through them\n"
                    "(world/sprite.asm:1700-1702)."),
        "_ee5196": ("smoking-airship frames (first rows)",
                    "kWorldAnimSmokingAirshipFrames",
                    "The frames the damaged airship shows, six per altitude row.\n"
                    "\n"
                    "The airship's altitude picks the row, in steps of six\n"
                    "(world/sprite.asm:1841-1854), and the step within a row runs\n"
                    "0-6 — one wider than the row, so a row's last read falls\n"
                    "through onto the next row's leading FRAME_0 and the sprite\n"
                    "blanks (world/sprite.asm:1876-1888, :2385-2397). The final\n"
                    "row is served by the trailing entry, which exists for exactly\n"
                    "that read."),
        "_ee51a8": ("smoking-airship frames (later rows)", None, None),
        "_ee5350": ("bird frames", "kWorldAnimBirdFrames",
                    "The bird's frames, a four-step cycle\n"
                    "(world/sprite.asm:2311-2320)."),
    }

    found = {}
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        stripped = code.strip()
        label = _RE_LABEL.match(stripped)
        if label is None or label.group(1) not in wanted:
            continue
        name = label.group(1)
        if name in found:
            raise ParseError(path, idx + 1,
                             "{} is defined twice".format(name))

        address = None
        values = []
        for body in range(idx + 1, len(lines)):
            body_code, _body_comment = common.strip_comment(lines[body])
            body_stripped = body_code.strip()
            if not body_stripped:
                if values:
                    break
                continue
            data = _RE_BYTE_DIR.match(body_stripped)
            if data is None:
                break
            prefix = _RE_ADDR_PREFIX.match(body_stripped)
            if prefix is not None and address is None:
                address = int(prefix.group(1), 16)
            values.extend(_byte_values(data.group(1), path, body + 1))
        if address is None or not values:
            raise ParseError(path, idx + 1,
                             "{} has no addressed .byte body".format(name))
        found[name] = (idx + 1, address, values)

    missing = sorted(set(wanted) - set(found))
    if missing:
        raise ParseError(path, 0,
                         "frame-sequence table(s) {} not found — the tables moved "
                         "or were renamed upstream".format(", ".join(missing)))

    # The smoking-airship rows carry two labels over one contiguous run; the
    # second label's address is the first's plus the first's length, so the two
    # bodies are joined into the single table the consumer indexes.
    first_line, first_at, first_values = found["_ee5196"]
    second_line, second_at, second_values = found["_ee51a8"]
    if second_at - first_at != len(first_values):
        raise ParseError(path, second_line,
                         "_ee51a8 sits ${:04x} after _ee5196 but _ee5196 holds "
                         "{} bytes — the two labels are no longer one run"
                         .format(second_at - first_at, len(first_values)))
    if len(first_values) != SMOKING_AIRSHIP_SECOND_LABEL_AT:
        raise ParseError(path, first_line,
                         "_ee5196 holds {} bytes, expected {}"
                         .format(len(first_values),
                                 SMOKING_AIRSHIP_SECOND_LABEL_AT))

    sequences = []
    for label in ("_ee4de3", "_ee5196", "_ee5350"):
        line, address, values = found[label]
        name, array, doc = wanted[label]
        if label == "_ee5196":
            values = list(first_values) + list(second_values)
        sequences.append(FrameSequence(name=name, array=array, label=label,
                                       line=line, address=address,
                                       values=values, doc=doc))

    for sequence in sequences:
        for position, value in enumerate(sequence.values):
            if value >= frame_count:
                raise ParseError(path, sequence.line,
                                 "{}[{}] is frame {} but only {} frames exist"
                                 .format(sequence.label, position, value,
                                         frame_count))
    return sequences


# --- resolution + cartridge cross-check ---------------------------------------

class Resolved(object):
    def __init__(self, frame_count, records, offsets, groups, sequences,
                 pointer_at, block_at, pointer_bytes, block_bytes):
        self.frame_count = frame_count
        self.records = records
        self.offsets = offsets            # frame index -> offset into the block
        self.groups = groups
        self.sequences = sequences
        self.pointer_at = pointer_at      # SNES address of the pointer table
        self.block_at = block_at          # SNES address of the first record
        self.pointer_bytes = pointer_bytes
        self.block_bytes = block_bytes

    @property
    def region_at(self):
        return self.pointer_at

    @property
    def region_size(self):
        return len(self.pointer_bytes) + len(self.block_bytes)

    @property
    def surplus_frames(self):
        """(frame, declared, stored) for every record storing extra rows."""
        rows = []
        for record in self.records:
            if record.stored_rows > record.declared_sprites:
                rows.append((record.frames[0], record.declared_sprites,
                             record.stored_rows))
        return rows


def resolve(source_root):
    """Read both sources, check the structure, and reassemble the two blocks."""
    path, frame_count, records, groups = read_records(source_root)
    bank_base = read_bank_base(source_root)
    pointer_length, block_length = read_copy_lengths(source_root)

    if not records:
        raise ParseError(path, 0, "no records parsed")

    # Every frame is named exactly once, across all the labels.
    named = []
    for record in records:
        named.extend(record.frames)
    if sorted(named) != list(range(frame_count)):
        missing = sorted(set(range(frame_count)) - set(named))
        extra = sorted(f for f in named if named.count(f) > 1)
        raise ParseError(path, 0,
                         "frame labels do not cover 0-{} exactly once "
                         "(missing {}, repeated {})"
                         .format(frame_count - 1, missing, sorted(set(extra))))

    base = records[0].address
    block = []
    for record in records:
        if record.address != base + len(block):
            raise ParseError(path, 0,
                             "record for frame {} starts at ${:04x} but the "
                             "records before it end at ${:04x} — the block is "
                             "not contiguous"
                             .format(record.frames[0], record.address,
                                     base + len(block)))
        if (len(record.values) - 1) % SPRITE_ROW_BYTES:
            raise ParseError(path, 0,
                             "record for frame {} holds {} bytes, which is not a "
                             "count byte plus whole sprite rows"
                             .format(record.frames[0], len(record.values)))
        if record.stored_rows < record.declared_sprites:
            raise ParseError(path, 0,
                             "record for frame {} declares {} sprites but stores "
                             "only {} rows — the consumer would read past it"
                             .format(record.frames[0], record.declared_sprites,
                                     record.stored_rows))
        block.extend(record.values)

    if len(block) != block_length:
        raise ParseError(path, 0,
                         "records total {} bytes but LoadAnimFrames copies {}"
                         .format(len(block), block_length))

    surplus = tuple(record.frames[0] for record in records
                    if record.stored_rows > record.declared_sprites)
    if surplus != SURPLUS_ROW_FRAMES:
        raise ParseError(path, 0,
                         "records storing more rows than they declare are {} — "
                         "expected {}; a change here is a corpus change, not "
                         "something to absorb"
                         .format([hex(f) for f in surplus],
                                 [hex(f) for f in SURPLUS_ROW_FRAMES]))

    offsets = {}
    for record in records:
        for frame in record.frames:
            offsets[frame] = record.address - base

    pointer_bytes = bytearray()
    for frame in range(frame_count):
        offset = offsets[frame]
        pointer_bytes.append(offset & 0xFF)
        pointer_bytes.append((offset >> 8) & 0xFF)
    if len(pointer_bytes) != pointer_length:
        raise ParseError(path, 0,
                         "the pointer table is {} bytes but LoadAnimFrames "
                         "copies {}".format(len(pointer_bytes), pointer_length))

    pointer_at = bank_base | (base - pointer_length)
    block_at = bank_base | base

    sequences = read_frame_sequences(source_root, frame_count)
    return Resolved(frame_count=frame_count, records=records, offsets=offsets,
                    groups=groups, sequences=sequences, pointer_at=pointer_at,
                    block_at=block_at, pointer_bytes=bytes(pointer_bytes),
                    block_bytes=bytes(block))


def assert_matches_rom(resolved, source_root, bank_base=None):
    """The reassembled region and every sequence table match the cartridge.

    Returns a one-line note for the run summary, or None when no cartridge is
    available. The addresses are the whole point of the region row, so this is
    the assert that makes them trustworthy: every byte of the 5,294-byte region
    is compared, not a sample.
    """
    if common.find_vanilla_rom(source_root) is None:
        return None
    rom = common.load_vanilla_rom(source_root)
    bank_base = bank_base if bank_base is not None else read_bank_base(source_root)

    expected = resolved.pointer_bytes + resolved.block_bytes
    offset = common.hirom_file_offset(resolved.region_at)
    actual = rom[offset:offset + len(expected)]
    if actual != expected:
        for position, (got, want) in enumerate(zip(actual, expected)):
            if got != want:
                raise ParseError(
                    "world/world_anim.asm", 0,
                    "ROM MISMATCH at region offset {} (${:06x}): ROM ${:02x} != "
                    "source ${:02x} — the address or the record layout is wrong"
                    .format(position, resolved.region_at + position, got, want))
        raise ParseError("world/world_anim.asm", 0,
                         "ROM MISMATCH: the region is {} bytes in the cartridge "
                         "but {} were reassembled"
                         .format(len(actual), len(expected)))

    for sequence in resolved.sequences:
        at = bank_base | sequence.address
        start = common.hirom_file_offset(at)
        got = list(rom[start:start + len(sequence.values)])
        if got != list(sequence.values):
            for position, (have, want) in enumerate(zip(got, sequence.values)):
                if have != want:
                    raise ParseError(
                        "world/sprite.asm", sequence.line,
                        "ROM MISMATCH at {}[{}]: ROM ${:02x} != source ${:02x}"
                        .format(sequence.label, position, have, want))
            raise ParseError("world/sprite.asm", sequence.line,
                             "ROM MISMATCH: {} length differs"
                             .format(sequence.label))

    return ("{} region bytes and {} sequence entries identical to the cartridge"
            .format(resolved.region_size,
                    sum(len(s.values) for s in resolved.sequences)))


# --- rendering ----------------------------------------------------------------

def _banner(source_lines, extra=None):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    note = "// (original-src pinned at 1ea47b5{})\n".format(
        "; " + extra if extra else "")
    return ("// AUTO-GENERATED by tools/asm_parser/parse_world_anim.py\n"
            + body + note +
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_world_anim.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


def render_frame_id_header(resolved):
    out = [_banner(["world/world_anim.asm (the frame labels and their group "
                    "headings)"]),
           "#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n",
           "// Which world-map animation frame to draw.\n"
           "//\n"
           "// A frame is one arrangement of hardware sprites — the airship seen\n"
           "// from a particular angle, a step of the chocobo's gait, the party\n"
           "// leader facing left. The arrangements themselves live in the\n"
           "// cartridge (RomAsset::WORLD_ANIM_FRAMES); this names them.\n"
           "//\n"
           "// The names are positions, not descriptions. The cartridge labels\n"
           "// groups of frames and never an individual one, so naming each\n"
           "// frame would be inventing meaning the game does not state. The\n"
           "// group headings ride along as comments, and better names are\n"
           "// welcome once the drawing code makes each frame's use plain.\n"
           "// They are quoted as the cartridge source writes them, so any\n"
           "// frame number inside a heading is hexadecimal while the\n"
           "// enumerators here count in decimal.\n"
           "//\n"
           "// FRAME_0 is the blank frame: it stores nothing, and both world draw\n"
           "// routines skip an object showing it (world/sprite.asm:423-489,\n"
           "// :495-554).\n"
           "namespace ostinato {\n\n",
           "enum class WorldAnimFrameId : std::uint8_t {\n"]

    for frame in range(resolved.frame_count):
        heading = resolved.groups.get(frame)
        if heading is not None:
            out.append("{}    // {}\n".format("" if frame == 0 else "\n",
                                              heading))
        out.append("    FRAME_{} = {},\n".format(frame, frame))
    out.append("};\n\n")
    out.append("// How many frames there are.\n")
    out.append("inline constexpr std::size_t kWorldAnimFrameCount = {};\n\n"
               .format(resolved.frame_count))
    out.append("}  // namespace ostinato\n")
    return "".join(out)


def render_sequence_inc(sequence):
    doc = "".join("// {}\n".format(line).rstrip() + "\n"
                  for line in sequence.doc.split("\n"))
    out = [_banner(["world/sprite.asm:{} ({})".format(sequence.line,
                                                      sequence.label)],
                   extra="every byte cross-checked against the cartridge"),
           doc,
           "//\n"
           "// The {} rows of {}, #included inside that array in\n"
           "// src/data/world_anim.cpp. The row's identity (.index) is a typed\n"
           "// field, not the array position; a compile-time assert verifies\n"
           "// index == position for every entry.\n\n"
           .format(len(sequence.values), sequence.array)]
    width = max(len(str(len(sequence.values) - 1)), 1)
    for position, value in enumerate(sequence.values):
        out.append("    {{ .index = {:>{w}}, .frame = WorldAnimFrameId::FRAME_{} }},\n"
                   .format(position, value, w=width))
    return "".join(out)


def render_fixture(resolved):
    surplus = resolved.surplus_frames
    out = [_banner(["world/world_anim.asm (record structure)",
                    "world/sprite.asm (the frame-sequence tables)"],
                   extra="cross-checked against the cartridge"),
           "#pragma once\n\n#include <array>\n#include <cstddef>\n"
           "#include <cstdint>\n\n",
           "// What the world animation frames are shaped like, and what the\n"
           "// frame-sequence tables hold.\n"
           "//\n"
           "// Structure only. The sprite arrangements are cartridge content and\n"
           "// are never written into this repository — a test that wants them\n"
           "// reads the cartridge, the same way the game does.\n"
           "namespace ostinato::test {\n\n",
           "// The region holding the pointer table and the records behind it,\n"
           "// as one extent.\n",
           "inline constexpr std::uint32_t kExpectedWorldAnimRegionAt = 0x{:06X};\n"
           .format(resolved.region_at),
           "inline constexpr std::size_t kExpectedWorldAnimRegionSize = {};\n"
           .format(resolved.region_size),
           "inline constexpr std::size_t kExpectedWorldAnimPointerBytes = {};\n"
           .format(len(resolved.pointer_bytes)),
           "inline constexpr std::size_t kExpectedWorldAnimBlockBytes = {};\n"
           .format(len(resolved.block_bytes)),
           "inline constexpr std::size_t kExpectedWorldAnimFrameCount = {};\n\n"
           .format(resolved.frame_count),
           "// A record that stores more sprite rows than it declares: the frame,\n"
           "// the count it declares, and the rows it actually holds. The extra\n"
           "// rows are never drawn.\n"
           "struct ExpectedSurplusFrame {\n"
           "    std::uint8_t frame;\n"
           "    std::uint8_t declaredSprites;\n"
           "    std::uint8_t storedRows;\n"
           "};\n\n",
           "inline constexpr std::array<ExpectedSurplusFrame, {}>\n"
           "kExpectedWorldAnimSurplusFrames = {{{{\n".format(len(surplus))]
    for frame, declared, stored in surplus:
        out.append("    {{ .frame = {:>3}, .declaredSprites = {}, "
                   ".storedRows = {} }},\n".format(frame, declared, stored))
    out.append("}};\n\n")

    out.append("// One step of a frame-sequence table: the step, and the frame\n"
               "// shown at it. Identity is a field, never a position.\n"
               "struct ExpectedWorldAnimFrameStep {\n"
               "    std::uint16_t index;\n"
               "    std::uint8_t  frame;\n"
               "};\n\n")
    for sequence in resolved.sequences:
        out.append("// The frames {} steps through.\n".format(sequence.label))
        out.append("inline constexpr std::array<ExpectedWorldAnimFrameStep, {}>\n"
                   "kExpected{} = {{{{\n".format(len(sequence.values),
                                                 sequence.array[1:]))
        width = max(len(str(len(sequence.values) - 1)), 1)
        for position, value in enumerate(sequence.values):
            out.append("    {{ .index = {:>{w}}, .frame = {:>3} }},\n"
                       .format(position, value, w=width))
        out.append("}};\n\n")

    out.append("// Where the smoking-airship table's second label sits, and how\n"
               "// wide one altitude row is.\n")
    out.append("inline constexpr std::size_t kExpectedSmokingAirshipSecondLabelAt "
               "= {};\n".format(SMOKING_AIRSHIP_SECOND_LABEL_AT))
    out.append("inline constexpr std::size_t kExpectedSmokingAirshipRowStride "
               "= {};\n\n".format(SMOKING_AIRSHIP_ROW_STRIDE))
    out.append("}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver -------------------------------------------------------------------

def _sequence_inc_path(repo_root, sequence):
    stem = {
        "kWorldAnimDismountChocoboFrames": "world_anim_dismount_chocobo_frames",
        "kWorldAnimSmokingAirshipFrames": "world_anim_smoking_airship_frames",
        "kWorldAnimBirdFrames": "world_anim_bird_frames",
    }[sequence.array]
    return os.path.join(repo_root, "src", "data", "generated",
                        stem + "_data.inc")


def run(source_root, repo_root, check_only=False):
    resolved = resolve(source_root)
    rom_note = assert_matches_rom(resolved, source_root)
    if rom_note is None:
        rom_note = "cartridge cross-check SKIPPED (set FF6_VANILLA_ROM)"

    summary = ("{} frames in {} records ({} aliased labels); region ${:06X} + {} "
               "B; {} records store unread rows; {} sequence tables; {}"
               .format(resolved.frame_count, len(resolved.records),
                       resolved.frame_count - len(resolved.records),
                       resolved.region_at, resolved.region_size,
                       len(resolved.surplus_frames), len(resolved.sequences),
                       rom_note))

    if check_only:
        print("OK: " + summary)
        return 0

    _write(os.path.join(repo_root, "include", "ostinato",
                        "world_anim_frame_id.h"),
           render_frame_id_header(resolved))
    for sequence in resolved.sequences:
        _write(_sequence_inc_path(repo_root, sequence),
               render_sequence_inc(sequence))
    _write(os.path.join(repo_root, "tests", "fixtures",
                        "world_anim_expected.h"),
           render_fixture(resolved))
    print("Emitted 5 files: " + summary)
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
        return run(args.source_root, args.repo_root,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
