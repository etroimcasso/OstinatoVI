#!/usr/bin/env python3
"""Emit the field dialog window's tile formation and shading tables.

Port-time tooling (NOT a build/CI dependency): reads field/window.asm,
field/hdma.asm and gfx/window_gfx.asm and emits:

  * include/ostinato/window_tile.h — WindowTile, the 28 tiles of a wallpaper,
    named from where the box's formation places them; each value is the tile's
    place in the wallpaper's art.
  * include/ostinato/wallpaper_id.h — WallpaperId, the eight wallpapers.
  * src/data/generated/dialog_window_formation_data.inc — the box's 9 rows of
    32 tiles (DlgWindowTilesTop/Mid/Btm, window.asm:626-648).
  * src/data/generated/dialog_window_shading_data.inc — the five shading
    tables, one per height the box shows (DlgWindowFixedColorTbl,
    hdma.asm:505-534), each row's level 0..7.
  * tests/fixtures/dialog_window_expected.h — all of it as raw cartridge bytes.

Structural guarantees, hard-errored at parse/emit time:
  * the formation is 9 rows of 32: a top border of corner, corner, 14 pairs
    of edge tiles, corner, corner; seven wallpaper rows whose columns 2-29
    repeat a 4 x 4 pattern and whose edges alternate between two pairs; and a
    bottom border shaped like the top. Every tile name is checked against the
    place the formation puts it;
  * columns 0 and 31 repeat columns 1 and 30 on every row;
  * there are 8 wallpapers, each 28 tiles of 32 bytes, and 8 palettes;
  * the shading tables are 72, 56, 40, 24 and 8 rows, every byte $E0-$E7
    (all three color channels, intensity 0..7);
  * the formation and each shading table occur exactly once in bank $C0 of
    the vanilla cartridge.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_window_data.py --source-root PATH
    parse_window_data.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError, strip_comment

COLUMNS = 32
ROWS = 9
WALLPAPER_ROWS = ROWS - 2
FIRST_TILE = 0xE0
TILES_PER_WALLPAPER = 28
TILE_BYTES = 32
WALLPAPERS = 8
PATTERN = 4                          # the wallpaper repeats every 4 tiles across and down
SHADING_HEIGHTS = (72, 56, 40, 24, 8)
SHADING_CHANNELS = 0xE0              # COLDATA bits 5-7: red, green and blue
SHADING_MAX_LEVEL = 7
BANK_C0 = (0x000000, 0x010000)       # file range of bank $C0, where the field code lives

# The border tiles by their place in the box.
BORDER_NAMES = {
    0xF0: "CORNER_TOP_LEFT",
    0xF1: "EDGE_TOP_A",
    0xF2: "EDGE_TOP_B",
    0xF3: "CORNER_TOP_RIGHT",
    0xF4: "EDGE_LEFT_EVEN",
    0xF5: "EDGE_RIGHT_EVEN",
    0xF6: "EDGE_LEFT_ODD",
    0xF7: "EDGE_RIGHT_ODD",
    0xF8: "CORNER_BOTTOM_LEFT",
    0xF9: "EDGE_BOTTOM_A",
    0xFA: "EDGE_BOTTOM_B",
    0xFB: "CORNER_BOTTOM_RIGHT",
}

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
_RE_LOCAL = re.compile(r"^(@[0-9A-Za-z_]+):\s*(.*)$")
_RE_BYTE = re.compile(r"^\.byte\s+(.+)$", re.IGNORECASE)
_RE_REPEAT = re.compile(r"^\.repeat\s+(\d+)\s*,\s*([A-Za-z_]\w*)$", re.IGNORECASE)
_RE_WORD_STRIDE = re.compile(r"^\.word\s+([A-Za-z_]\w*)\s*\*\s*(\$[0-9A-Fa-f]+|\d+)$",
                             re.IGNORECASE)
_RE_INCBIN = re.compile(r'^\.incbin\s+"([^"]+)"$', re.IGNORECASE)


def _paths(source_root):
    return {
        "window_asm": os.path.join(source_root, "src", "field", "window.asm"),
        "hdma_asm": os.path.join(source_root, "src", "field", "hdma.asm"),
        "window_gfx_asm": os.path.join(source_root, "src", "gfx", "window_gfx.asm"),
    }


def tile_name(tile):
    """The WindowTile name for a formation byte."""
    if tile in BORDER_NAMES:
        return BORDER_NAMES[tile]
    offset = tile - FIRST_TILE
    if 0 <= offset < PATTERN * PATTERN:
        return "WALLPAPER_R{}_C{}".format(offset // PATTERN, offset % PATTERN)
    raise ValueError("${:02X} is not a window tile".format(tile))


def all_tiles():
    """Every window tile, in the order of its place in the wallpaper's art."""
    return list(range(FIRST_TILE, FIRST_TILE + TILES_PER_WALLPAPER))


