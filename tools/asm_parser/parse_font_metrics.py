#!/usr/bin/env python3
"""Locate the dialogue font's width table and the choice-cursor arrow.

Port-time tooling (NOT a build/CI dependency). Both are cartridge content the
game reads at run time: the width table says how far the pen moves after each
glyph, and the arrow is the art drawn beside the selected choice. Neither is in
the upstream rip list — the widths sit in the assembly between two ripped font
files, and the arrow sits in the middle of the field code — so this script works
out where each one lives and proves it, and parse_rom_regions.py emits the
region rows through its assembly-sourced seam (_ASM_REGIONS there).

Nothing is emitted from here. The bytes themselves are never written into the
repository: the game and the tests read them from the cartridge.

Sources:

  * src/gfx/font_gfx.asm — the width table (`FontWidth`), between the small
    font's graphics and the dialogue font's in one segment. English states 256
    widths as `.byte` rows; Japanese reserves 1,280 with `.res` runs.
  * The rip lists' SMALL_FONT and LARGE_FONT rows — the two files bracketing the
    table. Their addresses settle the table's in each language: it starts where
    the small font ends and ends where the dialogue font begins.
  * src/field/text.asm — the arrow (`DlgCursorGfx`, 12 `.byte` rows) and the one
    instruction that reads it (`lda f:DlgCursorGfx,x` in DrawDlgCursor).
  * The vanilla cartridge — the oracle. The English widths are compared with
    the bytes at their address. The arrow's address is only settled by the
    assembler, so it is read off the cartridge: the arrow's bytes occur once in
    bank $C0, and the instruction that reads it names that address.

Structural guarantees, hard-errored:
  * `SmallFontGfx`, `FontWidth` and `LargeFontGfx` are the font segment's
    labels, in that order, so nothing else sits between the two fonts;
  * the English table is exactly 256 widths and the Japanese `.res` runs total
    exactly 1,280, and in both languages the gap between the two ripped fonts is
    exactly that long;
  * the English widths are identical to the cartridge;
  * the arrow is exactly 12 rows, its bytes occur exactly once in bank $C0, and
    the cartridge's one long load from that address is the routine's;
  * nothing the field code assembles is conditional on the US revision, so the
    arrow's address holds for both US cartridges. Japanese moves it by an amount
    only an assembler settles, so no Japanese address is stated.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_font_metrics.py --source-root PATH
    (Validates and prints where each one lives.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError

# Glyph codes each language's width table covers.
EN_WIDTH_COUNT = 256
JP_WIDTH_COUNT = 1280

# Rows in the arrow's art.
CURSOR_ROWS = 12

# The bank the field code lives in; the arrow is searched for here.
FIELD_BANK = 0xC00000
BANK_SIZE = 0x10000

# The 65C816 opcode of `lda long,x`, the instruction DrawDlgCursor reads the
# arrow with (field/text.asm:1171).
LDA_LONG_X = 0xBF

# The labels of the font segment, in the order the table needs them.
FONT_SEGMENT_LABELS = ("SmallFontGfx", "FontWidth", "LargeFontGfx")

# The symbols that tell the two US revisions apart (include/const.inc:27-34).
REVISION_SYMBOLS = ("ROM_VERSION", "LANG_EN_REV1")

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):$")
_RE_SEGMENT = re.compile(r'^\.segment\s+"([^"]+)"$')
_RE_BYTE = re.compile(r"^\.byte\s+(.+)$")
_RE_RES = re.compile(r"^\.res\s+(\S+)\s*,\s*(\S+)$")
_RE_CONDITIONAL = re.compile(r"^\.(?:if|ifdef|ifndef|elseif)\b(.*)$")
_RE_CURSOR_LOAD = re.compile(r"\blda\s+f:DlgCursorGfx,x\b")


class Placement(object):
    """Where one family sits in one language, and how much of it was proved
    against the cartridge."""

    def __init__(self, language, begin, size, proved):
        self.language = language  # "en" / "jp"
        self.begin = begin        # SNES address of the first byte
        self.size = size          # byte count
        self.proved = proved      # bytes compared with the cartridge


class FontWidths(object):
    """The width table as the source states it."""

    def __init__(self, path, line, en_widths, jp_count):
        self.path = path
        self.line = line              # 1-based line of the FontWidth label
        self.en_widths = en_widths    # the 256 English widths
        self.jp_count = jp_count      # the bytes the Japanese .res runs reserve


class CursorArt(object):
    """The arrow as the source states it."""

    def __init__(self, path, line, rows):
        self.path = path
        self.line = line   # 1-based line of the DlgCursorGfx label
        self.rows = rows   # one byte per row, leftmost pixel in the high bit


# --- source reading ----------------------------------------------------------

def _int(token, path, lineno):
    value = common.parse_int_literal(token.strip())
    if value is None:
        raise ParseError(path, lineno,
                         "unparsable value {!r}".format(token.strip()))
    return value


def _byte_values(text, path, lineno):
    values = []
    for term in text.split(","):
        value = _int(term, path, lineno)
        if not 0 <= value <= 0xFF:
            raise ParseError(path, lineno,
                             "byte value {} out of range".format(value))
        values.append(value)
    return values


def _read_lines(path):
    if not os.path.isfile(path):
        raise ParseError(path, 0, "source file not found")
    with open(path, "r", encoding="utf-8") as fh:
        return fh.readlines()


def read_font_widths(source_root):
    """The width table: the English rows and the Japanese reservation."""
    path = os.path.join(source_root, "src", "gfx", "font_gfx.asm")
    lines = _read_lines(path)

    # The labels of the segment the table lives in, in source order.
    segment = None
    label_lines = {}
    segment_labels = []
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        code = code.strip()
        match = _RE_SEGMENT.match(code)
        if match:
            segment = match.group(1)
            continue
        match = _RE_LABEL.match(code)
        if match and segment == "font_gfx":
            segment_labels.append(match.group(1))
            label_lines[match.group(1)] = idx
    if tuple(segment_labels) != FONT_SEGMENT_LABELS:
        raise ParseError(path, 0,
                         "the font segment's labels are {} — expected {}, with "
                         "the width table between the two fonts"
                         .format(segment_labels, list(FONT_SEGMENT_LABELS)))

    start = label_lines["FontWidth"]
    end = label_lines["LargeFontGfx"]
    en_widths = []
    jp_count = 0
    branch = None
    for idx in range(start + 1, end):
        code, _comment = common.strip_comment(lines[idx])
        code = code.strip()
        if not code:
            continue
        if code == ".if LANG_EN":
            branch = "en"
            continue
        if code == ".else" and branch == "en":
            branch = "jp"
            continue
        if code == ".endif" and branch == "jp":
            branch = "done"
            continue
        byte = _RE_BYTE.match(code)
        if byte and branch == "en":
            en_widths.extend(_byte_values(byte.group(1), path, idx + 1))
            continue
        res = _RE_RES.match(code)
        if res and branch == "jp":
            count = _int(res.group(1), path, idx + 1)
            fill = _int(res.group(2), path, idx + 1)
            if not 0 <= fill <= 0xFF:
                raise ParseError(path, idx + 1,
                                 "fill value {} out of range".format(fill))
            jp_count += count
            continue
        raise ParseError(path, idx + 1,
                         "unexpected line in the width table: {!r}"
                         .format(code))
    if branch != "done":
        raise ParseError(path, start + 1,
                         "the width table is not one .if LANG_EN / .else / "
                         ".endif block")
    if len(en_widths) != EN_WIDTH_COUNT:
        raise ParseError(path, start + 1,
                         "the English table holds {} widths, expected {}"
                         .format(len(en_widths), EN_WIDTH_COUNT))
    if jp_count != JP_WIDTH_COUNT:
        raise ParseError(path, start + 1,
                         "the Japanese table reserves {} bytes, expected {}"
                         .format(jp_count, JP_WIDTH_COUNT))
    return FontWidths(path=path, line=start + 1, en_widths=en_widths,
                      jp_count=jp_count)


def read_cursor_art(source_root):
    """The arrow's rows, and a check that exactly one instruction reads them."""
    path = os.path.join(source_root, "src", "field", "text.asm")
    lines = _read_lines(path)

    start = None
    loads = 0
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        if code.strip() == "DlgCursorGfx:":
            if start is not None:
                raise ParseError(path, idx + 1, "DlgCursorGfx is defined twice")
            start = idx
        if _RE_CURSOR_LOAD.search(code):
            loads += 1
    if start is None:
        raise ParseError(path, 0, "DlgCursorGfx not found")
    if loads != 1:
        raise ParseError(path, 0,
                         "{} instructions read DlgCursorGfx, expected 1"
                         .format(loads))

    rows = []
    for idx in range(start + 1, len(lines)):
        code, _comment = common.strip_comment(lines[idx])
        code = code.strip()
        byte = _RE_BYTE.match(code)
        if not byte:
            break
        rows.extend(_byte_values(byte.group(1), path, idx + 1))
    if len(rows) != CURSOR_ROWS:
        raise ParseError(path, start + 1,
                         "the arrow has {} rows, expected {}"
                         .format(len(rows), CURSOR_ROWS))
    return CursorArt(path=path, line=start + 1, rows=rows)


