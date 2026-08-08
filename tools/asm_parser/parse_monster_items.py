#!/usr/bin/env python3
"""Emit the monster steal/drop item table from original-src monster_items.asm.

Port-time tooling (NOT a build/CI dependency): interprets the ca65 macro DSL in
src/battle/monster_items.asm (384 records x 4 bytes, ROM CF/3000; the
MonsterItems label lives at the include site, battle_main.asm:16468-16469) and
emits:

  * src/data/generated/monster_items_data.inc — one designated-initializer
    MonsterItemsEntry row per monster (384 records), identity as the
    MonsterId enumerator field; the kMonsterItems array #includes it.
  * tests/fixtures/monster_items_expected.h — the same 384 records as raw
    4-byte rows (the ground-truth byte contract) for a full-corpus
    byte-equivalence test.

The DSL: one `monster_steal rare, common` line immediately followed by one
`monster_drop rare, common` line per monster, each argument a known ITEM
member (`monster_drop` is a .define alias of `monster_steal`, so the two
lines emit the same 2-byte shape). The macro *body* is asserted, not skipped:
its single `.byte ITEM::rare_item, ITEM::common_item` line is the byte-order
authority (rare steal, common steal, rare drop, common drop —
monster_items.asm:3-12). The trailing `.delmac`/`.undef` teardown lines are
recognized; any other line is a hard error.

Symbol values resolve against original-src/include/const.inc (ITEM for the
record bytes, MONSTER for the identity names); an unknown symbol is a hard
error with a file:line citation — a deviation surfaces loudly, never as a
guessed byte.

Structural guarantees, hard-errored at emit time:
  * the macro body matches the documented `.byte ITEM::rare_item,
    ITEM::common_item` shape;
  * steal/drop lines strictly alternate, steal first, drop completing each
    record;
  * the record count equals the MONSTER index space (384), whose values must
    be contiguous from 0 so record index == MonsterId value.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_monster_items.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_monster_items.py --monster-items-asm PATH --const-inc PATH \\
                           --inc-out FILE --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import sys

import common
import parse_const_enums as pce
from common import ParseError, strip_comment

RECORD_COUNT = 384

# The macro body is the byte-order authority (rare steal, common steal / rare
# drop, common drop). Whitespace-normalized; a repin that changes it must
# hard-error, never silently re-shape the records.
_EXPECTED_MACRO_BODY = ("ITEM::rare_item, ITEM::common_item",)


class Record(object):
    """One monster: steal pair + drop pair, as ITEM member names and bytes."""

    def __init__(self, index, steal_names, steal_bytes):
        self.index = index
        self.steal_names = steal_names   # [rare, common] ITEM member names
        self.steal_bytes = steal_bytes   # [int, int]
        self.drop_names = None           # [rare, common], filled by the drop line
        self.drop_bytes = None
        self.name = None                 # MONSTER member name, filled by index

    @property
    def names(self):
        return self.steal_names + self.drop_names

    @property
    def bytes(self):
        return self.steal_bytes + self.drop_bytes


class Symbols(object):
    """The const.inc enums the DSL resolves against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        for enum_name in ("ITEM", "MONSTER"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(enum_name))

    def item_value(self, member, path, lineno):
        val = self.parsed.enum("ITEM").value_of(member)
        if val is None:
            raise ParseError(path, lineno,
                             "unknown ITEM::{}".format(member))
        return val


def _split_pair_args(s, macro_name, path, lineno):
    parts = s.split(None, 1)
    args = [a.strip() for a in parts[1].split(",")] if len(parts) > 1 else []
    if len(args) != 2 or any(not a for a in args):
        raise ParseError(path, lineno,
                         "{} expects 2 args, got {}".format(macro_name,
                                                            len(args)))
    return args