# --- reading ---------------------------------------------------------------------

def _lines(path):
    with open(path, "r", encoding="utf-8") as fh:
        return fh.readlines()


def _byte_terms(text, path, lineno):
    values = []
    for term in text.split(","):
        value = common.parse_int_literal(term.strip())
        if value is None or not 0 <= value <= 0xFF:
            raise ParseError(path, lineno,
                             "'{}' is not a byte literal".format(term.strip()))
        values.append(value)
    return values


def read_label_bytes(path, label, lines=None):
    """The `.byte` values from `label` to the next global label or `.endproc`."""
    lines = _lines(path) if lines is None else lines
    values = []
    inside = False
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        found = _RE_LABEL.match(s)
        if found:
            if found.group(1) == label:
                inside = True
                continue
            if inside:
                break
            continue
        if not inside:
            continue
        if s.lower().startswith(".endproc"):
            break
        data = _RE_BYTE.match(s)
        if not data:
            raise ParseError(path, lineno,
                             "unexpected line inside {}: '{}' (grammar not covered — "
                             "escalate, never guess)".format(label, s))
        values.extend(_byte_terms(data.group(1), path, lineno))
    if not inside:
        raise ParseError(path, 0, "{} not found".format(label))
    return values


def read_local_groups(path, label, lines=None):
    """The `.byte` runs under each local label (@xxxx:) after `label`, in order."""
    lines = _lines(path) if lines is None else lines
    groups = []
    inside = False
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        found = _RE_LABEL.match(s)
        if found:
            if found.group(1) == label:
                inside = True
                continue
            if inside:
                break
            continue
        if not inside:
            continue
        local = _RE_LOCAL.match(s)
        if local:
            groups.append([])
            s = local.group(2).strip()
            if not s:
                continue
        if not groups:
            raise ParseError(path, lineno,
                             "data before the first local label inside {}".format(label))
        data = _RE_BYTE.match(s)
        if not data:
            raise ParseError(path, lineno,
                             "unexpected line inside {}: '{}' (grammar not covered — "
                             "escalate, never guess)".format(label, s))
        groups[-1].extend(_byte_terms(data.group(1), path, lineno))
    if not inside:
        raise ParseError(path, 0, "{} not found".format(label))
    return groups


def read_gfx_pointer_stride(path, lines=None):
    """DlgWindowGfxPtrs: `.repeat N, i` / `.word i*STRIDE` — returns (N, STRIDE)."""
    lines = _lines(path) if lines is None else lines
    inside = False
    count = None
    for lineno, raw in enumerate(lines, 1):
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        found = _RE_LABEL.match(s)
        if found:
            inside = found.group(1) == "DlgWindowGfxPtrs"
            continue
        if not inside:
            continue
        repeat = _RE_REPEAT.match(s)
        if repeat:
            count = int(repeat.group(1))
            variable = repeat.group(2)
            continue
        word = _RE_WORD_STRIDE.match(s)
        if word and count is not None and word.group(1) == variable:
            return count, common.parse_int_literal(word.group(2))
        raise ParseError(path, lineno,
                         "unexpected line inside DlgWindowGfxPtrs: '{}'".format(s))
    raise ParseError(path, 0, "DlgWindowGfxPtrs not found")


def count_incbins(path, label, lines=None):
    """How many `.incbin` lines follow `label` before the next label."""
    lines = _lines(path) if lines is None else lines
    inside = False
    count = 0
    for raw in lines:
        code, _ = strip_comment(raw)
        s = code.strip()
        if not s:
            continue
        found = _RE_LABEL.match(s)
        if found:
            if inside:
                break
            inside = found.group(1) == label
            continue
        if inside and _RE_INCBIN.match(s):
            count += 1
        elif inside and s.startswith("."):
            break
    if not inside and count == 0:
        raise ParseError(path, 0, "{} not found".format(label))
    return count


class WindowData(object):
    def __init__(self, formation, shading, wallpapers, palettes, stride):
        self.formation = formation    # 9 rows of 32 tile bytes
        self.shading = shading        # {height: [bytes]}
        self.wallpapers = wallpapers
        self.palettes = palettes
        self.stride = stride


