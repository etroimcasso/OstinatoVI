#!/usr/bin/env python3
"""Emit the colosseum-wager table from original-src colosseum.asm.

Port-time tooling (NOT a build/CI dependency). ColosseumProp
(src/menu/colosseum.asm:1212, ROM DF/B600) is 256 make_colosseum_prop rows of
4 bytes each — for each wagerable item: the monster fought, a dead $40 byte,
the prize item, and the hide-prize flag (the macro at
colosseum.asm:1189-1204; sole reader LoadColosseumProp,
colosseum.asm:833-846). The table is committed source (not a rip product), so
this parser walks the .asm grammar directly and emits:

  * src/data/generated/colosseum_prop_data.inc — one designated-initializer
    ColosseumWagerEntry row per record (256 records), every field labeled
    inline; the kColosseumWagers array #includes it.
  * tests/fixtures/colosseum_prop_expected.h — the same 256 records as raw
    4-byte rows (the ground-truth byte contract) for a full-corpus
    byte-equivalence test.

Monster and item names resolve against original-src/include/const.inc's
MONSTER and ITEM enums. Blank-argument rows take the macro's .ifblank
defaults (MONSTER::CHUPON_COLOSSEUM / ITEM::ELIXIR); a nonblank third
argument emits the $ff hide flag.

Structural guarantees, hard-errored at emit time:
  * the table holds exactly 256 rows, terminated by .popseg — any other line
    inside the table is a grammar deviation;
  * every row has 0, 2, or 3 arguments (the corpus' three shapes), and a
    third argument is the literal 1;
  * every monster resolves in MONSTER and its index fits one byte (the
    macro's .byte would truncate a wider index — ca65 errors instead, so a
    wider value here means the grammar was misread);
  * every prize resolves in ITEM;
  * every row's trailing comment names the wagered item — asserted equal to
    the ITEM name at the row's index, so the upstream author's own row labels
    become a structural check that no row was dropped or duplicated.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_colosseum.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_colosseum.py --colosseum-asm PATH --const-inc PATH \\
                       --inc-out FILE --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import sys

import common
import parse_const_enums as pce
from common import ParseError

RECORD_COUNT = 256
RECORD_SIZE = 4

UNUSED_BYTE = 0x40    # dead record byte +1 (no consumer in the tree)
HIDE_PRIZE = 0xFF
SHOW_PRIZE = 0x00

DEFAULT_MONSTER = "CHUPON_COLOSSEUM"   # macro .ifblank default
DEFAULT_PRIZE = "ELIXIR"

_MACRO = "make_colosseum_prop"


class ColosseumError(Exception):
    pass


# --- symbol resolution -------------------------------------------------------

class Symbols(object):
    """The const.inc MONSTER and ITEM enums the rows resolve against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        for enum_name in ("MONSTER", "ITEM"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found"
                                 .format(enum_name))
        self.monster_values = {m.name: m.value
                               for m in self.parsed.enum("MONSTER").members}
        self.item_values = {m.name: m.value
                            for m in self.parsed.enum("ITEM").members}
        # First declaration wins for aliased values.
        self.item_names = {}
        for m in self.parsed.enum("ITEM").members:
            self.item_names.setdefault(m.value, m.name)


# --- row parsing -------------------------------------------------------------

class Record(object):
    """One parsed table row: resolved names + the raw record bytes."""

    def __init__(self, index, monster_name, prize_name, hidden, symbols,
                 path, lineno):
        self.index = index
        self.monster_name = monster_name
        self.prize_name = prize_name
        self.hidden = hidden
        monster_value = symbols.monster_values.get(monster_name)
        if monster_value is None:
            raise ParseError(path, lineno,
                             "unknown MONSTER '{}'".format(monster_name))
        if monster_value > 0xFF:
            raise ColosseumError(
                "{}:{}: wagered monster {} has index {:#06x} — the record's "
                "one-byte monster field cannot hold it; escalate, never "
                "guess".format(path, lineno, monster_name, monster_value))
        prize_value = symbols.item_values.get(prize_name)
        if prize_value is None:
            raise ParseError(path, lineno,
                             "unknown ITEM '{}'".format(prize_name))
        self.raw = [monster_value, UNUSED_BYTE, prize_value,
                    HIDE_PRIZE if hidden else SHOW_PRIZE]


