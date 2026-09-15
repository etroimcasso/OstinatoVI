#!/usr/bin/env python3
"""Emit the battle-background property table from original-src battle_bg.asm.

Port-time tooling (NOT a build/CI dependency): reads the `BattleBGProp` table in
src/gfx/battle_bg.asm and the asset-id enums in include/gfx/battle_bg.inc, and
emits:

  * include/ostinato/battle_background_graphics_id.h  — BATTLE_BG_GFX
  * include/ostinato/battle_background_tilemap_id.h   — BATTLE_BG_TILES
  * include/ostinato/battle_background_palette_id.h   — BATTLE_BG_PAL
  * src/data/generated/battle_bg_prop_data.inc        — one designated-initializer
    BattleBackgroundPropertiesEntry row per record (56), keyed by the
    BattleBackgroundId enumerator the record's position names.
  * tests/fixtures/battle_bg_prop_expected.h          — the same 56 records as raw
    6-byte rows (the ground-truth byte contract).

The table's grammar is six `.byte` expressions per record, each a scoped enum
reference optionally OR'd with a flag bit:

    .byte BATTLE_BG_GFX::FOREST_2 | $80     ; graphics 1, 128-tile form
    .byte BATTLE_BG_PAL::DESERT_WOB | $80   ; palette, wavy HDMA effect

Structural guarantees, hard-errored at parse/emit time:
  * every record emits exactly 6 bytes, each in 0..255;
  * each byte position references the scope that position is documented to use
    (battle_bg.asm:31-36) — a row that references another scope is a grammar
    deviation and stops the run rather than being coerced;
  * only bytes 0 and 5 may carry the $80 flag (battle_bg.asm:44, :48);
  * the record count equals the non-sentinel BATTLE_BG index space (56), and
    every record index has a BATTLE_BG name;
  * the assembled 336 bytes are byte-identical to the vanilla cartridge at
    $E7/0000 over the table's whole extent.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_battle_bg.py --source-root PATH
    parse_battle_bg.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
from common import ParseError, strip_comment

# --- the table -----------------------------------------------------------------

# battle_bg.asm:50 — the table's own address comment.
BATTLE_BG_PROP_SNES = 0xE70000
RECORD_BYTES = 6
TABLE_LABEL = "BattleBGProp"

# battle_bg.asm:31-36 — which asset-id space each byte of a record names, and
# whether that byte carries a flag in bit 7 (:44 graphics 1, :48 palette).
_FIELD_SCOPES = (
    ("graphics1", "BATTLE_BG_GFX", True),
    ("graphics2", "BATTLE_BG_GFX", False),
    ("graphics3", "BATTLE_BG_GFX", False),
    ("tilemap1", "BATTLE_BG_TILES", False),
    ("tilemap2", "BATTLE_BG_TILES", False),
    ("palette", "BATTLE_BG_PAL", True),
)

_FLAG_BIT = 0x80

_ENUMS = ("BATTLE_BG", "BATTLE_BG_GFX", "BATTLE_BG_TILES", "BATTLE_BG_PAL")

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
_RE_BYTE = re.compile(r"^\.byte\s+(.+)$")
_RE_SCOPED = re.compile(r"^([A-Z][A-Z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)$")


class Field(object):
    """One resolved record byte: its enum member name plus the flag bit."""

    def __init__(self, member, flag, value, from_literal=False):
        self.member = member
        self.flag = flag
        self.value = value
        # True when the corpus wrote a bare number here instead of a symbol.
        self.from_literal = from_literal


class Record(object):
    """One BattleBGProp record: six resolved fields plus its six ROM bytes."""

    def __init__(self, index):
        self.index = index
        self.name = None          # BATTLE_BG member name, by index
        self.fields = []          # list[Field], len 6
        self.bytes = []           # list[int], len 6


class _Symbols(object):
    """The battle_bg.inc asset-id enums the table resolves against."""

    def __init__(self, inc_path):
        self.path = inc_path
        self.parsed = common.parse_ca65_constants(inc_path)
        for name in _ENUMS:
            if self.parsed.enum(name) is None:
                raise ParseError(inc_path, 0,
                                 "expected enum '{}' not found".format(name))

    def value(self, enum_name, member, path, lineno):
        val = self.parsed.enum(enum_name).value_of(member)
        if val is None:
            raise ParseError(path, lineno,
                             "unknown {}::{}".format(enum_name, member))
        return val

    def members(self, enum_name):
        return [(m.name, m.value) for m in self.parsed.enum(enum_name).members]

    def member_named(self, enum_name, value):
        """The member holding `value`, or None when the value is unnameable."""
        for m in self.parsed.enum(enum_name).members:
            if m.value == value:
                return m.name
        return None

    def background_name_by_index(self):
        return {m.value: m.name
                for m in self.parsed.enum("BATTLE_BG").members
                if m.value != 0xFF}


def _resolve_byte(expr, position, symbols, path, lineno):
    """Resolve one `.byte` expression against the scope its position requires."""
    field_name, scope, flag_allowed = _FIELD_SCOPES[position]
    flag = False
    text = expr.strip()
    if "|" in text:
        head, _, tail = text.partition("|")
        tail = tail.strip()
        bit = common.parse_int_literal(tail)
        if bit is None:
            raise ParseError(path, lineno,
                             "{}: flag term '{}' is not an integer literal"
                             .format(field_name, tail))
        if bit != _FLAG_BIT:
            raise ParseError(path, lineno,
                             "{}: unexpected flag ${:02x} (only ${:02x} is "
                             "documented)".format(field_name, bit, _FLAG_BIT))
        if not flag_allowed:
            raise ParseError(path, lineno,
                             "{}: byte {} carries a ${:02x} flag, which only "
                             "graphics 1 and the palette do (battle_bg.asm:44,:48)"
                             .format(field_name, position, _FLAG_BIT))
        flag = True
        text = head.strip()

    m = _RE_SCOPED.match(text)
    if m:
        got_scope, member = m.group(1), m.group(2)
        if got_scope != scope:
            raise ParseError(path, lineno,
                             "{}: byte {} references {} but this position names "
                             "{} (battle_bg.asm:31-36)"
                             .format(field_name, position, got_scope, scope))
        value = symbols.value(scope, member, path, lineno)
    else:
        # A few rows write a bare literal where the rest of the table writes a
        # symbol. The byte is the contract, so it resolves to whatever member
        # holds that value — never to NONE, which is $FF and not 0.
        literal = common.parse_int_literal(text)
        if literal is None:
            raise ParseError(path, lineno,
                             "{}: '{}' is neither a scoped {} reference nor an "
                             "integer literal (grammar not covered — escalate, "
                             "never guess)".format(field_name, text, scope))
        member = symbols.member_named(scope, literal)
        if member is None:
            raise ParseError(path, lineno,
                             "{}: literal {} has no {} member — the byte would "
                             "be unnameable".format(field_name, literal, scope))
        value = literal
        if flag:
            value |= _FLAG_BIT
        if not (0 <= value <= 0xFF):
            raise ParseError(path, lineno,
                             "{}: byte value {} out of range 0..255"
                             .format(field_name, value))
        return Field(member, flag, value, from_literal=True)
    if flag:
        if value & _FLAG_BIT:
            raise ParseError(path, lineno,
                             "{}: {}::{} already sets bit 7; the flag would be "
                             "lossy".format(field_name, scope, member))
        value |= _FLAG_BIT
    if not (0 <= value <= 0xFF):
        raise ParseError(path, lineno,
                         "{}: byte value {} out of range 0..255"
                         .format(field_name, value))
    return Field(member, flag, value)


def parse_battle_bg_prop(asm_path, symbols):
    """Walk BattleBGProp, returning one Record per six `.byte` expressions."""
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    exprs = []          # list[(expr, lineno)]
    in_table = False
    macro_depth = 0
    for idx, raw in enumerate(lines):
        lineno = idx + 1
        code, _comment = strip_comment(raw)
        if not code:
            continue
        s = code.strip()
        low = s.lower()
        if low.startswith(".endmac"):
            macro_depth = max(0, macro_depth - 1)
            continue
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if macro_depth > 0:
            continue

        m = _RE_LABEL.match(s)
        if m:
            if m.group(1) == TABLE_LABEL:
                in_table = True
            elif in_table:
                break           # the next global label ends the table
            continue
        if not in_table:
            continue

        b = _RE_BYTE.match(s)
        if b:
            exprs.append((b.group(1), lineno))
            continue
        if s.startswith("."):
            raise ParseError(asm_path, lineno,
                             "unexpected directive '{}' inside {} (grammar not "
                             "covered — escalate, never guess)".format(s, TABLE_LABEL))

    if not exprs:
        raise ParseError(asm_path, 0, "{} not found or empty".format(TABLE_LABEL))
    if len(exprs) % RECORD_BYTES:
        raise ParseError(asm_path, exprs[-1][1],
                         "{} holds {} bytes, not a whole number of {}-byte "
                         "records".format(TABLE_LABEL, len(exprs), RECORD_BYTES))

    records = []
    for i in range(0, len(exprs), RECORD_BYTES):
        rec = Record(i // RECORD_BYTES)
        for position in range(RECORD_BYTES):
            expr, lineno = exprs[i + position]
            field = _resolve_byte(expr, position, symbols, asm_path, lineno)
            rec.fields.append(field)
            rec.bytes.append(field.value)
        records.append(rec)

    _assign_names_and_verify(records, symbols, asm_path)
    return records


def _assign_names_and_verify(records, symbols, path):
    names = symbols.background_name_by_index()
    if len(records) != len(names):
        raise ParseError(path, 0,
                         "{} produced {} records; the non-sentinel BATTLE_BG "
                         "index space is {}".format(TABLE_LABEL, len(records),
                                                    len(names)))
    for rec in records:
        if rec.index not in names:
            raise ParseError(path, 0,
                             "record index {} has no BATTLE_BG name".format(rec.index))
        rec.name = names[rec.index]


def assert_rom(rom, records, path):
    """The assembled table must match the cartridge over its whole extent."""
    base = common.hirom_file_offset(BATTLE_BG_PROP_SNES)
    want = [b for rec in records for b in rec.bytes]
    got = list(rom[base:base + len(want)])
    if len(got) != len(want):
        raise ParseError(path, 0,
                         "ROM MISMATCH: cartridge holds {} bytes at ${:06x}, "
                         "source assembles {}".format(len(got),
                                                      BATTLE_BG_PROP_SNES, len(want)))
    for i, (g, w) in enumerate(zip(got, want)):
        if g != w:
            rec, pos = divmod(i, RECORD_BYTES)
            raise ParseError(path, 0,
                             "ROM MISMATCH at record {} ({}) byte {} ({}): "
                             "cartridge ${:02x} != source ${:02x}"
                             .format(rec, records[rec].name, pos,
                                     _FIELD_SCOPES[pos][0], g, w))


# --- rendering: enum headers ---------------------------------------------------

_BANNER = ("// AUTO-GENERATED by tools/asm_parser/parse_battle_bg.py — "
           "DO NOT EDIT.\n")


def render_enum_h(type_name, enum_name, members, blurb):
    width = "std::uint16_t" if max(v for _, v in members) > 0xFF \
        else "std::uint8_t"
    name_w = max(len(n) for n, _ in members)
    out = [_BANNER,
           "// Source: original-src/include/gfx/battle_bg.inc (ca65 .enum {})\n"
           "// (original-src pinned at 1ea47b5)\n"
           "//\n".format(enum_name)]
    out.append(blurb)
    out.append("#pragma once\n\n"
               "#include <cstdint>\n\n"
               "namespace ostinato {\n\n")
    out.append("enum class {} : {} {{\n".format(type_name, width))
    for name, value in members:
        out.append("    {}{} = 0x{:02X},\n".format(
            name, " " * (name_w - len(name)), value))
    out.append("};\n\n}  // namespace ostinato\n")
    return "".join(out)


_GFX_BLURB = (
    "// One 64-tile battle-background graphics block. A record in\n"
    "// src/data/generated/battle_bg_prop_data.inc names up to three; NONE\n"
    "// ($FF) means that block is not loaded. When a record's first block sets\n"
    "// the double-width flag the block is 128 tiles and the second is skipped.\n")

_TILES_BLURB = (
    "// One 32x32 battle-background tilemap. Only the first 19 rows are visible\n"
    "// unless the background scrolls vertically.\n")

_PAL_BLURB = (
    "// One battle-background palette (48 colours, BG palettes 5-7).\n")


# --- rendering: the .inc and the fixture ---------------------------------------

_REGEN = ("// DO NOT EDIT BY HAND — regenerate via:\n"
          "//   python3 tools/asm_parser/parse_battle_bg.py \\\n"
          "//       --source-root  original-src\n")

_INC_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_battle_bg.py\n"
    "// Source: src/gfx/battle_bg.asm (BattleBGProp, 56 records x 6 bytes,\n"
    "//         ROM $E7/0000)\n"
    "// Source: include/gfx/battle_bg.inc (BATTLE_BG / BATTLE_BG_GFX /\n"
    "//         BATTLE_BG_TILES / BATTLE_BG_PAL values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    + _REGEN + "\n")


def _slot(kind, member, flag):
    """A named builder so neither half of a packed byte becomes a literal."""
    if kind == "graphics1":
        form = "doubleWidth" if flag else "single"
        return ("BattleBackgroundGraphicsSlot::{}(\n"
                "                BattleBackgroundGraphicsId::{})".format(form, member))
    form = "wavy" if flag else "plain"
    return ("BattleBackgroundPaletteSlot::{}(\n"
            "                BattleBackgroundPaletteId::{})".format(form, member))


def render_inc(records):
    out = [_INC_HEADER,
           "// BattleBackgroundPropertiesEntry rows in BATTLE_BG record-index\n"
           "// order ($00..$37), one designated-initializer row per record,\n"
           "// #included inside the kBattleBackgroundProperties array in\n"
           "// src/data/battle_backgrounds.cpp. Each row's identity is its .id\n"
           "// field — the BattleBackgroundId enumerator\n"
           "// (include/ostinato/battle_background_id.h) — and a compile-time\n"
           "// assert verifies id == position. The packed .record stays\n"
           "// byte-identical to the 6 ROM bytes: the graphics-1 and palette\n"
           "// bytes each pack an asset id with a flag in bit 7, so both are\n"
           "// built through a named builder rather than a literal.\n\n"]
    for rec in records:
        g1, g2, g3, t1, t2, pal = rec.fields
        out.append("    BattleBackgroundPropertiesEntry{{  // [${:02X}]\n".format(
            rec.index))
        out.append("        .id = BattleBackgroundId::{},\n".format(rec.name))
        out.append("        .record = BattleBackgroundProperties{\n")
        out.append("            .graphics1 = {},\n".format(
            _slot("graphics1", g1.member, g1.flag)))
        out.append("            .graphics2 = BattleBackgroundGraphicsId::{},\n"
                   .format(g2.member))
        out.append("            .graphics3 = BattleBackgroundGraphicsId::{},\n"
                   .format(g3.member))
        out.append("            .tilemap1 = BattleBackgroundTilemapId::{},\n"
                   .format(t1.member))
        out.append("            .tilemap2 = BattleBackgroundTilemapId::{},\n"
                   .format(t2.member))
        out.append("            .palette = {},\n".format(
            _slot("palette", pal.member, pal.flag)))
        out.append("        },\n    },\n")
    return "".join(out)


_FIXTURE_STRUCT = (
    "// One raw 6-byte BattleBGProp record; field names and order mirror\n"
    "// battle_bg.asm:31-36. Values are the exact ROM bytes with every upstream\n"
    "// symbol resolved and the bit-7 flags folded in — deliberately independent\n"
    "// of the enum-symbol rows in battle_bg_prop_data.inc, so symbol/value\n"
    "// drift in either artifact fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedBattleBackgroundRecord {\n"
    "    std::uint8_t graphics1;  // BATTLE_BG_GFX | bit 7: 128-tile form\n"
    "    std::uint8_t graphics2;  // BATTLE_BG_GFX\n"
    "    std::uint8_t graphics3;  // BATTLE_BG_GFX\n"
    "    std::uint8_t tilemap1;   // BATTLE_BG_TILES\n"
    "    std::uint8_t tilemap2;   // BATTLE_BG_TILES (unused by the loader)\n"
    "    std::uint8_t palette;    // BATTLE_BG_PAL | bit 7: wavy HDMA effect\n"
    "};\n"
    "static_assert(sizeof(ExpectedBattleBackgroundRecord) == 6,\n"
    "              \"fixture record must stay byte-identical to a ROM \"\n"
    "              \"BattleBGProp record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (raw decimal\n"
    "// index — the fixture stays independent of the port's BattleBackgroundId\n"
    "// header) alongside the raw record bytes.\n"
    "struct ExpectedBattleBackgroundEntry {\n"
    "    std::uint8_t id;\n"
    "    ExpectedBattleBackgroundRecord record;\n"
    "};\n"
)


def render_fixture(records):
    out = [_INC_HEADER,
           "// Test fixture for tests/test_battle_backgrounds.cpp — the\n"
           "// ground-truth record bytes. The full-corpus test asserts, per\n"
           "// entry: fixture id == position, table id enumerator == position,\n"
           "// and a 6-byte memcmp of the packed record against\n"
           "// src/data/generated/battle_bg_prop_data.inc's row.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n",
           _FIXTURE_STRUCT,
           "\ninline constexpr std::array<ExpectedBattleBackgroundEntry, {}>\n"
           "    kExpectedBattleBackgroundEntries = {{{{  // ROM BattleBGProp\n"
           .format(len(records))]
    for rec in records:
        b = rec.bytes
        out.append(
            "    {{ .id = {:>2},  // ${:02X} {}\n"
            "      .record = {{ .graphics1 = 0x{:02X}, .graphics2 = 0x{:02X}, "
            ".graphics3 = 0x{:02X},\n"
            "                  .tilemap1 = 0x{:02X}, .tilemap2 = 0x{:02X}, "
            ".palette = 0x{:02X} }} }},\n".format(
                rec.index, rec.index, rec.name, b[0], b[1], b[2], b[3], b[4], b[5]))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver --------------------------------------------------------------------

def run(source_root, asm_path, inc_path, outputs, check_only=False):
    symbols = _Symbols(inc_path)
    records = parse_battle_bg_prop(asm_path, symbols)
    assert_rom(common.load_vanilla_rom(source_root), records, asm_path)

    flagged_gfx = sum(1 for r in records if r.fields[0].flag)
    flagged_pal = sum(1 for r in records if r.fields[5].flag)
    literals = [(r, i) for r in records
                for i, f in enumerate(r.fields) if f.from_literal]
    if check_only:
        print("OK: {} records x {} bytes, cartridge-identical at ${:06X}; "
              "{} double-width graphics, {} wavy palettes.".format(
                  len(records), RECORD_BYTES, BATTLE_BG_PROP_SNES,
                  flagged_gfx, flagged_pal))
        for rec, pos in literals:
            print("  note: record {} ({}) writes {} as a bare literal -> "
                  "{}::{}".format(rec.index, rec.name, _FIELD_SCOPES[pos][0],
                                  _FIELD_SCOPES[pos][1], rec.fields[pos].member))
        return 0

    _write(outputs["gfx_enum"], render_enum_h(
        "BattleBackgroundGraphicsId", "BATTLE_BG_GFX",
        symbols.members("BATTLE_BG_GFX"), _GFX_BLURB))
    _write(outputs["tiles_enum"], render_enum_h(
        "BattleBackgroundTilemapId", "BATTLE_BG_TILES",
        symbols.members("BATTLE_BG_TILES"), _TILES_BLURB))
    _write(outputs["pal_enum"], render_enum_h(
        "BattleBackgroundPaletteId", "BATTLE_BG_PAL",
        symbols.members("BATTLE_BG_PAL"), _PAL_BLURB))
    _write(outputs["inc"], render_inc(records))
    _write(outputs["fixture"], render_fixture(records))
    print("Emitted {} records -> {}".format(len(records), outputs["inc"]))
    print("Emitted fixture -> {}".format(outputs["fixture"]))
    print("Emitted 3 asset-id enums ({} / {} / {} names)".format(
        len(symbols.members("BATTLE_BG_GFX")),
        len(symbols.members("BATTLE_BG_TILES")),
        len(symbols.members("BATTLE_BG_PAL"))))
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
                    help="disassembly root (battle_bg.asm + battle_bg.inc under it)")
    ap.add_argument("--inc-out", default="src/data/generated/battle_bg_prop_data.inc")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/battle_bg_prop_expected.h")
    ap.add_argument("--gfx-enum-out",
                    default="include/ostinato/battle_background_graphics_id.h")
    ap.add_argument("--tiles-enum-out",
                    default="include/ostinato/battle_background_tilemap_id.h")
    ap.add_argument("--pal-enum-out",
                    default="include/ostinato/battle_background_palette_id.h")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    asm_path = os.path.join(args.source_root, "src", "gfx", "battle_bg.asm")
    inc_path = os.path.join(args.source_root, "include", "gfx", "battle_bg.inc")
    outputs = {"inc": args.inc_out, "fixture": args.fixture_out,
               "gfx_enum": args.gfx_enum_out, "tiles_enum": args.tiles_enum_out,
               "pal_enum": args.pal_enum_out}
    try:
        return run(args.source_root, asm_path, inc_path, outputs,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