def read_window_data(source_root):
    paths = _paths(source_root)
    window_lines = _lines(paths["window_asm"])
    top = read_label_bytes(paths["window_asm"], "DlgWindowTilesTop", window_lines)
    mid = read_label_bytes(paths["window_asm"], "DlgWindowTilesMid", window_lines)
    btm = read_label_bytes(paths["window_asm"], "DlgWindowTilesBtm", window_lines)
    flat = top + mid + btm
    if len(flat) != ROWS * COLUMNS:
        raise ParseError(paths["window_asm"], 0,
                         "the formation holds {} bytes, not {} rows of {}".format(
                             len(flat), ROWS, COLUMNS))
    if len(top) != COLUMNS or len(btm) != COLUMNS:
        raise ParseError(paths["window_asm"], 0,
                         "the top and bottom borders must be one row of {} each".format(COLUMNS))
    formation = [flat[r * COLUMNS:(r + 1) * COLUMNS] for r in range(ROWS)]

    groups = read_local_groups(paths["hdma_asm"], "DlgWindowFixedColorTbl")
    if [len(g) for g in groups] != list(SHADING_HEIGHTS):
        raise ParseError(paths["hdma_asm"], 0,
                         "DlgWindowFixedColorTbl holds tables of {} rows, not {}".format(
                             [len(g) for g in groups], list(SHADING_HEIGHTS)))
    shading = dict(zip(SHADING_HEIGHTS, groups))

    pointers, stride = read_gfx_pointer_stride(paths["window_asm"], window_lines)
    gfx_lines = _lines(paths["window_gfx_asm"])
    wallpapers = count_incbins(paths["window_gfx_asm"], "WindowGfx", gfx_lines)
    palettes = count_incbins(paths["window_gfx_asm"], "WindowPal", gfx_lines)
    if pointers != wallpapers:
        raise ParseError(paths["window_asm"], 0,
                         "{} wallpaper pointers for {} wallpapers".format(pointers, wallpapers))

    data = WindowData(formation, shading, wallpapers, palettes, stride)
    check(data, paths)
    return paths, data


# --- checks ----------------------------------------------------------------------

def expected_formation():
    """The formation the tile names describe, row by row."""
    def border(corner_l, a, b, corner_r):
        return [corner_l, corner_l] + [a, b] * 14 + [corner_r, corner_r]

    rows = [border(0xF0, 0xF1, 0xF2, 0xF3)]
    for wallpaper_row in range(WALLPAPER_ROWS):
        left, right = (0xF4, 0xF5) if wallpaper_row % 2 == 0 else (0xF6, 0xF7)
        pattern_row = wallpaper_row % PATTERN
        inner = [FIRST_TILE + PATTERN * pattern_row + (c % PATTERN)
                 for c in range(COLUMNS - 4)]
        rows.append([left, left] + inner + [right, right])
    rows.append(border(0xF8, 0xF9, 0xFA, 0xFB))
    return rows


def check(data, paths):
    path = paths["window_asm"]
    for r, row in enumerate(data.formation):
        if row[0] != row[1] or row[-1] != row[-2]:
            raise ParseError(path, 0,
                             "formation row {}: columns 0 and 31 do not repeat columns 1 "
                             "and 30".format(r))
        for c, tile in enumerate(row):
            try:
                tile_name(tile)
            except ValueError:
                raise ParseError(path, 0,
                                 "formation row {} column {}: ${:02X} is not a window "
                                 "tile".format(r, c, tile))
    if data.formation != expected_formation():
        for r, (got, want) in enumerate(zip(data.formation, expected_formation())):
            if got != want:
                c = next(i for i in range(COLUMNS) if got[i] != want[i])
                raise ParseError(path, 0,
                                 "formation row {} column {} is ${:02X}, where its place "
                                 "names ${:02X} ({})".format(r, c, got[c], want[c],
                                                            tile_name(want[c])))

    if data.wallpapers != WALLPAPERS or data.palettes != WALLPAPERS:
        raise ParseError(paths["window_gfx_asm"], 0,
                         "{} wallpapers and {} palettes, not {} of each".format(
                             data.wallpapers, data.palettes, WALLPAPERS))
    if data.stride != TILES_PER_WALLPAPER * TILE_BYTES:
        raise ParseError(path, 0,
                         "wallpapers are {} bytes apart, not {} tiles of {}".format(
                             data.stride, TILES_PER_WALLPAPER, TILE_BYTES))

    path = paths["hdma_asm"]
    for height, table in data.shading.items():
        for row, byte in enumerate(table):
            if byte & SHADING_CHANNELS != SHADING_CHANNELS:
                raise ParseError(path, 0,
                                 "{}-row shading row {}: ${:02X} does not set all three "
                                 "channels".format(height, row, byte))
            if byte & ~SHADING_CHANNELS & 0xFF > SHADING_MAX_LEVEL:
                raise ParseError(path, 0,
                                 "{}-row shading row {}: level {} is above {}".format(
                                     height, row, byte & 0x1F, SHADING_MAX_LEVEL))


