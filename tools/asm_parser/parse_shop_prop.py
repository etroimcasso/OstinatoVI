#!/usr/bin/env python3
"""Emit the shop-properties table from original-src shop_prop.dat.

Port-time tooling (NOT a build/CI dependency). shop_prop.dat is the raw
128-record x 9-byte shop table (ROM C4/7AC0) — one packed config byte plus
eight item slots per shop. The table is version-invariant upstream (a plain
.incbin, src/menu/shop.asm:2305-2310). The layout authority is the consumer
access sites: the x9 stride (shop.asm:1794-1801), the config byte's two reads
(shop type shop.asm:1802, price adjustment shop.asm:900 with the 7-entry
dispatch at shop.asm:908-923), and the item-id reads (shop.asm:819, with the
$ff empty-slot check at shop.asm:822). This script reads the .dat straight
off disk, decomposes every byte into the port's typed surfaces, and emits:

  * src/data/generated/shop_prop_data.inc — one designated-initializer
    ShopEntry row per record (128 records), every field labeled inline; the
    kShopProperties array #includes it.
  * tests/fixtures/shop_prop_expected.h — the same 128 records as raw 9-byte
    rows (the ground-truth byte contract) for a full-corpus byte-equivalence
    test.

Item names resolve against original-src/include/const.inc's ITEM enum. The
shop-type and price-adjustment codes have no upstream symbol source (the
type's meaning lives in the SHOP_TYPE_1..5 display strings, the adjustment's
in the AdjustShopPrice dispatch comments), so the code->name tables below
mirror src/data/shop_properties.h's named constants — any drift is caught at
compile + full-corpus memcmp.

Structural guarantees, hard-errored at emit time:
  * the .dat is exactly 1152 bytes (128 records x 9 — any other length is
    the wrong artifact);
  * every config byte stays inside the consumed spaces: type <= 5 (the
    shop-name text table has no entry past 5), adjustment <= 6 (the dispatch
    table has 7 entries), bits 6-7 clear (read by no consumer, clear across
    the corpus);
  * a config-$00 record is fully empty (all eight slots $ff) — the corpus'
    41 unused rows all take that shape;
  * empty slots ($ff) only trail real items — a real item after a $ff pad is
    an escalation, not a guess;
  * every item byte has an ITEM name.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_shop_prop.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_shop_prop.py --shop-prop-dat PATH --const-inc PATH \\
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

RECORD_COUNT = 128
RECORD_SIZE = 9
EXPECTED_LEN = RECORD_COUNT * RECORD_SIZE
ITEM_SLOTS = 8
EMPTY_SLOT = 0xFF

# Code->name tables (mirror src/data/shop_properties.h). Types are named from
# the upstream shop-name display strings (SHOP_TYPE_1..5,
# src/menu/menu_text_en.inc:282-286); adjustments from the AdjustShopPrice
# dispatch comments (shop.asm:908-915).
_SHOP_TYPE_NAMES = {
    0: "kShopTypeUnused",
    1: "kShopTypeWeapon",
    2: "kShopTypeArmor",
    3: "kShopTypeItem",
    4: "kShopTypeRelics",
    5: "kShopTypeVendor",
}

_PRICE_ADJUST_NAMES = {
    0: "kPriceAdjustNone",
    1: "kPriceAdjustPlus50",
    2: "kPriceAdjustPlus100",
    3: "kPriceAdjustMinus50",
    4: "kPriceAdjustFemaleMinus50",
    5: "kPriceAdjustMaleMinus50",
    6: "kPriceAdjustEdgarMinus50",
}


class ShopPropError(Exception):
    pass


# --- symbol resolution -------------------------------------------------------

class Symbols(object):
    """The const.inc ITEM enum the item slots resolve against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        if self.parsed.enum("ITEM") is None:
            raise ParseError(const_inc, 0, "expected enum 'ITEM' not found")
        # First declaration wins for aliased values.
        self.item_names = {}
        for m in self.parsed.enum("ITEM").members:
            self.item_names.setdefault(m.value, m.name)


# --- byte decomposition ------------------------------------------------------