def parse_monster_items(asm_path, symbols):
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    records = []
    macro_body = []
    macro_depth = 0
    define_seen = False
    teardown_seen = False
    pending = None  # the Record awaiting its drop line

    for idx, raw in enumerate(lines):
        lineno = idx + 1
        code, _comment = strip_comment(raw)
        if not code:
            continue
        s = code.strip()
        low = s.lower()

        # --- the macro definition: capture the body (byte-order authority) ---
        if low.startswith(".endmac"):
            if macro_depth == 0:
                raise ParseError(asm_path, lineno, ".endmac without .mac")
            macro_depth -= 1
            continue
        if low.startswith(".mac"):
            macro_depth += 1
            continue
        if macro_depth > 0:
            if not low.startswith(".byte"):
                raise ParseError(asm_path, lineno,
                                 "unexpected macro-body line: '{}'".format(s))
            macro_body.append(" ".join(s.split(None, 1)[1].split()))
            continue

        # --- the drop alias + teardown directives ---
        if low.startswith(".define"):
            if " ".join(s.split()) != ".define monster_drop monster_steal":
                raise ParseError(asm_path, lineno,
                                 "unexpected .define: '{}'".format(s))
            define_seen = True
            continue
        if low.startswith(".delmac") or low.startswith(".undef"):
            teardown_seen = True
            continue

        # --- record lines: strictly alternating steal, then drop ---
        head = s.split(None, 1)[0]
        if head == "monster_steal":
            if teardown_seen:
                raise ParseError(asm_path, lineno,
                                 "record line after the .delmac teardown")
            if pending is not None:
                raise ParseError(asm_path, lineno,
                                 "monster_steal while the previous record "
                                 "still awaits its monster_drop line")
            args = _split_pair_args(s, "monster_steal", asm_path, lineno)
            pending = Record(len(records),
                             args,
                             [symbols.item_value(a, asm_path, lineno)
                              for a in args])
            continue
        if head == "monster_drop":
            if not define_seen:
                raise ParseError(asm_path, lineno,
                                 "monster_drop before its .define alias")
            if pending is None:
                raise ParseError(asm_path, lineno,
                                 "monster_drop without a preceding "
                                 "monster_steal line")
            args = _split_pair_args(s, "monster_drop", asm_path, lineno)
            pending.drop_names = args
            pending.drop_bytes = [symbols.item_value(a, asm_path, lineno)
                                  for a in args]
            records.append(pending)
            pending = None
            continue

        raise ParseError(asm_path, lineno,
                         "unrecognized line: '{}' (grammar not covered — "
                         "escalate, never guess)".format(s))

    if pending is not None:
        raise ParseError(asm_path, len(lines),
                         "last record's monster_drop line is missing")
    if tuple(macro_body) != _EXPECTED_MACRO_BODY:
        raise ParseError(asm_path, 0,
                         "monster_steal macro body {} does not match the "
                         "documented byte-order shape {}"
                         .format(macro_body, list(_EXPECTED_MACRO_BODY)))
    _assign_names_and_verify(records, symbols, asm_path)
    return records


def _assign_names_and_verify(records, symbols, path):
    if len(records) != RECORD_COUNT:
        raise ParseError(path, 0,
                         "monster_items produced {} records; expected {}"
                         .format(len(records), RECORD_COUNT))
    monster = symbols.parsed.enum("MONSTER")
    names_by_value = {}
    for m in monster.members:
        names_by_value.setdefault(m.value, m.name)
    if max(names_by_value) != RECORD_COUNT - 1:
        raise ParseError(path, 0,
                         "MONSTER max id {} != {} (record count mismatch)"
                         .format(max(names_by_value), RECORD_COUNT - 1))
    for rec in records:
        rec.name = names_by_value[rec.index]
        for b in rec.bytes:
            if not (0 <= b <= 0xFF):
                raise ParseError(path, 0,
                                 "byte value {} out of range 0..255".format(b))


# --- rendering -------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_monster_items.py\n"
    "// Source: src/battle/monster_items.asm (ROM CF/3000, 384 records x\n"
    "//         4 bytes: rare steal, common steal, rare drop, common drop —\n"
    "//         the monster_steal/monster_drop macro pairs; the MonsterItems\n"
    "//         label lives at the include site, battle_main.asm:16468-16469)\n"
    "// Source: include/const.inc (ITEM / MONSTER values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_monster_items.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/monster_items_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/monster_items_expected.h\n"
    "\n"
)