def occurrences(haystack, needle):
    count = 0
    start = 0
    while True:
        found = haystack.find(needle, start)
        if found < 0:
            return count
        count += 1
        start = found + 1


def assert_rom(rom, data, paths):
    bank = bytes(rom[BANK_C0[0]:BANK_C0[1]])
    formation = bytes(b for row in data.formation for b in row)
    found = occurrences(bank, formation)
    if found != 1:
        raise ParseError(paths["window_asm"], 0,
                         "ROM MISMATCH: the formation occurs {} times in bank $C0, not "
                         "once".format(found))
    for height, table in data.shading.items():
        found = occurrences(bank, bytes(table))
        if found != 1:
            raise ParseError(paths["hdma_asm"], 0,
                             "ROM MISMATCH: the {}-row shading table occurs {} times in "
                             "bank $C0, not once".format(height, found))


# --- rendering -------------------------------------------------------------------

_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_window_data.py\n"
    "// Source: {}\n"
    "// (original-src pinned at 1ea47b5; cross-checked against the vanilla ROM)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_window_data.py \\\n"
    "//       --source-root  original-src\n\n")

_ENUM_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_window_data.py — DO NOT EDIT.\n"
    "// Source: {}\n"
    "// (original-src pinned at 1ea47b5)\n"
    "//\n")


def render_window_tile_h():
    out = [_ENUM_HEADER.format("original-src/src/field/window.asm (DlgWindowTilesTop/Mid/Btm)"),
           "// One of the 28 tiles of a dialog-window wallpaper, named for where the box's\n"
           "// formation places it. The value is the tile's place in the wallpaper's art.\n"
           "// The wallpaper fills the box's middle with a 4 x 4 pattern (WALLPAPER_R<row>_C<column>);\n"
           "// the left and right edges alternate between an even and an odd pair, counting the\n"
           "// wallpaper's rows from 0; the top and bottom borders each have two corners and\n"
           "// an alternating pair of edge tiles.\n"
           "#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\nnamespace ostinato {\n\n"
           "enum class WindowTile : std::uint8_t {\n"]
    names = [tile_name(t) for t in all_tiles()]
    width = max(len(n) for n in names)
    for tile, name in zip(all_tiles(), names):
        out.append("    {} = 0x{:02X},\n".format(name.ljust(width), tile - FIRST_TILE))
    out.append("}};\n\n"
               "inline constexpr std::size_t kWindowTileCount = {};\n\n"
               "}}  // namespace ostinato\n".format(TILES_PER_WALLPAPER))
    return "".join(out)


def render_wallpaper_id_h():
    out = [_ENUM_HEADER.format("original-src/src/gfx/window_gfx.asm (WindowGfx, WindowPal)"),
           "// One of the eight dialog-window wallpapers the cartridge holds, each with its own\n"
           "// art and default colors. The player picks one in the configuration.\n"
           "#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\nnamespace ostinato {\n\n"
           "enum class WallpaperId : std::uint8_t {\n"]
    for i in range(WALLPAPERS):
        out.append("    WALLPAPER_{} = {},\n".format(i, i))
    out.append("}};\n\ninline constexpr std::size_t kWallpaperCount = {};\n\n"
               "}}  // namespace ostinato\n".format(WALLPAPERS))
    return "".join(out)


def render_formation_inc(data):
    out = [_HEADER.format("src/field/window.asm (DlgWindowTilesTop/Mid/Btm, 9 rows x 32 "
                          "tiles)"),
           "// The dialog box's tile formation, one WindowFormationRow per row, top border\n"
           "// first, #included inside the kDialogWindowFormation array in\n"
           "// src/text/window_layout.h. A compile-time assert verifies row == position.\n"
           "// Columns 0 and 31 repeat columns 1 and 30; the field never shows them.\n\n"]
    for r, row in enumerate(data.formation):
        names = ["WindowTile::" + tile_name(t) for t in row]
        lines = [", ".join(names[i:i + 4]) for i in range(0, COLUMNS, 4)]
        out.append("    WindowFormationRow{{ .row = {}, .tiles = {{{{\n".format(r))
        for line in lines:
            out.append("        {},\n".format(line))
        out.append("    }} },\n")
    return "".join(out)