def _parse_row(code, comment, index, symbols, path, lineno):
    """Parse one make_colosseum_prop line into a Record."""
    arg_text = code[len(_MACRO):].strip()
    args = ([a.strip() for a in arg_text.split(",")] if arg_text else [])
    if len(args) not in (0, 2, 3):
        raise ParseError(path, lineno,
                         "row has {} argument(s); the corpus uses 0, 2, or 3"
                         .format(len(args)))
    if len(args) == 3 and args[2] != "1":
        raise ParseError(path, lineno,
                         "third argument '{}' — the corpus' hide-prize rows "
                         "all pass the literal 1".format(args[2]))
    if args:
        monster_name, prize_name = args[0], args[1]
    else:
        monster_name, prize_name = DEFAULT_MONSTER, DEFAULT_PRIZE

    # The trailing comment names the wagered item; assert it matches the ITEM
    # name at this row's index so a dropped or duplicated row surfaces here.
    expected = symbols.item_names.get(index)
    if comment is None or comment.strip() != expected:
        raise ParseError(path, lineno,
                         "row {} comment '{}' does not name the wagered item "
                         "'{}'".format(index, comment, expected))

    return Record(index, monster_name, prize_name, len(args) == 3, symbols,
                  path, lineno)


def read_records(asm_path, symbols):
    """Walk colosseum.asm: anchor the ColosseumProp label, parse exactly 256
    rows, and require the .popseg terminator."""
    with open(asm_path, "r", encoding="utf-8") as fh:
        raw_lines = fh.readlines()

    records = []
    in_table = False
    terminated = False
    for idx, raw in enumerate(raw_lines):
        lineno = idx + 1
        code, comment = common.strip_comment(raw)
        stripped = code.strip()
        if not in_table:
            if stripped == "ColosseumProp:":
                in_table = True
            continue
        if not stripped:
            continue
        if stripped == ".popseg":
            terminated = True
            break
        if not stripped.startswith(_MACRO):
            raise ParseError(asm_path, lineno,
                             "unexpected line inside ColosseumProp: '{}'"
                             .format(stripped))
        records.append(_parse_row(stripped, comment, len(records), symbols,
                                  asm_path, lineno))

    if not in_table:
        raise ParseError(asm_path, len(raw_lines),
                         "ColosseumProp label not found")
    if not terminated:
        raise ParseError(asm_path, len(raw_lines),
                         "ColosseumProp table not terminated by .popseg")
    if len(records) != RECORD_COUNT:
        raise ColosseumError(
            "{}: {} rows, expected {} — wrong table shape"
            .format(asm_path, len(records), RECORD_COUNT))
    return records


# --- rendering ---------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_colosseum.py\n"
    "// Source: src/menu/colosseum.asm (ColosseumProp, ROM DF/B600,\n"
    "//         256 make_colosseum_prop rows x 4 bytes; macro at\n"
    "//         colosseum.asm:1189-1204, sole reader LoadColosseumProp at\n"
    "//         colosseum.asm:833-846)\n"
    "// Source: include/const.inc (MONSTER / ITEM values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_colosseum.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/colosseum_prop_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/colosseum_prop_expected.h\n"
    "\n"
)


def _render_row(rec):
    return (
        "    ColosseumWagerEntry{{  // [${:02X}]\n"
        "        .id = ItemId::{},\n"
        "        .record = ColosseumWager{{\n"
        "            .monster       = wagerMonster(MonsterId::{}),\n"
        "            .unused40      = kColosseumUnusedByte,\n"
        "            .prize         = ItemId::{},\n"
        "            .hidePrizeFlag = {},\n"
        "        }},\n"
        "    }},\n"
    ).format(rec.index,
             # The wagered item IS the row's index — resolved to its name by
             # the comment-anchor assert in _parse_row.
             rec.wagered_name,
             rec.monster_name,
             rec.prize_name,
             "kHidePrize" if rec.hidden else "kShowPrize")