def render_inc(records):
    lines = [_HEADER_COMMON,
             "// MonsterItemsEntry rows in MONSTER index order ($000..$17F),\n"
             "// one designated-initializer row per monster, #included inside\n"
             "// the kMonsterItems array in src/data/monster_items.cpp. Each\n"
             "// row's identity is its .id field — the MonsterId enumerator;\n"
             "// a compile-time assert verifies id == position. The packed\n"
             "// .record stays byte-identical to the 4 ROM bytes; every slot\n"
             "// is an ItemId enumerator (ItemId::EMPTY for an empty slot).\n\n"]
    for rec in records:
        lines.append(
            "    MonsterItemsEntry{{  // [${:03X}]\n"
            "        .id = MonsterId::{},\n"
            "        .record = MonsterItems{{\n"
            "            .rareSteal   = ItemId::{},\n"
            "            .commonSteal = ItemId::{},\n"
            "            .rareDrop    = ItemId::{},\n"
            "            .commonDrop  = ItemId::{},\n"
            "        }},\n"
            "    }},\n".format(rec.index, rec.name, *rec.names))
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 4-byte monster_items record: rare steal, common steal, rare\n"
    "// drop, common drop. Values are the exact ROM bytes with every upstream\n"
    "// symbol resolved — deliberately independent of the enum-symbol rows in\n"
    "// monster_items_data.inc, so symbol/value drift in either artifact\n"
    "// fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedMonsterItemsRecord {\n"
    "    std::uint8_t rareSteal, commonSteal, rareDrop, commonDrop;\n"
    "};\n"
    "static_assert(sizeof(ExpectedMonsterItemsRecord) == 4,\n"
    "              \"fixture record must stay byte-identical to a ROM "
    "monster_items record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (raw decimal\n"
    "// index — the fixture stays independent of the port's MonsterId header)\n"
    "// alongside the raw record bytes. Mirrors ostinato::MonsterItemsEntry\n"
    "// without depending on it.\n"
    "struct ExpectedMonsterItemsEntry {\n"
    "    std::uint16_t id;\n"
    "    ExpectedMonsterItemsRecord record;\n"
    "};\n"
)


def render_fixture(records):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_monster_tables.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture id == position, table id enumerator ==\n"
             "// position, and a 4-byte memcmp of the packed record against\n"
             "// src/data/generated/monster_items_data.inc's row.\n"
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
             "inline constexpr std::array<ExpectedMonsterItemsEntry, {}> "
             "kExpectedMonsterItemsEntries = {{{{  // ROM MonsterItems\n"
             .format(len(records))]
    for rec in records:
        h = ["0x{:02X}".format(b) for b in rec.bytes]
        lines.append(
            "    {{ .id = {:>3},  // ${:03X} {}\n"
            "      .record = {{ .rareSteal = {}, .commonSteal = {},"
            " .rareDrop = {}, .commonDrop = {} }} }},\n".format(
                rec.index, rec.index, rec.name, *h))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ----------------------------------------------------------------

def run(monster_items_asm, const_inc, inc_out, fixture_out, check_only=False):
    symbols = Symbols(const_inc)
    records = parse_monster_items(monster_items_asm, symbols)

    if check_only:
        print("OK: {} monster_items records; steal/drop pairs complete, all "
              "bytes resolved.".format(len(records)))
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
    items_asm = args.monster_items_asm
    const_inc = args.const_inc
    if args.source_root:
        if not items_asm:
            items_asm = os.path.join(args.source_root, "src", "battle",
                                     "monster_items.asm")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return items_asm, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (monster_items.asm + const.inc "
                         "resolved under it)")
    ap.add_argument("--monster-items-asm", help="path to monster_items.asm")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out",
                    default="src/data/generated/monster_items_data.inc",
                    help="output path for the MonsterItemsEntry rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/monster_items_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    items_asm, const_inc = _resolve(args)
    if not items_asm or not const_inc:
        ap.error("provide --source-root, or both --monster-items-asm and "
                 "--const-inc")
    try:
        return run(items_asm, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