def assert_no_revision_conditionals(source_root):
    """Nothing the field code assembles depends on which US revision is built.

    The field module's own files may branch on language, which moves the field
    code in the Japanese build, but never on revision. The include files only
    define the revision symbols. Returns how many files were checked.
    """
    checked = 0
    field = os.path.join(source_root, "src", "field")
    for name in sorted(os.listdir(field)):
        if not name.endswith(".asm"):
            continue
        path = os.path.join(field, name)
        for idx, raw in enumerate(_read_lines(path)):
            code, _comment = common.strip_comment(raw)
            match = _RE_CONDITIONAL.match(code.strip())
            if match and any(s in match.group(1) for s in REVISION_SYMBOLS):
                raise ParseError(path, idx + 1,
                                 "the field code branches on the US revision, "
                                 "so the two US cartridges may place the arrow "
                                 "differently — escalate")
        checked += 1

    include = os.path.join(source_root, "include")
    for name in sorted(os.listdir(include)):
        if not name.endswith(".inc") or name == "const.inc":
            continue
        path = os.path.join(include, name)
        for idx, raw in enumerate(_read_lines(path)):
            code, _comment = common.strip_comment(raw)
            if any(re.search(r"\b{}\b".format(s), code)
                   for s in REVISION_SYMBOLS):
                raise ParseError(path, idx + 1,
                                 "{} uses a revision symbol — the field code "
                                 "may differ between the US cartridges"
                                 .format(name))
        checked += 1
    return checked


