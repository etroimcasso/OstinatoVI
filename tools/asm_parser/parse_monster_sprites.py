#!/usr/bin/env python3
"""Place the monster graphics properties, and emit the monster sprite boxes.

Port-time tooling (NOT a build/CI dependency). How wide a monster stands on the
battlefield decides which side of a pincer it is on and which way it faces, so
the battle has to know each monster's sprite size. The original works it out
from two things:

  * the monster's stencil — the map of which 8x8 tiles its sprite fills. Which
    stencil a monster uses is in its graphics properties (MonsterGfxProp, five
    bytes per monster). Both are cartridge content and stay in the cartridge:
    this script places the properties table so the region table can name it,
    and proves the place.
  * the box the formation gives the monster's slot — the largest sprite that
    slot shows. The boxes are a short table in the battle graphics code
    (MonsterVRAMMap: per formation layout, one box per slot), so they compile
    in; this script emits them.

Sources:

  * src/gfx/monster_gfx.asm — the MonsterGfxProp records, each one a
    make_monster_gfx_prop line naming the stencil, its size and the bit depth.
  * include/gfx/monster_gfx.inc — the two stencil enums the records index.
  * tools/rip_list_{en,jp}.json — the ripped monster palettes, which begin
    where the properties end.
  * cfg/ff6-{en,jp}.cfg — the properties segment's alignment.
  * src/btlgfx/btlgfx_main.asm — MonsterVRAMMapPtrs and the thirteen layouts.
  * The vanilla cartridge — the oracle for both.

Emitted artifacts:

  * box rows          src/data/generated/monster_sprite_boxes_data.inc
  * fixture           tests/fixtures/monster_sprites_expected.h

The region row for the properties is emitted by parse_rom_regions.py, which
calls place_gfx_props() below.

Structural guarantees, hard-errored at emit time:
  * every MonsterGfxProp line is a make_monster_gfx_prop with a bit depth of 3
    or 4 and a size of 8 or 16, naming a stencil of that size that exists;
  * the records end exactly where the ripped palettes begin, in both
    languages, and start on the segment's alignment;
  * every English record's stencil number, large-stencil bit and 3bpp bit
    match the cartridge;
  * the layouts are numbered 0 to ARRAY_LENGTH - 1 in order, each holding at
    least one box;
  * the assembled pointer table and boxes occur in the bank the bank map
    loads their segment into, the boxes exactly once, with the pointer table
    immediately before them.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_monster_sprites.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError

# Bytes per graphics-properties record: the graphics index word (bit 15 = 3bpp),
# the palette word stored high byte first (bit 15 = large stencil), and the
# stencil number (gfx/monster_gfx.asm:23-50, btlgfx_main.asm:4543-4582).
GFX_PROP_RECORD_BYTES = 5

# Where in a record each of the bits this script proves sits.
GFX_PROP_BPP3_BYTE = 1      # bit 7: the graphics are 3bpp
GFX_PROP_LARGE_BYTE = 2     # bit 7: the stencil is a 16x16 one
GFX_PROP_STENCIL_BYTE = 4   # the stencil number

# Bytes per stencil: eight one-byte rows for an 8x8 stencil, sixteen two-byte
# rows for a 16x16 one (btlgfx_main.asm:4684-4715).
SMALL_STENCIL_BYTES = 8
LARGE_STENCIL_BYTES = 32

BANK_SIZE = 0x10000

# Bytes per box: the destination word, then width and height in tiles
# (btlgfx_main.asm, the def_monster_vram_map macro).
BOX_RECORD_BYTES = 4

_RE_GFX_PROP = re.compile(
    r"^make_monster_gfx_prop\s+([A-Z0-9_]+)\s*,\s*(\d+)\s*,\s*(\d+)"
    r"(?:\s*,\s*([A-Z0-9_]+))?$")
_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
_RE_LAYOUT_LABEL = re.compile(r"^MonsterVRAMMap::_(\d+):$")
_RE_BOX = re.compile(
    r"^(?:@[0-9a-f]{4}:)?\s*def_monster_vram_map\s+(\d+)\s*,\s*(\d+)\s*,"
    r"\s*(\d+)\s*,\s*(\d+)$")
_RE_ARRAY_LENGTH = re.compile(r"^ARRAY_LENGTH\s*=\s*(\S+)$")
_RE_SEGMENT = re.compile(r'^\.segment\s+"([^"]+)"$')
_RE_SEGMENT_ALIGN = re.compile(
    r"^\s*monster_gfx_prop:\s*load\s*=\s*\w+,.*align\s*=\s*\$([0-9a-fA-F]+)")


class GfxProp(object):
    """One make_monster_gfx_prop line."""

    def __init__(self, line, gfx, bpp, size, pal, stencil):
        self.line = line        # 1-based source line
        self.gfx = gfx          # the graphics name
        self.bpp = bpp          # 3 or 4
        self.size = size        # 8 or 16: which stencil family
        self.pal = pal          # the palette name, or None for the graphics' own
        self.stencil = stencil  # the stencil number within its family

    @property
    def large(self):
        return self.size == 16


class GfxProps(object):
    """Every graphics-properties record, with the stencil counts they index."""

    def __init__(self, path, line, records, small_count, large_count):
        self.path = path
        self.line = line              # the MonsterGfxProp label's line
        self.records = records        # [GfxProp], in record order
        self.small_count = small_count
        self.large_count = large_count


class Placement(object):
    """Where the properties sit in one language, and how much was proved."""

    def __init__(self, language, begin, size, proved):
        self.language = language
        self.begin = begin
        self.size = size
        self.proved = proved   # bytes compared with the cartridge


class Box(object):
    """One slot's box in one formation layout."""

    def __init__(self, layout, slot, x, y, width, height, line):
        self.layout = layout
        self.slot = slot
        self.x = x            # where its tiles go, in tiles; assembly only
        self.y = y
        self.width = width    # in tiles
        self.height = height
        self.line = line