def decompose_config(byte, index):
    """The config byte -> (type_name, adjust_name). Hard-errors on any value
    outside the consumed spaces (type > 5, adjustment > 6, bits 6-7 set)."""
    if byte & 0xC0:
        raise ShopPropError(
            "shop {}: config byte {:#04x} sets bit(s) 6-7, which no consumer "
            "reads and the corpus leaves clear — escalate, never guess"
            .format(index, byte))
    type_value = byte & 0x07
    type_name = _SHOP_TYPE_NAMES.get(type_value)
    if type_name is None:
        raise ShopPropError(
            "shop {}: shop type {} has no text-table entry (max 5) — "
            "escalate, never guess".format(index, type_value))
    adjust_value = (byte & 0x38) >> 3
    adjust_name = _PRICE_ADJUST_NAMES.get(adjust_value)
    if adjust_name is None:
        raise ShopPropError(
            "shop {}: price adjustment {} is off the 7-entry dispatch table "
            "— escalate, never guess".format(index, adjust_value))
    return type_name, adjust_name


class Record(object):
    """One decomposed 9-byte record: raw bytes + the typed-surface names."""

    def __init__(self, index, raw, symbols):
        assert len(raw) == RECORD_SIZE
        self.index = index
        self.raw = list(raw)
        self.type_name, self.adjust_name = decompose_config(raw[0], index)
        if raw[0] == 0 and any(b != EMPTY_SLOT for b in raw[1:]):
            raise ShopPropError(
                "shop {}: config $00 record carries a real item — every "
                "unused row in the corpus is fully empty; escalate, never "
                "guess".format(index))
        self.item_names = []
        seen_empty = False
        for slot, byte in enumerate(raw[1:]):
            if byte == EMPTY_SLOT:
                seen_empty = True
            elif seen_empty:
                raise ShopPropError(
                    "shop {}: slot {} holds item {:#04x} after a $ff pad — "
                    "pads only trail real items; escalate, never guess"
                    .format(index, slot, byte))
            name = symbols.item_names.get(byte)
            if name is None:
                raise ShopPropError(
                    "shop {}: slot {} item {:#04x} has no ITEM name"
                    .format(index, slot, byte))
            self.item_names.append(name)


def read_records(dat_path, symbols):
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != EXPECTED_LEN:
        raise ShopPropError(
            "{}: {} bytes, expected {} (128 records x 9 — wrong artifact)"
            .format(dat_path, len(data), EXPECTED_LEN))
    return [Record(i, data[i * RECORD_SIZE:(i + 1) * RECORD_SIZE], symbols)
            for i in range(RECORD_COUNT)]


# --- rendering ---------------------------------------------------------------

_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_shop_prop.py\n"
    "// Source: src/menu/shop_prop.dat (ShopProp, ROM C4/7AC0,\n"
    "//         128 records x 9 bytes; layout per the consumer access sites\n"
    "//         cited in src/data/shop_properties.h)\n"
    "// Source: include/const.inc (ITEM values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_shop_prop.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/shop_prop_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/shop_prop_expected.h\n"
    "\n"
)

_LINE_WIDTH = 78


def _wrap_braces(lead, tokens, indent):
    """Render lead{token, token, ...} wrapped at _LINE_WIDTH with the
    continuation lines aligned under the opening brace's content."""
    single = "{}{{{}}}".format(lead, ", ".join(tokens))
    if len(indent) + len(single) <= _LINE_WIDTH:
        return single
    cont = " " * (len(indent) + len(lead) + 1)
    lines = []
    current = "{}{{".format(lead)
    for i, token in enumerate(tokens):
        piece = token + ("," if i + 1 < len(tokens) else "}")
        if current.endswith("{"):
            candidate = current + piece
        else:
            candidate = current + " " + piece
        if len(indent) + len(candidate) > _LINE_WIDTH \
                and not current.endswith("{"):
            lines.append(current)
            current = cont.removeprefix(indent) + piece
        else:
            current = candidate
    lines.append(current)
    return "\n{}".format(indent).join(lines)


def _render_row(rec):
    indent = " " * 12
    if rec.raw[0] == 0:
        config = "ShopConfig{}"
    else:
        config = "ShopConfig::of({}, {})".format(rec.type_name,
                                                 rec.adjust_name)
    items = _wrap_braces(
        ".items  = ", ["ItemId::{}".format(n) for n in rec.item_names],
        indent)
    return (
        "    ShopEntry{{  // [${:02X}]\n"
        "        .shopIndex = {},\n"
        "        .record = ShopProperties{{\n"
        "            .config = {},\n"
        "            {},\n"
        "        }},\n"
        "    }},\n"
    ).format(rec.index, rec.index, config, items)


