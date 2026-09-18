#!/usr/bin/env python3
"""Emit the menu ordering lists from original-src menu/skills.asm and equip.asm.

Port-time tooling (NOT a build/CI dependency): reads three small lists the
menus read and emits:

  * src/data/generated/esper_menu_order_data.inc — GenjuOrder (skills.asm:1695,
    ROM $D1/F9B5): each esper's place in the esper list, keyed by EsperId.
  * src/data/generated/magic_list_order_data.inc — MagicOrderTbl
    (skills.asm:766, ROM $C3/4F49): for each of the six magic-order settings,
    the three spell bands in the order the list shows them.
  * src/data/generated/imp_equipment_data.inc — ImpItem (equip.asm:1684, ROM
    $ED/82E4): the items an imp can equip.
  * tests/fixtures/menu_orders_expected.h — all three as raw bytes.

Structural guarantees, hard-errored at parse/emit time:
  * GenjuOrder holds one entry per esper and its values are a permutation of
    1..27 — a menu ordering that repeated or skipped a place would be a
    grammar deviation, not data;
  * every MagicOrderTbl row names the three band-opening spells exactly once
    and ends with the $FF terminator;
  * ImpItem holds 16 slots, the first ten naming real items and the rest
    ITEM::EMPTY (the consumer reads ten, equip.asm:1670);
  * all three lists are identical to the vanilla cartridge.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_menu_orders.py --source-root PATH
    parse_menu_orders.py --source-root PATH --check-only
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import common
import parse_const_enums as pce
from common import ParseError, strip_comment

ESPER_ORDER_SNES = 0xD1F9B5
MAGIC_ORDER_SNES = 0xC34F49
IMP_EQUIPMENT_SNES = 0xED82E4

ESPER_COUNT = 27
MAGIC_ORDER_SETTINGS = 6
MAGIC_BANDS = 3
MAGIC_ROW_BYTES = MAGIC_BANDS + 1          # the three bands then the terminator
TERMINATOR = 0xFF
IMP_SLOTS = 16
IMP_SLOTS_READ = 10                        # equip.asm:1670 `cpx #$000a`
ITEM_EMPTY = 0xFF

_RE_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
_RE_LOCAL = re.compile(r"^@[0-9A-Za-z_]+:\s*")
_RE_BYTE = re.compile(r"^\.byte\s+(.+)$", re.IGNORECASE)
_RE_SCOPED = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)$")


def _paths(source_root):
    return {
        "const_inc": os.path.join(source_root, "include", "const.inc"),
        "skills_asm": os.path.join(source_root, "src", "menu", "skills.asm"),
        "equip_asm": os.path.join(source_root, "src", "menu", "equip.asm"),
    }


class Symbols(object):
    """The three const.inc enums these lists name, by value and by name."""

    WANTED = ("GENJU", "ATTACK", "ITEM")

    def __init__(self, const_inc):
        parsed = common.parse_ca65_constants(const_inc, skip_body_enums=pce.SKIP)
        self.by_value = {}
        self.by_name = {}
        for name in self.WANTED:
            enum = parsed.enum(name)
            if enum is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(name))
            self.by_value[name] = {m.value: m.name for m in enum.members}
            self.by_name[name] = {m.name: m.value for m in enum.members}

    def value(self, scope, member, path, lineno):
        if scope not in self.by_name:
            raise ParseError(path, lineno,
                             "'{}' is not an enum these lists resolve against "
                             "(grammar not covered — escalate, never guess)"
                             .format(scope))
        if member not in self.by_name[scope]:
            raise ParseError(path, lineno, "unknown {}::{}".format(scope, member))
        return self.by_name[scope][member]

    def name(self, scope, value, path):
        found = self.by_value[scope].get(value)
        if found is None:
            raise ParseError(path, 0, "no {} enumerator has value ${:02X}"
                             .format(scope, value))
        return found


def read_byte_list(asm_path, label, symbols):
    """The `.byte` values following `label`, up to the next global label."""
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
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
        if s.lower().startswith(".popseg"):
            break
        s = _RE_LOCAL.sub("", s)
        data = _RE_BYTE.match(s)
        if not data:
            raise ParseError(asm_path, lineno,
                             "unexpected line inside {}: '{}' (grammar not "
                             "covered — escalate, never guess)".format(label, s))
        for term in data.group(1).split(","):
            term = term.strip()
            literal = common.parse_int_literal(term)
            if literal is not None:
                value = literal
            else:
                scoped = _RE_SCOPED.match(term)
                if not scoped:
                    raise ParseError(asm_path, lineno,
                                     "'{}' is neither a literal nor a scoped "
                                     "enum reference".format(term))
                value = symbols.value(scoped.group(1), scoped.group(2),
                                      asm_path, lineno)
            if not 0 <= value <= 0xFF:
                raise ParseError(asm_path, lineno,
                                 "'{}' is outside a byte".format(term))
            values.append(value)
    if not inside:
        raise ParseError(asm_path, 0, "{} not found".format(label))
    return values


class Lists(object):
    def __init__(self, esper_order, magic_order, imp_equipment):
        self.esper_order = esper_order        # 27 places, esper index order
        self.magic_order = magic_order        # 6 rows x 4 bytes
        self.imp_equipment = imp_equipment    # 16 item ids


def check(lists, symbols, paths):
    path = paths["skills_asm"]
    if len(lists.esper_order) != ESPER_COUNT:
        raise ParseError(path, 0, "GenjuOrder holds {} entries, not {}".format(
            len(lists.esper_order), ESPER_COUNT))
    if sorted(lists.esper_order) != list(range(1, ESPER_COUNT + 1)):
        raise ParseError(path, 0,
                         "GenjuOrder is not a permutation of 1..{} — a menu "
                         "ordering cannot repeat or skip a place"
                         .format(ESPER_COUNT))

    if len(lists.magic_order) != MAGIC_ORDER_SETTINGS * MAGIC_ROW_BYTES:
        raise ParseError(path, 0,
                         "MagicOrderTbl holds {} bytes, not {} rows of {}"
                         .format(len(lists.magic_order), MAGIC_ORDER_SETTINGS,
                                 MAGIC_ROW_BYTES))
    bands = magic_bands(lists, symbols, path)
    for setting, row in enumerate(magic_rows(lists)):
        if row[-1] != TERMINATOR:
            raise ParseError(path, 0,
                             "MagicOrderTbl row {} does not end with the ${:02X} "
                             "terminator".format(setting, TERMINATOR))
        if sorted(row[:MAGIC_BANDS]) != sorted(bands):
            raise ParseError(path, 0,
                             "MagicOrderTbl row {} is not the three spell bands "
                             "once each".format(setting))

    path = paths["equip_asm"]
    if len(lists.imp_equipment) != IMP_SLOTS:
        raise ParseError(path, 0, "ImpItem holds {} slots, not {}".format(
            len(lists.imp_equipment), IMP_SLOTS))
    for slot, value in enumerate(lists.imp_equipment):
        empty = value == ITEM_EMPTY
        if slot < IMP_SLOTS_READ and empty:
            raise ParseError(path, 0,
                             "ImpItem slot {} is empty, but the consumer reads "
                             "{} slots".format(slot, IMP_SLOTS_READ))
        if slot >= IMP_SLOTS_READ and not empty:
            raise ParseError(path, 0,
                             "ImpItem slot {} names an item past the {} the "
                             "consumer reads".format(slot, IMP_SLOTS_READ))


def magic_rows(lists):
    return [lists.magic_order[i:i + MAGIC_ROW_BYTES]
            for i in range(0, len(lists.magic_order), MAGIC_ROW_BYTES)]


def magic_bands(lists, symbols, path):
    """The three band-opening spell ids, taken from the table's own first row."""
    first = magic_rows(lists)[0][:MAGIC_BANDS]
    if len(set(first)) != MAGIC_BANDS:
        raise ParseError(path, 0,
                         "MagicOrderTbl row 0 repeats a spell band")
    for value in first:
        symbols.name("ATTACK", value, path)
    return list(first)


