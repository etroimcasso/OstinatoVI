#!/usr/bin/env python3
"""Emit the metamorph pack + rate tables from original-src.

Port-time tooling (NOT a build/CI dependency). Two source artifacts feed the
metamorph surface:

  * src/battle/metamorph_prop.dat — the raw 32-pack x 4-byte item-pack table
    (MetamorphProp, ROM C4/7F40; incbin at battle_main.asm:9424). Each pack is
    four ITEM bytes; the metamorph effect indexes pack*4 + a 2-bit random
    offset (TargetEffect_12, battle_main.asm:9385-9409).
  * The MetamorphRateTbl row inside src/battle/battle_main.asm (:10008-10009,
    ROM c2/3dc5) — the eight probability bytes the same effect compares a
    random byte against, indexed by the metamorph byte's high 3 bits.

This script reads both, resolves pack bytes to ITEM names against
original-src/include/const.inc, and emits:

  * src/data/generated/metamorph_prop_data.inc — one designated-initializer
    MetamorphPackEntry row per pack (32 packs), items as ItemId enumerators.
  * src/data/generated/metamorph_rate_data.inc — one MetamorphRateEntry row
    per probability byte (8 rows, raw hex values — the byte is the
    observable).
  * tests/fixtures/metamorph_expected.h — both tables as raw bytes (the
    ground-truth byte contract) for full-corpus tests.

Structural guarantees, hard-errored at emit time:
  * the .dat is exactly 128 bytes (32 packs x 4 — any other length is the
    wrong artifact);
  * every pack byte has an ITEM name;
  * the MetamorphRateTbl label exists in battle_main.asm exactly once and is
    followed by a single .byte row of exactly 8 values (a moved or reshaped
    table is an escalation, not a guess).

Python 3 standard library only; targets 3.9+.

Usage:
    parse_metamorph_prop.py --source-root PATH --pack-inc-out FILE \\
                            --rate-inc-out FILE --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_const_enums as pce
from common import ParseError

PACK_COUNT = 32
PACK_SIZE = 4
EXPECTED_LEN = PACK_COUNT * PACK_SIZE
RATE_COUNT = 8

_RATE_LABEL = "MetamorphRateTbl"

# Metamorph rate-row names (mirror include/ostinato/metamorph_info.h; the
# odds ladder is battle-ram.txt:963's).
_METAMORPH_RATE_NAMES = (
    "ODDS_255_256",  # rate 0
    "ODDS_3_4",      # rate 1
    "ODDS_1_2",      # rate 2
    "ODDS_1_4",      # rate 3
    "ODDS_1_8",      # rate 4
    "ODDS_1_16",     # rate 5
    "ODDS_1_32",     # rate 6
    "NEVER",         # rate 7
)


# --- symbol resolution -------------------------------------------------------

class Symbols(object):
    """The const.inc ITEM enum the pack bytes resolve against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        if self.parsed.enum("ITEM") is None:
            raise ParseError(const_inc, 0, "expected enum 'ITEM' not found")
        # First declaration wins (trailing aliases never shadow the primary).
        self.item_names = {}
        for m in self.parsed.enum("ITEM").members:
            self.item_names.setdefault(m.value, m.name)


# --- reading -----------------------------------------------------------------

class MetamorphError(Exception):
    pass


class Pack(object):
    """One 4-item metamorph pack: raw bytes + resolved ITEM names."""

    def __init__(self, index, raw, symbols):
        assert len(raw) == PACK_SIZE
        self.index = index
        self.raw = list(raw)
        self.item_names = []
        for byte in raw:
            name = symbols.item_names.get(byte)
            if name is None:
                raise MetamorphError(
                    "pack {}: item byte {:#04x} has no ITEM name"
                    .format(index, byte))
            self.item_names.append(name)


def read_packs(dat_path, symbols):
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != EXPECTED_LEN:
        raise MetamorphError(
            "{}: {} bytes, expected {} (32 packs x 4 — wrong artifact)"
            .format(dat_path, len(data), EXPECTED_LEN))
    return [Pack(i, data[i * PACK_SIZE:(i + 1) * PACK_SIZE], symbols)
            for i in range(PACK_COUNT)]


# The .byte row that must follow the label: an optional @local: label, the
# .byte directive, then comma-separated ca65 integer literals.
_RE_BYTE_ROW = re.compile(
    r"^(?:@[A-Za-z0-9_]+:)?\s*\.byte\s+(.+)$")