def render_inc(records):
    lines = [_HEADER_COMMON,
             "// ShopEntry rows in table order (0..127), one designated-\n"
             "// initializer row per record, #included inside the\n"
             "// kShopProperties array in src/data/shop_properties.cpp. Each\n"
             "// row's identity is its .shopIndex field (decimal — shops have\n"
             "// no upstream index enum); a compile-time assert verifies\n"
             "// shopIndex == position. The packed .record stays\n"
             "// byte-identical to the 9 ROM bytes. Every value renders\n"
             "// through a named surface: the config byte through\n"
             "// ShopConfig::of with the named type / price-adjustment codes\n"
             "// (empty records are ShopConfig{}), the item slots as ItemId\n"
             "// enumerators (trailing empty slots are ItemId::EMPTY).\n\n"]
    for rec in records:
        lines.append(_render_row(rec))
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 9-byte shop_prop record; field names mirror the\n"
    "// src/data/shop_properties.h record layout (the eight item slots appear\n"
    "// as their individual bytes). Values are the exact ROM bytes —\n"
    "// deliberately independent of the typed-surface rows in\n"
    "// shop_prop_data.inc, so decomposition/builder drift in either artifact\n"
    "// fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedShopRecord {\n"
    "    std::uint8_t config;\n"
    "    std::uint8_t item0, item1, item2, item3, item4, item5, item6, item7;\n"
    "};\n"
    "static_assert(sizeof(ExpectedShopRecord) == 9,\n"
    "              \"fixture record must stay byte-identical to a ROM shop_prop"
    " record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (the decimal\n"
    "// shop index) alongside the raw record bytes. Mirrors\n"
    "// ostinato::ShopEntry without depending on it.\n"
    "struct ExpectedShopEntry {\n"
    "    std::uint8_t shopIndex;\n"
    "    ExpectedShopRecord record;\n"
    "};\n"
)


def _fixture_row(rec):
    h = ["0x{:02X}".format(b) for b in rec.raw]
    return (
        "    {{ .shopIndex = {:>3},  // [${:02X}]\n"
        "      .record = {{ .config = {}, .item0 = {}, .item1 = {},\n"
        "                  .item2 = {}, .item3 = {}, .item4 = {},\n"
        "                  .item5 = {}, .item6 = {}, .item7 = {} }} }},\n"
    ).format(rec.index, rec.index, *h)


def render_fixture(records):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_shop_properties.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture shopIndex == position, table shopIndex ==\n"
             "// position, and a 9-byte memcmp of the packed record against\n"
             "// src/data/generated/shop_prop_data.inc's row.\n"
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
             "inline constexpr std::array<ExpectedShopEntry, {}> "
             "kExpectedShopEntries = {{{{  // ROM ShopProp\n"
             .format(len(records))]
    for rec in records:
        lines.append(_fixture_row(rec))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ------------------------------------------------------------------

def run(shop_prop_dat, const_inc, inc_out, fixture_out, check_only=False):
    symbols = Symbols(const_inc)
    records = read_records(shop_prop_dat, symbols)

    if check_only:
        print("OK: {} records x {} bytes; every byte decomposed."
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
    shop_prop = args.shop_prop_dat
    const_inc = args.const_inc
    if args.source_root:
        if not shop_prop:
            shop_prop = os.path.join(args.source_root, "src", "menu",
                                     "shop_prop.dat")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return shop_prop, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (shop_prop.dat + const.inc "
                         "resolved under it)")
    ap.add_argument("--shop-prop-dat", help="path to shop_prop.dat")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out",
                    default="src/data/generated/shop_prop_data.inc",
                    help="output path for the ShopEntry rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/shop_prop_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    shop_prop, const_inc = _resolve(args)
    if not shop_prop or not const_inc:
        ap.error("provide --source-root, or both --shop-prop-dat and "
                 "--const-inc")
    try:
        return run(shop_prop, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except (ParseError, ShopPropError) as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