# --- source reading ----------------------------------------------------------

def _read_lines(path):
    if not os.path.isfile(path):
        raise ParseError(path, 0, "source file not found")
    with open(path, "r", encoding="utf-8") as fh:
        return fh.readlines()


def read_stencil_counts(source_root):
    """The two stencil enums: {name: number} for each, small then large."""
    path = os.path.join(source_root, "include", "gfx", "monster_gfx.inc")
    if not os.path.isfile(path):
        raise ParseError(path, 0, "source file not found")
    parsed = common.parse_ca65_constants(path)
    families = []
    for name in ("MONSTER_STENCIL_SMALL", "MONSTER_STENCIL_LARGE"):
        enum = parsed.enum(name)
        if enum is None or not enum.members:
            raise ParseError(path, 0, "{} not found".format(name))
        numbers = {m.name: m.value for m in enum.members}
        if sorted(numbers.values()) != list(range(len(numbers))):
            raise ParseError(path, enum.src_line,
                             "{} does not number its stencils 0 to {}"
                             .format(name, len(numbers) - 1))
        families.append(numbers)
    return families[0], families[1]


def parse_gfx_prop_line(code, path, lineno, small, large):
    """One record line, resolved against the stencil enums."""
    match = _RE_GFX_PROP.match(code)
    if match is None:
        raise ParseError(path, lineno,
                         "expected make_monster_gfx_prop NAME, BPP, SIZE[, PAL],"
                         " got {!r}".format(code))
    gfx, bpp, size, pal = (match.group(1), int(match.group(2)),
                           int(match.group(3)), match.group(4))
    if bpp not in (3, 4):
        raise ParseError(path, lineno, "bit depth {} is not 3 or 4".format(bpp))
    if size not in (8, 16):
        raise ParseError(path, lineno, "size {} is not 8 or 16".format(size))
    family = large if size == 16 else small
    if gfx not in family:
        raise ParseError(path, lineno,
                         "{} has no {}x{} stencil".format(gfx, size, size))
    return GfxProp(line=lineno, gfx=gfx, bpp=bpp, size=size, pal=pal,
                   stencil=family[gfx])


def read_gfx_props(source_root):
    """Every MonsterGfxProp record, from its label to the next separator."""
    path = os.path.join(source_root, "src", "gfx", "monster_gfx.asm")
    lines = _read_lines(path)
    small, large = read_stencil_counts(source_root)

    label_line = None
    records = []
    for idx, raw in enumerate(lines):
        lineno = idx + 1
        code, comment = common.strip_comment(raw)
        code = code.strip()
        if label_line is None:
            if code == "MonsterGfxProp:":
                label_line = lineno
            continue
        if not code:
            # A separator line ends the table; blank and comment lines inside
            # it are skipped.
            if comment is not None and comment.strip().startswith("---"):
                break
            continue
        if code.startswith(".segment") or _RE_LABEL.match(code):
            break
        records.append(parse_gfx_prop_line(code, path, lineno, small, large))

    if label_line is None:
        raise ParseError(path, 0, "MonsterGfxProp label not found")
    if not records:
        raise ParseError(path, label_line, "MonsterGfxProp holds no records")
    return GfxProps(path=path, line=label_line, records=records,
                    small_count=len(small), large_count=len(large))