def read_rates(asm_path):
    """Extract the 8 MetamorphRateTbl bytes, anchored on the label text.

    The label must appear exactly once; its row must be a single .byte line of
    exactly 8 literals. Anything else — label gone, table moved, row count
    changed — is a hard error for escalation.
    """
    with open(asm_path, "r", encoding="utf-8") as fh:
        raw_lines = fh.readlines()

    label_linenos = []
    for idx, raw in enumerate(raw_lines):
        code, _comment = common.strip_comment(raw)
        if code.strip() == _RATE_LABEL + ":":
            label_linenos.append(idx)
    if len(label_linenos) != 1:
        raise MetamorphError(
            "{}: label '{}:' found {} times, expected exactly once"
            .format(asm_path, _RATE_LABEL, len(label_linenos)))

    # The first non-blank code line after the label must be the .byte row.
    for idx in range(label_linenos[0] + 1, len(raw_lines)):
        code, _comment = common.strip_comment(raw_lines[idx])
        stripped = code.strip()
        if not stripped:
            continue
        m = _RE_BYTE_ROW.match(stripped)
        if not m:
            raise MetamorphError(
                "{}:{}: expected the {} .byte row, found '{}'"
                .format(asm_path, idx + 1, _RATE_LABEL, stripped))
        values = []
        for token in m.group(1).split(","):
            value = common.parse_int_literal(token.strip())
            if value is None:
                raise MetamorphError(
                    "{}:{}: malformed byte literal '{}'"
                    .format(asm_path, idx + 1, token.strip()))
            values.append(value)
        if len(values) != RATE_COUNT:
            raise MetamorphError(
                "{}:{}: {} rate bytes, expected {}"
                .format(asm_path, idx + 1, len(values), RATE_COUNT))
        return values

    raise MetamorphError(
        "{}: no code line follows the {} label".format(asm_path, _RATE_LABEL))


# --- rendering ---------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_metamorph_prop.py\n"
    "// Source: src/battle/metamorph_prop.dat (MetamorphProp, ROM C4/7F40,\n"
    "//         32 packs x 4 bytes; consumed by the metamorph effect\n"
    "//         TargetEffect_12 in src/battle/battle_main.asm:9385-9409)\n"
    "// Source: src/battle/battle_main.asm MetamorphRateTbl (:10008-10009,\n"
    "//         ROM c2/3dc5 — the eight probability bytes)\n"
    "// Source: include/const.inc (ITEM values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_metamorph_prop.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --pack-inc-out src/data/generated/metamorph_prop_data.inc \\\n"
    "//       --rate-inc-out src/data/generated/metamorph_rate_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/metamorph_expected.h\n"
    "\n"
)


def _render_pack_row(pack):
    return (
        "    MetamorphPackEntry{{  // [{}]\n"
        "        .index = {},\n"
        "        .record = MetamorphPack{{\n"
        "            .items = {{ ItemId::{}, ItemId::{},\n"
        "                       ItemId::{}, ItemId::{} }},\n"
        "        }},\n"
        "    }},\n"
    ).format(pack.index, pack.index, *pack.item_names)


def render_pack_inc(packs):
    lines = [_HEADER_COMMON,
             "// 32 MetamorphPackEntry rows in pack-index order. No upstream\n"
             "// index enum exists for packs, so the row's identity (.index,\n"
             "// decimal 0..31) is a typed field — a compile-time assert in\n"
             "// src/data/metamorph.h verifies index == position. Each pack's\n"
             "// four items render as ItemId enumerators; picking one of the\n"
             "// four at random is the metamorph effect's job, not the\n"
             "// table's. Included inside the kMetamorphPacks array in\n"
             "// src/data/metamorph.h.\n\n"]
    for pack in packs:
        lines.append(_render_pack_row(pack))
    return "".join(lines)


def render_rate_inc(rates):
    lines = [_HEADER_COMMON,
             "// 8 MetamorphRateEntry rows in rate-index order. The row's\n"
             "// identity is its .id field — the MetamorphRate enumerator\n"
             "// (ostinato/metamorph_info.h names the documented odds\n"
             "// ladder); a compile-time assert in src/data/metamorph.h\n"
             "// verifies id == position. .value is the raw ROM probability\n"
             "// byte (hex; the byte value IS the observable — the effect\n"
             "// passes when a random byte compares below it). Included\n"
             "// inside the kMetamorphRates array in src/data/metamorph.h.\n\n"]
    for index, value in enumerate(rates):
        lines.append("    {{ .id = MetamorphRate::{}, .value = 0x{:02X} }},\n"
                     .format(_METAMORPH_RATE_NAMES[index], value))
    return "".join(lines)