def render_inc(records):
    lines = [_HEADER_COMMON,
             "// ColosseumWagerEntry rows in ITEM index order ($00..$FF), one\n"
             "// designated-initializer row per record, #included inside the\n"
             "// kColosseumWagers array in src/data/colosseum_wagers.cpp.\n"
             "// Each row's identity is its .id field — the wagered item's\n"
             "// ItemId enumerator; a compile-time assert verifies\n"
             "// id == position. The packed .record stays byte-identical to\n"
             "// the 4 ROM bytes. Every value renders through a named\n"
             "// surface: the monster through wagerMonster(MonsterId::...),\n"
             "// the dead +1 byte as kColosseumUnusedByte, the prize as its\n"
             "// ItemId enumerator, and the hide flag as\n"
             "// kHidePrize/kShowPrize.\n\n"]
    for rec in records:
        lines.append(_render_row(rec))
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 4-byte ColosseumProp record; field names mirror the\n"
    "// src/data/colosseum_wagers.h record layout. Values are the exact ROM\n"
    "// bytes — deliberately independent of the typed-surface rows in\n"
    "// colosseum_prop_data.inc, so resolution drift in either artifact fails\n"
    "// the full-corpus byte-equivalence test.\n"
    "struct ExpectedColosseumRecord {\n"
    "    std::uint8_t monster;\n"
    "    std::uint8_t unused40;\n"
    "    std::uint8_t prize;\n"
    "    std::uint8_t hidePrizeFlag;\n"
    "};\n"
    "static_assert(sizeof(ExpectedColosseumRecord) == 4,\n"
    "              \"fixture record must stay byte-identical to a ROM"
    " ColosseumProp record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (raw decimal\n"
    "// index — the fixture stays independent of the port's ItemId header)\n"
    "// alongside the raw record bytes. Mirrors\n"
    "// ostinato::ColosseumWagerEntry without depending on it.\n"
    "struct ExpectedColosseumEntry {\n"
    "    std::uint8_t id;\n"
    "    ExpectedColosseumRecord record;\n"
    "};\n"
)


def _fixture_row(rec):
    h = ["0x{:02X}".format(b) for b in rec.raw]
    return (
        "    {{ .id = {:>3},  // ${:02X} {}\n"
        "      .record = {{ .monster = {}, .unused40 = {}, .prize = {},\n"
        "                  .hidePrizeFlag = {} }} }},\n"
    ).format(rec.index, rec.index, rec.wagered_name, *h)


def render_fixture(records):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_colosseum_wagers.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture id == position, table id enumerator ==\n"
             "// position, and a 4-byte memcmp of the packed record against\n"
             "// src/data/generated/colosseum_prop_data.inc's row.\n"
             "\n"
             "#pragma once\n"
             "\n"
             "#include <array>\n"
             "#include <cstdint>\n"
             "\n"
             "namespace ostinato::test {\n"
             "\n",
             _FIXTURE_STRUCT,
             "\n",
             "inline constexpr std::array<ExpectedColosseumEntry, {}> "
             "kExpectedColosseumEntries = {{{{  // ROM ColosseumProp\n"
             .format(len(records))]
    for rec in records:
        lines.append(_fixture_row(rec))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ------------------------------------------------------------------

def run(colosseum_asm, const_inc, inc_out, fixture_out, check_only=False):
    symbols = Symbols(const_inc)
    records = read_records(colosseum_asm, symbols)
    # Stamp each record with its wagered item's name (identity = row index).
    for rec in records:
        rec.wagered_name = symbols.item_names[rec.index]

    if check_only:
        print("OK: {} rows x {} bytes; every symbol resolved."
              .format(len(records), RECORD_SIZE))
        return 0

    _write(inc_out, render_inc(records))
    _write(fixture_out, render_fixture(records))
    print("Emitted {} records -> {}".format(len(records), inc_out))
    print("Emitted fixture -> {}".format(fixture_out))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def _resolve(args):
    colosseum_asm = args.colosseum_asm
    const_inc = args.const_inc
    if args.source_root:
        if not colosseum_asm:
            colosseum_asm = os.path.join(args.source_root, "src", "menu",
                                         "colosseum.asm")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return colosseum_asm, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (colosseum.asm + const.inc "
                         "resolved under it)")
    ap.add_argument("--colosseum-asm", help="path to colosseum.asm")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out",
                    default="src/data/generated/colosseum_prop_data.inc",
                    help="output path for the ColosseumWagerEntry rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/colosseum_prop_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    colosseum_asm, const_inc = _resolve(args)
    if not colosseum_asm or not const_inc:
        ap.error("provide --source-root, or both --colosseum-asm and "
                 "--const-inc")
    try:
        return run(colosseum_asm, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except (ParseError, ColosseumError) as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