# --- placement ---------------------------------------------------------------

def _rip_row(rows, asset, language):
    found = [row for row in rows if row.asset == asset]
    if len(found) != 1:
        raise ParseError("tools/rip_list_{}.json".format(language), 0,
                         "expected one {} row, found {}"
                         .format(asset, len(found)))
    return found[0]


def place_font_widths(widths, rows_by_language, rom=None):
    """The table's place in each language, between the two ripped fonts.

    `rows_by_language` maps "en" / "jp" to rows carrying .asset, .begin and .end
    (parse_rom_regions.RegionRow). With `rom`, the English widths are compared
    with the cartridge.
    """
    sizes = {"en": len(widths.en_widths), "jp": widths.jp_count}
    placements = []
    for language in ("en", "jp"):
        rows = rows_by_language[language]
        small = _rip_row(rows, "SMALL_FONT", language)
        large = _rip_row(rows, "LARGE_FONT", language)
        begin = small.end + 1
        gap = large.begin - begin
        if gap != sizes[language]:
            raise ParseError(widths.path, widths.line,
                             "{}: the fonts leave {} bytes between them but the "
                             "table is {}".format(language, gap,
                                                  sizes[language]))
        proved = 0
        if language == "en" and rom is not None:
            offset = common.hirom_file_offset(begin)
            actual = list(rom[offset:offset + gap])
            if actual != widths.en_widths:
                for glyph, (got, want) in enumerate(zip(actual,
                                                        widths.en_widths)):
                    if got != want:
                        raise ParseError(
                            widths.path, widths.line,
                            "ROM MISMATCH at glyph ${:02x} (${:06x}): ROM {} "
                            "!= source {}".format(glyph, begin + glyph, got,
                                                  want))
            proved = gap
        placements.append(Placement(language=language, begin=begin,
                                    size=gap, proved=proved))
    return placements


def _occurrences(haystack, needle):
    found = []
    at = haystack.find(needle)
    while at != -1:
        found.append(at)
        at = haystack.find(needle, at + 1)
    return found


def place_cursor_art(art, rom):
    """The arrow's English address, read off the cartridge.

    The arrow's bytes must occur once in the field code's bank, and the one
    `lda long,x` naming that address must be the only instruction in the bank
    that does.
    """
    if rom is None:
        raise ParseError(art.path, art.line,
                         "the arrow's address is read off the cartridge — set "
                         "FF6_VANILLA_ROM")
    base = common.hirom_file_offset(FIELD_BANK)
    bank = bytes(rom[base:base + BANK_SIZE])
    hits = _occurrences(bank, bytes(art.rows))
    if len(hits) != 1:
        raise ParseError(art.path, art.line,
                         "the arrow's bytes occur {} times in bank ${:02x}, "
                         "expected exactly once"
                         .format(len(hits), FIELD_BANK >> 16))
    begin = FIELD_BANK | hits[0]

    load = bytes([LDA_LONG_X, begin & 0xFF, (begin >> 8) & 0xFF,
                  (begin >> 16) & 0xFF])
    loads = _occurrences(bank, load)
    if len(loads) != 1:
        raise ParseError(art.path, art.line,
                         "{} long loads of ${:06x} in bank ${:02x}, expected "
                         "DrawDlgCursor's one"
                         .format(len(loads), begin, FIELD_BANK >> 16))
    return [Placement(language="en", begin=begin, size=len(art.rows),
                      proved=len(art.rows))]


# --- driver ------------------------------------------------------------------

def run(source_root, rows_by_language):
    """Resolve both families; returns {asset: [Placement]}."""
    rom = None
    if common.find_vanilla_rom(source_root) is not None:
        rom = common.load_vanilla_rom(source_root)
    assert_no_revision_conditionals(source_root)
    return {
        "FONT_WIDTH": place_font_widths(read_font_widths(source_root),
                                        rows_by_language, rom),
        "DLG_CURSOR_GFX": place_cursor_art(read_cursor_art(source_root), rom),
    }


def main(argv=None):
    import parse_rom_regions  # the rip-list reader

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root", default="original-src",
                    help="disassembly root")
    args = ap.parse_args(argv)
    try:
        rom_size = parse_rom_regions.read_rom_size(args.source_root)
        rows = {language: parse_rom_regions.read_rip_list(args.source_root,
                                                          language, rom_size)
                for language in ("en", "jp")}
        placed = run(args.source_root, rows)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2
    for asset, placements in placed.items():
        for p in placements:
            print("OK: {} {} ${:06X} + {} B ({} B proved against the cartridge)"
                  .format(asset, p.language, p.begin, p.size, p.proved))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