def assert_rom(rom, lists, paths):
    for label, snes, want, path in (
            ("GenjuOrder", ESPER_ORDER_SNES, lists.esper_order,
             paths["skills_asm"]),
            ("MagicOrderTbl", MAGIC_ORDER_SNES, lists.magic_order,
             paths["skills_asm"]),
            ("ImpItem", IMP_EQUIPMENT_SNES, lists.imp_equipment,
             paths["equip_asm"])):
        base = common.hirom_file_offset(snes)
        got = list(rom[base:base + len(want)])
        if got != list(want):
            raise ParseError(path, 0,
                             "ROM MISMATCH: {} at ${:06X} differs from the "
                             "assembled source".format(label, snes))


def read_lists(source_root):
    paths = _paths(source_root)
    symbols = Symbols(paths["const_inc"])
    lists = Lists(read_byte_list(paths["skills_asm"], "GenjuOrder", symbols),
                  read_byte_list(paths["skills_asm"], "MagicOrderTbl", symbols),
                  read_byte_list(paths["equip_asm"], "ImpItem", symbols))
    check(lists, symbols, paths)
    return paths, symbols, lists


# --- rendering -----------------------------------------------------------------

_HEADER = (
    "// AUTO-GENERATED by tools/asm_parser/parse_menu_orders.py\n"
    "// Source: {}\n"
    "// Source: include/const.inc (GENJU / ATTACK / ITEM values)\n"
    "// (original-src pinned at 1ea47b5; cross-checked against the vanilla ROM\n"
    "// over the whole list)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_menu_orders.py \\\n"
    "//       --source-root  original-src\n\n")