def render_shading_inc(data):
    out = [_HEADER.format("src/field/hdma.asm (DlgWindowFixedColorTbl, 5 tables)"),
           "// How strongly each row of the dialog box is shaded, one table per height the\n"
           "// box shows: 72 rows open, and 56, 40, 24 and 8 on the opening frames. Each row's\n"
           "// level, 0 to 7, is added to all three color channels (in 31sts) above the\n"
           "// table's middle row and subtracted from it down. #included at namespace scope in\n"
           "// src/text/window_layout.h; compile-time asserts verify row == position.\n\n"]
    for height in SHADING_HEIGHTS:
        out.append("inline constexpr std::array<WindowShadingRow, {}> kWindowShading{} = {{{{\n"
                   .format(height, height))
        for row, byte in enumerate(data.shading[height]):
            out.append("    {{ .row = {:2}, .level = {} }},\n".format(row, byte & 0x1F))
        out.append("}};\n\n")
    return "".join(out).rstrip("\n") + "\n"


def render_fixture(data):
    out = [_HEADER.format("src/field/window.asm (DlgWindowTilesTop/Mid/Btm), src/field/hdma.asm "
                          "(DlgWindowFixedColorTbl)"),
           "// Test fixture for tests/test_window_components.cpp — the formation and the five\n"
           "// shading tables as raw cartridge bytes, deliberately independent of the typed\n"
           "// rows in the generated tables.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"
           "// One row of the box's formation: its 32 tile bytes as the cartridge stores them.\n"
           "struct ExpectedFormationRow {\n"
           "    std::uint8_t row;\n"
           "    std::array<std::uint8_t, 32> tiles;\n"
           "};\n\n"
           "inline constexpr std::array<ExpectedFormationRow, 9> kExpectedDialogWindowFormation = {{\n"]
    for r, row in enumerate(data.formation):
        out.append("    {{ .row = {}, .tiles = {{{{ {} }}}} }},\n".format(
            r, ", ".join("0x{:02X}".format(b) for b in row)))
    out.append("}};\n\n"
               "// One row of a shading table: the color-math byte the cartridge stores.\n"
               "struct ExpectedShadingRow {\n"
               "    std::uint8_t row;\n"
               "    std::uint8_t value;\n"
               "};\n")
    for height in SHADING_HEIGHTS:
        out.append("\ninline constexpr std::array<ExpectedShadingRow, {}> kExpectedWindowShading{} "
                   "= {{{{\n".format(height, height))
        for row, byte in enumerate(data.shading[height]):
            out.append("    {{ .row = {:2}, .value = 0x{:02X} }},\n".format(row, byte))
        out.append("}};\n")
    out.append("\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ----------------------------------------------------------------------

def run(source_root, outputs, check_only=False):
    paths, data = read_window_data(source_root)
    assert_rom(common.load_vanilla_rom(source_root), data, paths)
    if check_only:
        print("OK: formation {} x {}, {} wallpapers of {} tiles, shading tables {}; "
              "each found once in bank $C0.".format(
                  ROWS, COLUMNS, data.wallpapers, TILES_PER_WALLPAPER,
                  "/".join(str(h) for h in SHADING_HEIGHTS)))
        return 0
    _write(outputs["window_tile"], render_window_tile_h())
    _write(outputs["wallpaper_id"], render_wallpaper_id_h())
    _write(outputs["formation"], render_formation_inc(data))
    _write(outputs["shading"], render_shading_inc(data))
    _write(outputs["fixture"], render_fixture(data))
    for key in ("window_tile", "wallpaper_id", "formation", "shading", "fixture"):
        print("Emitted -> {}".format(outputs[key]))
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
    ap.add_argument("--window-tile-out", default="include/ostinato/window_tile.h")
    ap.add_argument("--wallpaper-id-out", default="include/ostinato/wallpaper_id.h")
    ap.add_argument("--formation-out",
                    default="src/data/generated/dialog_window_formation_data.inc")
    ap.add_argument("--shading-out",
                    default="src/data/generated/dialog_window_shading_data.inc")
    ap.add_argument("--fixture-out", default="tests/fixtures/dialog_window_expected.h")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)
    outputs = {"window_tile": args.window_tile_out,
               "wallpaper_id": args.wallpaper_id_out,
               "formation": args.formation_out,
               "shading": args.shading_out,
               "fixture": args.fixture_out}
    try:
        return run(args.source_root, outputs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