def read_segment_align(source_root, language):
    """The properties segment's alignment in one language's bank map."""
    path = os.path.join(source_root, "cfg", "ff6-{}.cfg".format(language))
    for raw in _read_lines(path):
        match = _RE_SEGMENT_ALIGN.match(raw)
        if match is not None:
            return int(match.group(1), 16)
    raise ParseError(path, 0, "no aligned monster_gfx_prop segment")


def read_segment_bank(source_root, segment):
    """The SNES address of the bank the English bank map loads `segment` into."""
    path = os.path.join(source_root, "cfg", "ff6-en.cfg")
    pattern = re.compile(r"^\s*{}:\s*load\s*=\s*bank_([0-9a-f]{{2}})\b"
                         .format(re.escape(segment)))
    for raw in _read_lines(path):
        match = pattern.match(raw)
        if match is not None:
            return int(match.group(1), 16) << 16
    raise ParseError(path, 0, "segment {} not in the bank map".format(segment))


def read_boxes(source_root):
    """Every layout's boxes, as [[Box]] indexed by layout then slot, and the
    segment holding them."""
    path = os.path.join(source_root, "src", "btlgfx", "btlgfx_main.asm")
    lines = _read_lines(path)

    segment = None
    array_length = None
    in_scope = False
    layouts = []
    started = False
    for idx, raw in enumerate(lines):
        lineno = idx + 1
        code, comment = common.strip_comment(raw)
        code = code.strip()
        match = _RE_SEGMENT.match(code)
        if match is not None and not started:
            segment = match.group(1)
            continue
        if code == ".scope MonsterVRAMMap":
            in_scope = True
            continue
        if in_scope:
            if code == ".endscope":
                in_scope = False
                continue
            match = _RE_ARRAY_LENGTH.match(code)
            if match is not None:
                array_length = common.parse_int_literal(match.group(1))
            continue

        match = _RE_LAYOUT_LABEL.match(code)
        if match is not None:
            started = True
            number = int(match.group(1))
            if number != len(layouts):
                raise ParseError(path, lineno,
                                 "layout _{} out of order (expected _{})"
                                 .format(number, len(layouts)))
            layouts.append([])
            continue
        if not started:
            continue
        if not code:
            if comment is not None and comment.strip().startswith("---"):
                break
            continue
        match = _RE_BOX.match(code)
        if match is None:
            raise ParseError(path, lineno,
                             "expected def_monster_vram_map X, Y, W, H, got "
                             "{!r}".format(code))
        x, y, width, height = (int(match.group(n)) for n in range(1, 5))
        if width <= 0 or height <= 0:
            raise ParseError(path, lineno, "a box with no area")
        layout = layouts[-1]
        layout.append(Box(layout=len(layouts) - 1, slot=len(layout), x=x, y=y,
                          width=width, height=height, line=lineno))

    if array_length is None:
        raise ParseError(path, 0, "MonsterVRAMMap::ARRAY_LENGTH not found")
    if len(layouts) != array_length:
        raise ParseError(path, 0, "{} layouts, ARRAY_LENGTH says {}"
                         .format(len(layouts), array_length))
    for number, layout in enumerate(layouts):
        if not layout:
            raise ParseError(path, 0, "layout _{} holds no box".format(number))
    if segment is None:
        raise ParseError(path, 0, "no .segment before the boxes")
    return layouts, segment


# --- placement and the cartridge ---------------------------------------------

def _rip_row(rows, asset, language):
    found = [row for row in rows if row.asset == asset]
    if len(found) != 1:
        raise ParseError("tools/rip_list_{}.json".format(language), 0,
                         "expected one {} row, found {}"
                         .format(asset, len(found)))
    return found[0]


def check_gfx_props_against_rom(props, begin, rom):
    """Every record's stencil number, large bit and 3bpp bit match the bytes at
    `begin`. Returns the number of bytes compared whole."""
    for number, record in enumerate(props.records):
        at = common.hirom_file_offset(begin) + number * GFX_PROP_RECORD_BYTES
        stored = rom[at:at + GFX_PROP_RECORD_BYTES]
        expected = {
            "stencil": (stored[GFX_PROP_STENCIL_BYTE], record.stencil),
            "large-stencil bit": (stored[GFX_PROP_LARGE_BYTE] >> 7,
                                  int(record.large)),
            "3bpp bit": (stored[GFX_PROP_BPP3_BYTE] >> 7,
                         int(record.bpp == 3)),
        }
        for what, (got, want) in expected.items():
            if got != want:
                raise ParseError(
                    props.path, record.line,
                    "ROM MISMATCH in record {} ({}) {}: ROM {} != source {}"
                    .format(number, record.gfx, what, got, want))
    return len(props.records)