_FIXTURE_STRUCTS = (
    "// One raw 4-byte metamorph pack; values are the exact ROM bytes —\n"
    "// deliberately independent of the typed-surface rows in\n"
    "// metamorph_prop_data.inc, so symbol/value drift in either artifact\n"
    "// fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedMetamorphPack {\n"
    "    std::uint8_t item0, item1, item2, item3;\n"
    "};\n"
    "static_assert(sizeof(ExpectedMetamorphPack) == 4,\n"
    "              \"fixture pack must stay byte-identical to a ROM metamorph"
    " pack\");\n"
    "\n"
    "// One fixture entry per table row: identity as a typed field (raw\n"
    "// decimal index) alongside the raw bytes. Mirror\n"
    "// ostinato::MetamorphPackEntry / MetamorphRateEntry without depending\n"
    "// on them.\n"
    "struct ExpectedMetamorphPackEntry {\n"
    "    std::uint8_t index;\n"
    "    ExpectedMetamorphPack record;\n"
    "};\n"
    "\n"
    "struct ExpectedMetamorphRateEntry {\n"
    "    std::uint8_t index;\n"
    "    std::uint8_t value;\n"
    "};\n"
)


def render_fixture(packs, rates):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_monster_properties.cpp — the\n"
             "// ground-truth bytes for both metamorph tables. The\n"
             "// full-corpus tests assert, per entry: fixture index ==\n"
             "// position, table index field == position, and byte equality\n"
             "// of the pack/rate data against the generated rows.\n"
             "\n"
             "#pragma once\n"
             "\n"
             "#include <array>\n"
             "#include <cstdint>\n"
             "\n"
             "namespace ostinato::test {\n"
             "\n",
             _FIXTURE_STRUCTS,
             "\n",
             "inline constexpr std::array<ExpectedMetamorphPackEntry, {}> "
             "kExpectedMetamorphPacks = {{{{  // ROM MetamorphProp\n"
             .format(len(packs))]
    for pack in packs:
        h = ["0x{:02X}".format(b) for b in pack.raw]
        lines.append(
            "    {{ .index = {:>2},\n"
            "      .record = {{ .item0 = {}, .item1 = {}, .item2 = {},"
            " .item3 = {} }} }},\n".format(pack.index, *h))
    lines.append("}};\n\n")
    lines.append(
        "inline constexpr std::array<ExpectedMetamorphRateEntry, {}> "
        "kExpectedMetamorphRates = {{{{  // MetamorphRateTbl\n"
        .format(len(rates)))
    for index, value in enumerate(rates):
        lines.append("    {{ .index = {}, .value = 0x{:02X} }},\n"
                     .format(index, value))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ------------------------------------------------------------------

def run(metamorph_dat, battle_main_asm, const_inc, pack_inc_out, rate_inc_out,
        fixture_out, check_only=False):
    symbols = Symbols(const_inc)
    packs = read_packs(metamorph_dat, symbols)
    rates = read_rates(battle_main_asm)

    if check_only:
        print("OK: {} packs x {} bytes + {} rate bytes; every byte resolved."
              .format(len(packs), PACK_SIZE, len(rates)))
        return 0

    _write(pack_inc_out, render_pack_inc(packs))
    _write(rate_inc_out, render_rate_inc(rates))
    _write(fixture_out, render_fixture(packs, rates))
    print("Emitted {} packs -> {}".format(len(packs), pack_inc_out))
    print("Emitted {} rates -> {}".format(len(rates), rate_inc_out))
    print("Emitted fixture -> {}".format(fixture_out))
    return 0


def _write(path, text):
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def _resolve(args):
    metamorph = args.metamorph_prop_dat
    battle_main = args.battle_main_asm
    const_inc = args.const_inc
    if args.source_root:
        if not metamorph:
            metamorph = os.path.join(args.source_root, "src", "battle",
                                     "metamorph_prop.dat")
        if not battle_main:
            battle_main = os.path.join(args.source_root, "src", "battle",
                                       "battle_main.asm")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return metamorph, battle_main, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (metamorph_prop.dat + "
                         "battle_main.asm + const.inc resolved under it)")
    ap.add_argument("--metamorph-prop-dat", help="path to metamorph_prop.dat")
    ap.add_argument("--battle-main-asm", help="path to battle_main.asm")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--pack-inc-out",
                    default="src/data/generated/metamorph_prop_data.inc",
                    help="output path for the MetamorphPackEntry rows")
    ap.add_argument("--rate-inc-out",
                    default="src/data/generated/metamorph_rate_data.inc",
                    help="output path for the MetamorphRateEntry rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/metamorph_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    metamorph, battle_main, const_inc = _resolve(args)
    if not metamorph or not battle_main or not const_inc:
        ap.error("provide --source-root, or all of --metamorph-prop-dat, "
                 "--battle-main-asm, and --const-inc")
    try:
        return run(metamorph, battle_main, const_inc, args.pack_inc_out,
                   args.rate_inc_out, args.fixture_out,
                   check_only=args.check_only)
    except (ParseError, MetamorphError) as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