def render_esper_order_inc(lists, symbols, path):
    out = [_HEADER.format("src/menu/skills.asm (GenjuOrder, 27 bytes, "
                          "ROM $D1/F9B5)"),
           "// { EsperId, place } pairs in esper index order, #included inside\n"
           "// the kEsperMenuOrder array in src/data/menu_orders.cpp. The place\n"
           "// is where the esper sits in the menu's list, counting from 1; the\n"
           "// 27 places are a permutation of 1..27. A compile-time assert\n"
           "// verifies esper == position + EsperId::RAMUH.\n\n"]
    base = symbols.by_name["GENJU"]["RAMUH"]
    for index, place in enumerate(lists.esper_order):
        out.append("    {{ EsperId::{}, {} }},\n".format(
            symbols.name("GENJU", base + index, path), place))
    return "".join(out)


def render_magic_order_inc(lists, symbols, path):
    out = [_HEADER.format("src/menu/skills.asm (MagicOrderTbl, 6 x 4 bytes, "
                          "ROM $C3/4F49)"),
           "// MagicListOrderEntry rows in setting order, #included inside the\n"
           "// kMagicListOrder array in src/data/menu_orders.cpp. Each row names\n"
           "// the first spell of each band — black, effect and white magic — in\n"
           "// the order that setting shows them. The $FF terminator the table\n"
           "// stores after each row is not a field; the parser asserts it. A\n"
           "// compile-time assert verifies setting == position.\n\n"]
    for setting, row in enumerate(magic_rows(lists)):
        names = ["AttackId::" + symbols.name("ATTACK", value, path)
                 for value in row[:MAGIC_BANDS]]
        out.append("    MagicListOrderEntry{{ .setting = {}, .firstSpells = {{ {} }} }},\n"
                   .format(setting, ", ".join(names)))
    return "".join(out)


def render_imp_equipment_inc(lists, symbols, path):
    out = [_HEADER.format("src/menu/equip.asm (ImpItem, 16 bytes, "
                          "ROM $ED/82E4)"),
           "// { slot, item } pairs, #included inside the kImpEquipment array in\n"
           "// src/data/menu_orders.cpp. The first ten slots name the items an\n"
           "// imp can equip; the rest are EMPTY and nothing reads them. A\n"
           "// compile-time assert verifies slot == position.\n\n"]
    for slot, value in enumerate(lists.imp_equipment):
        out.append("    {{ {}, ItemId::{} }},\n".format(
            slot, symbols.name("ITEM", value, path)))
    return "".join(out)


def render_fixture(lists):
    out = [_HEADER.format("skills.asm (GenjuOrder, MagicOrderTbl), "
                          "equip.asm (ImpItem)"),
           "// Test fixture for tests/test_menu_orders.cpp — the raw cartridge\n"
           "// bytes of all three lists, deliberately independent of the typed\n"
           "// rows in the generated tables.\n"
           "\n#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"
           "// Each esper's place in the menu list, in esper index order.\n"
           "inline constexpr std::array<std::uint8_t, 27> kExpectedEsperMenuOrder = {\n    ",
           ", ".join("{:>2}".format(v) for v in lists.esper_order),
           "};\n\n"
           "// The six magic-order rows, four raw bytes each including the\n"
           "// $FF terminator.\n"
           "inline constexpr std::array<std::array<std::uint8_t, 4>, 6>\n"
           "    kExpectedMagicListOrder = {{\n"]
    for row in magic_rows(lists):
        out.append("    {{ " + ", ".join("0x{:02X}".format(v) for v in row) + " }},\n")
    out.append("}};\n\n"
               "// The imp's sixteen equipment slots.\n"
               "inline constexpr std::array<std::uint8_t, 16> kExpectedImpEquipment = {\n    "
               + ", ".join("0x{:02X}".format(v) for v in lists.imp_equipment)
               + "};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver --------------------------------------------------------------------

def run(source_root, outputs, check_only=False):
    paths, symbols, lists = read_lists(source_root)
    assert_rom(common.load_vanilla_rom(source_root), lists, paths)
    if check_only:
        print("OK: {} esper places, {} magic-order rows, {} imp slots ({} read); "
              "cartridge-identical.".format(
                  len(lists.esper_order), MAGIC_ORDER_SETTINGS,
                  len(lists.imp_equipment), IMP_SLOTS_READ))
        return 0
    _write(outputs["esper_order"],
           render_esper_order_inc(lists, symbols, paths["skills_asm"]))
    _write(outputs["magic_order"],
           render_magic_order_inc(lists, symbols, paths["skills_asm"]))
    _write(outputs["imp_equipment"],
           render_imp_equipment_inc(lists, symbols, paths["equip_asm"]))
    _write(outputs["fixture"], render_fixture(lists))
    for key in ("esper_order", "magic_order", "imp_equipment", "fixture"):
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
    ap.add_argument("--esper-order-out",
                    default="src/data/generated/esper_menu_order_data.inc")
    ap.add_argument("--magic-order-out",
                    default="src/data/generated/magic_list_order_data.inc")
    ap.add_argument("--imp-equipment-out",
                    default="src/data/generated/imp_equipment_data.inc")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/menu_orders_expected.h")
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args(argv)
    outputs = {"esper_order": args.esper_order_out,
               "magic_order": args.magic_order_out,
               "imp_equipment": args.imp_equipment_out,
               "fixture": args.fixture_out}
    try:
        return run(args.source_root, outputs, check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