def place_gfx_props(props, rows_by_language, rom=None, aligns=None):
    """The properties' place in each language: they end where the ripped
    monster palettes begin. With `rom`, every English record is checked."""
    size = len(props.records) * GFX_PROP_RECORD_BYTES
    placements = []
    for language in ("en", "jp"):
        palettes = _rip_row(rows_by_language[language], "MONSTER_GFX_PAL",
                            language)
        begin = palettes.begin - size
        align = (aligns or {}).get(language)
        if align is not None and begin % align != 0:
            raise ParseError(props.path, props.line,
                             "{}: {} records would start at ${:06x}, off the "
                             "segment's ${:x} alignment"
                             .format(language, len(props.records), begin,
                                     align))
        proved = 0
        if language == "en" and rom is not None:
            proved = check_gfx_props_against_rom(props, begin, rom)
        placements.append(Placement(language=language, begin=begin,
                                    size=size, proved=proved))
    return placements


def assemble_boxes(layouts, base):
    """The pointer table and the boxes as they assemble at bank address
    `base`: ([pointer bytes], [box bytes])."""
    pointer_bytes = []
    box_bytes = []
    records_at = base + 2 * len(layouts)
    for layout in layouts:
        address = records_at + len(box_bytes)
        pointer_bytes += [address & 0xFF, (address >> 8) & 0xFF]
        for box in layout:
            destination = box.x * 0x20 + box.y * 0x200
            box_bytes += [destination & 0xFF, (destination >> 8) & 0xFF,
                          box.width, box.height]
    return pointer_bytes, box_bytes


def _occurrences(haystack, needle):
    found = []
    at = haystack.find(needle)
    while at != -1:
        found.append(at)
        at = haystack.find(needle, at + 1)
    return found


def place_boxes(layouts, bank_at, rom):
    """Where the boxes sit in the bank at `bank_at`. They must occur once,
    with the pointer table addressing them right before. Returns the address
    of the pointer table."""
    path = "src/btlgfx/btlgfx_main.asm"
    base = common.hirom_file_offset(bank_at)
    bank = bytes(rom[base:base + BANK_SIZE])
    _pointers, boxes = assemble_boxes(layouts, 0)
    hits = _occurrences(bank, bytes(boxes))
    if len(hits) != 1:
        raise ParseError(path, 0, "the boxes occur {} times in bank ${:02x}, "
                         "expected exactly once"
                         .format(len(hits), bank_at >> 16))
    table_at = hits[0] - 2 * len(layouts)
    pointers, boxes = assemble_boxes(layouts, table_at)
    if list(bank[table_at:table_at + len(pointers)]) != pointers:
        raise ParseError(path, 0, "the bytes before the boxes are not the "
                         "pointer table addressing them")
    return bank_at | table_at


# --- rendering ----------------------------------------------------------------

def _banner(source_lines, extra=None):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    note = "// (original-src pinned at 1ea47b5{})\n".format(
        "; " + extra if extra else "")
    return ("// AUTO-GENERATED by tools/asm_parser/parse_monster_sprites.py\n"
            + body + note +
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_monster_sprites.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


def _flat(layouts):
    return [box for layout in layouts for box in layout]


def render_boxes_inc(layouts):
    boxes = _flat(layouts)
    out = [_banner(["btlgfx/btlgfx_main.asm (MonsterVRAMMap)"],
                   extra="every box cross-checked against the cartridge"),
           "// The largest sprite each slot of a formation shows, in tiles of 8\n"
           "// pixels: {} boxes over {} layouts. A formation names its layout\n"
           "// (Formation::vramMap()); its slot 0 takes the layout's first box,\n"
           "// slot 1 the second, and so on.\n"
           "//\n"
           "// The rows of kMonsterSpriteBoxes, #included inside that array in\n"
           "// src/data/monster_sprites.cpp. A row's identity is its .layout and\n"
           "// .slot fields, not its position; a compile-time assert checks the\n"
           "// rows run layout by layout, slots counting from 0.\n\n"
           .format(len(boxes), len(layouts))]
    for box in boxes:
        out.append("    {{ .layout = {:>2}, .slot = {}, .width = {:>2}, "
                   ".height = {:>2} }},\n".format(box.layout, box.slot,
                                                  box.width, box.height))
    return "".join(out)


def render_fixture(props, layouts, placements):
    boxes = _flat(layouts)
    en = next(p for p in placements if p.language == "en")
    out = [_banner(["gfx/monster_gfx.asm (MonsterGfxProp)",
                    "include/gfx/monster_gfx.inc (the stencil enums)",
                    "btlgfx/btlgfx_main.asm (MonsterVRAMMap)"],
                   extra="cross-checked against the cartridge"),
           "#pragma once\n\n#include <array>\n#include <cstddef>\n"
           "#include <cstdint>\n\n",
           "// What the monster graphics tables are shaped like, and the boxes\n"
           "// each formation layout gives its slots.\n"
           "//\n"
           "// Structure only for the graphics: the properties and stencils are\n"
           "// cartridge content and are never written into this repository — a\n"
           "// test that wants them reads the cartridge, as the game does.\n"
           "namespace ostinato::test {\n\n",
           "// The graphics properties: one record per monster graphics index.\n",
           "inline constexpr std::uint32_t kExpectedMonsterGfxPropAt = 0x{:06X};\n"
           .format(en.begin),
           "inline constexpr std::size_t kExpectedMonsterGfxPropCount = {};\n"
           .format(len(props.records)),
           "inline constexpr std::size_t kExpectedMonsterGfxPropRecordBytes = {};\n"
           .format(GFX_PROP_RECORD_BYTES),
           "inline constexpr std::size_t kExpectedSmallStencilCount = {};\n"
           .format(props.small_count),
           "inline constexpr std::size_t kExpectedLargeStencilCount = {};\n"
           .format(props.large_count),
           "inline constexpr std::size_t kExpectedSmallStencilBytes = {};\n"
           .format(SMALL_STENCIL_BYTES),
           "inline constexpr std::size_t kExpectedLargeStencilBytes = {};\n\n"
           .format(LARGE_STENCIL_BYTES),
           "// One slot's box: the layout and slot it belongs to, and its size in\n"
           "// tiles. Identity is the two fields, never a position.\n"
           "struct ExpectedMonsterSpriteBox {\n"
           "    std::uint8_t layout;\n"
           "    std::uint8_t slot;\n"
           "    std::uint8_t width;\n"
           "    std::uint8_t height;\n"
           "};\n\n",
           "inline constexpr std::size_t kExpectedMonsterSpriteLayoutCount = {};\n\n"
           .format(len(layouts)),
           "inline constexpr std::array<ExpectedMonsterSpriteBox, {}>\n"
           "kExpectedMonsterSpriteBoxes = {{{{\n".format(len(boxes))]
    for box in boxes:
        out.append("    {{ .layout = {:>2}, .slot = {}, .width = {:>2}, "
                   ".height = {:>2} }},\n".format(box.layout, box.slot,
                                                  box.width, box.height))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver -------------------------------------------------------------------

def _load_rom_if_present(source_root):
    if common.find_vanilla_rom(source_root) is None:
        return None
    return common.load_vanilla_rom(source_root)


def run(source_root, repo_root, check_only=False):
    import parse_rom_regions  # the rip-list reader

    rom_size = parse_rom_regions.read_rom_size(source_root)
    rows = {language: parse_rom_regions.read_rip_list(source_root, language,
                                                      rom_size)
            for language in ("en", "jp")}
    rom = _load_rom_if_present(source_root)
    props = read_gfx_props(source_root)
    aligns = {language: read_segment_align(source_root, language)
              for language in ("en", "jp")}
    placements = place_gfx_props(props, rows, rom, aligns)
    layouts, segment = read_boxes(source_root)

    if rom is None:
        rom_note = "cartridge cross-check SKIPPED (set FF6_VANILLA_ROM)"
    else:
        table_at = place_boxes(layouts, read_segment_bank(source_root, segment),
                               rom)
        rom_note = ("{} records checked against the cartridge; the boxes' "
                    "pointer table found at ${:06X}"
                    .format(placements[0].proved, table_at))
    summary = ("{} graphics records (${:06X} en / ${:06X} jp); {} small and {} "
               "large stencils; {} boxes over {} layouts; {}"
               .format(len(props.records), placements[0].begin,
                       placements[1].begin, props.small_count,
                       props.large_count, len(_flat(layouts)), len(layouts),
                       rom_note))

    if check_only:
        print("OK: " + summary)
        return 0

    _write(os.path.join(repo_root, "src", "data", "generated",
                        "monster_sprite_boxes_data.inc"),
           render_boxes_inc(layouts))
    _write(os.path.join(repo_root, "tests", "fixtures",
                        "monster_sprites_expected.h"),
           render_fixture(props, layouts, placements))
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
