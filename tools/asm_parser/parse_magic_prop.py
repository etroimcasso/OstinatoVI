#!/usr/bin/env python3
"""Emit the attack-properties table from original-src magic_prop_en.dat.

Port-time tooling (NOT a build/CI dependency). magic_prop_en.dat is the raw
256-record x 14-byte attack-properties table (ROM C4/6AC0); the record layout
authority is LoadMagicProp (src/battle/battle_main.asm) plus the RAM map
notes/battle-ram.txt:208-249, which documents every byte and every flag bit.
This script reads the .dat straight off disk, decomposes every byte into the
port's typed surfaces, and emits:

  * src/data/generated/magic_prop_en_data.inc — one designated-initializer
    AttackProperties row per record (256 records), every field labeled
    inline; the kAttackPropertiesEn array #includes it.
  * tests/fixtures/magic_prop_expected.h — the same 256 records as raw 14-byte
    rows (the ground-truth byte contract) for a full-corpus byte-equivalence
    test.

Symbol names (ATTACK identity comments, TARGET flag names, STATUS_ID status
names, ELEMENT names) resolve against original-src/include/const.inc; the flag
bytes at +2/+3/+4/+7 have no upstream symbol source (their meanings live only
in the prose RAM map), so their bit->name tables below mirror
include/ostinato/attack_flags.h — any drift is caught at compile +
full-corpus memcmp.

Structural guarantees, hard-errored at emit time:
  * the .dat is exactly 3584 bytes (256 x 14 — any other length is the wrong
    artifact);
  * every decomposed byte reconstructs to the source byte (unexpected targeting
    residue or an undocumented flag bit is an escalation, not a guess);
  * every record index has an ATTACK name.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_magic_prop.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_magic_prop.py --magic-prop-dat PATH --const-inc PATH \\
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
RECORD_SIZE = 14
EXPECTED_LEN = RECORD_COUNT * RECORD_SIZE

# --- bit->name tables (mirror include/ostinato/attack_flags.h) ---

# Record byte +2 (battle-ram.txt:212-220).
_TRAIT_BITS = (
    (0x01, "PHYSICAL"),
    (0x02, "INSTANT_DEATH"),
    (0x04, "RESURRECTION_TARGET"),
    (0x08, "INVERT_ON_UNDEAD"),
    (0x10, "RANDOM_TARGET"),
    (0x20, "IGNORE_DEFENSE"),
    (0x40, "NO_DAMAGE_SPLIT"),
    (0x80, "NO_CHARACTER_TARGET"),
)

# Record byte +3 (battle-ram.txt:221-229).
_FLAG1_BITS = (
    (0x01, "USABLE_ON_FIELD"),
    (0x02, "IGNORE_REFLECT"),
    (0x04, "LEARNABLE_LORE"),
    (0x08, "ENABLE_RUNIC"),
    (0x10, "QUICK_WARP"),
    (0x20, "RETARGET_IF_INVALID"),
    (0x40, "KILLS_ATTACKER"),
    (0x80, "AFFECT_MP"),
)

# Record byte +4 (battle-ram.txt:230-238).
_FLAG2_BITS = (
    (0x01, "RESTORE_HP_MP"),
    (0x02, "DRAIN"),
    (0x04, "REMOVE_STATUS"),
    (0x08, "TOGGLE_STATUS"),
    (0x10, "STAMINA_DEFENSE"),
    (0x20, "UNDODGEABLE"),
    (0x40, "LEVEL_DIVISIBLE"),
    (0x80, "FRACTIONAL_DAMAGE"),
)

# Record byte +7 (battle-ram.txt:241-243) — only bits 0-1 are documented; any
# other bit set is a hard error (escalation trigger, never a guess).
_MISC_BITS = (
    (0x01, "MISS_IF_STATUS_IMMUNE"),
    (0x02, "SHOW_ATTACK_MESSAGE"),
)

# Record byte +9 special-effect values — mirror
# include/ostinato/attack_effects.h (names from the handler headers in
# battle_main.asm's attacker/target special-effect jump tables, cited per
# enumerator there). Exactly the values the EN corpus carries plus the $FF
# no-effect sentinel; any other byte is a hard error, never a guess.
_ATTACK_EFFECT_NAMES = {
    0x00: "PUMMEL",
    0x10: "SCAN",
    0x11: "GOLEM",
    0x12: "METAMORPH",
    0x13: "PALIDOR",
    0x15: "MANTRA",
    0x16: "SPIRALER",
    0x17: "TAPIR",
    0x18: "WARP",
    0x19: "EXPLODER",
    0x1A: "BLOW_FISH",
    0x1B: "PEARL_WIND",
    0x1C: "REFLECT_LORE",
    0x1D: "PEARL_LORE",
    0x1E: "STEP_MINE",
    0x1F: "DISCHORD",
    0x20: "PEP_UP",
    0x21: "RIPPLER",
    0x22: "STONE",
    0x23: "DISABLE_COUNTERATTACK",
    0x24: "CRUSADER",
    0x25: "MISSES_FLOATING_TARGETS",
    0x26: "WALLCHANGE",
    0x27: "ESCAPE",
    0x28: "MIND_BLAST",
    0x29: "N_CROSS",
    0x2A: "FLARE_STAR",
    0x2B: "R_POLARITY",
    0x2C: "LAUNCHER",
    0x2D: "LOVE_TOKEN",
    0x2E: "SEIZE",
    0x2F: "TARGETTING",
    0x30: "SUPLEX",
    0x31: "FORCEFIELD",
    0x32: "QUADRA_SLAM_SLICE",
    0x33: "BABABREATH",
    0x34: "CHARM",
    0x35: "DOOM",
    0x36: "EMPOWERER",
    0x37: "OVERCAST",
    0x38: "SNEEZE",
    0x39: "ENGULF",
    0x3A: "ZINGER",
    0x3B: "EVIL_TOOT",
    0x3C: "RETORT",
    0x3D: "REVENGE",
    0x3E: "PHANTASM",
    0x3F: "STUNNER",
    0x40: "FALLEN_ONE",
    0x43: "QUICK",
    0x44: "DISCARD",
    0x45: "CLEAR",
    0xFF: "NONE",
}


# --- symbol resolution -------------------------------------------------------

class Symbols(object):
    """The const.inc enums the decomposition resolves against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        for enum_name in ("ATTACK", "TARGET", "STATUS_ID", "ELEMENT"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(enum_name))
        self.attack_names = self._names_by_value("ATTACK")
        self.status_names = self._names_by_value("STATUS_ID")
        self.element_by_bit = self._names_by_value("ELEMENT")
        self.target = {m.name: m.value
                       for m in self.parsed.enum("TARGET").members}
        for needed in ("MANUAL", "ONE_SIDE", "INIT_MASK", "INIT_ALL",
                       "INIT_GROUP", "INIT_HALF", "AUTO_CONFIRM",
                       "MULTI_TARGET", "ENEMY", "ROULETTE", "MENU"):
            if needed not in self.target:
                raise ParseError(const_inc, 0,
                                 "TARGET::{} not found".format(needed))

    def _names_by_value(self, enum_name):
        # First declaration wins (trailing aliases like TARGET::SELF never
        # shadow the primary name).
        by_value = {}
        for m in self.parsed.enum(enum_name).members:
            by_value.setdefault(m.value, m.name)
        return by_value


# --- byte decomposition ------------------------------------------------------

class MagicPropError(Exception):
    pass


def _decompose_bits(byte, bit_table, what, index):
    """Split a flag byte into its named bits; hard-error on undocumented bits."""
    names = []
    residue = byte
    for bit, name in bit_table:
        if byte & bit:
            names.append(name)
            residue &= ~bit
    if residue:
        raise MagicPropError(
            "record {:#04x}: {} byte {:#04x} uses undocumented bit(s) {:#04x} "
            "— escalate, never guess".format(index, what, byte, residue))
    return names


def decompose_targeting(byte, target, index):
    """The deterministic targeting-byte decomposition.

    $FF -> MENU alone; else the INIT_MASK sub-field token (INIT_SINGLE == 0
    omitted) followed by the remaining bits in ascending order. For the
    duplicate value $02 emit ONE_SIDE (the primary declaration; SELF is the
    trailing alias). Unexpected residue after reconstruction is a hard error.
    """
    if byte == target["MENU"]:
        return ["MENU"]
    if byte == 0:
        return []
    names = []
    init = byte & target["INIT_MASK"]
    if init:
        init_name = {target["INIT_ALL"]: "INIT_ALL",
                     target["INIT_GROUP"]: "INIT_GROUP",
                     target["INIT_HALF"]: "INIT_HALF"}[init]
        names.append(init_name)
    remaining = byte & ~target["INIT_MASK"] & 0xFF
    for name in ("MANUAL", "ONE_SIDE", "AUTO_CONFIRM", "MULTI_TARGET",
                 "ENEMY", "ROULETTE"):
        if remaining & target[name]:
            names.append(name)
            remaining &= ~target[name]
    if remaining:
        raise MagicPropError(
            "record {:#04x}: targeting byte {:#04x} left residue {:#04x} after "
            "decomposition — escalate, never guess".format(index, byte, remaining))
    packed = 0
    for n in names:
        packed |= target[n]
    if packed != byte:
        raise MagicPropError(
            "record {:#04x}: targeting decomposition round-trip {:#04x} != "
            "source {:#04x}".format(index, packed, byte))
    return names


def decompose_elements(byte, element_by_bit, index):
    names = []
    for bit_pos in range(8):
        bit = 1 << bit_pos
        if byte & bit:
            name = element_by_bit.get(bit)
            if name is None:
                raise MagicPropError(
                    "record {:#04x}: element bit {:#04x} has no ELEMENT name"
                    .format(index, bit))
            names.append(name)
    return names


def decompose_special_effect(byte, index):
    """The byte +9 -> AttackSpecialEffect enumerator name; hard-error on any
    byte outside the corpus map (a corpus divergence is an escalation)."""
    name = _ATTACK_EFFECT_NAMES.get(byte)
    if name is None:
        raise MagicPropError(
            "record {:#04x}: special-effect byte {:#04x} has no "
            "AttackSpecialEffect name — escalate, never guess"
            .format(index, byte))
    return name


def decompose_statuses(status_bytes, status_names, index):
    """The four status bytes -> StatusId names (id = byte*8 + bit, ascending)."""
    names = []
    for byte_i, byte in enumerate(status_bytes):
        for bit_pos in range(8):
            if byte & (1 << bit_pos):
                status_id = byte_i * 8 + bit_pos
                name = status_names.get(status_id)
                if name is None:
                    raise MagicPropError(
                        "record {:#04x}: status id {} has no STATUS_ID name"
                        .format(index, status_id))
                names.append(name)
    return names


class Record(object):
    """One decomposed 14-byte record: raw bytes + the typed-surface names."""

    def __init__(self, index, raw, symbols):
        assert len(raw) == RECORD_SIZE
        self.index = index
        self.raw = list(raw)
        name = symbols.attack_names.get(index)
        if name is None:
            raise MagicPropError(
                "record {:#04x} has no ATTACK name".format(index))
        self.name = name
        self.targeting = decompose_targeting(raw[0], symbols.target, index)
        self.elements = decompose_elements(raw[1], symbols.element_by_bit, index)
        self.traits = _decompose_bits(raw[2], _TRAIT_BITS, "trait", index)
        self.flags1 = _decompose_bits(raw[3], _FLAG1_BITS, "flags1", index)
        self.flags2 = _decompose_bits(raw[4], _FLAG2_BITS, "flags2", index)
        self.mp_cost = raw[5]
        self.power = raw[6]
        self.misc = _decompose_bits(raw[7], _MISC_BITS, "misc", index)
        self.hit_rate = raw[8]
        self.special_effect = raw[9]
        self.special_effect_name = decompose_special_effect(raw[9], index)
        self.statuses = decompose_statuses(raw[10:14], symbols.status_names,
                                           index)


def read_records(dat_path, symbols):
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != EXPECTED_LEN:
        raise MagicPropError(
            "{}: {} bytes, expected {} (256 records x 14 — wrong artifact)"
            .format(dat_path, len(data), EXPECTED_LEN))
    return [Record(i, data[i * RECORD_SIZE:(i + 1) * RECORD_SIZE], symbols)
            for i in range(RECORD_COUNT)]


# --- rendering ---------------------------------------------------------------

# Emitted-file header: AUTO-GENERATED line, Source lines, upstream pin,
# DO-NOT-EDIT + exact regeneration command, then a consumption paragraph.
_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_magic_prop.py\n"
    "// Source: src/battle/magic_prop_en.dat (MagicProp, ROM C4/6AC0,\n"
    "//         256 records x 14 bytes; layout per notes/battle-ram.txt:208-249\n"
    "//         and LoadMagicProp in src/battle/battle_main.asm)\n"
    "// Source: include/const.inc (ATTACK / TARGET / STATUS_ID / ELEMENT values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_magic_prop.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/magic_prop_en_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/magic_prop_expected.h\n"
    "\n"
)


def _of_or_empty(type_name, member_scope, names):
    if not names:
        return "{}{{}}".format(type_name)
    return "{}::of({})".format(
        type_name, ", ".join("{}::{}".format(member_scope, n) for n in names))


def _render_row(rec):
    targeting = ("Targeting{}" if not rec.targeting else
                 "Targeting::of({})".format(
                     ", ".join("TargetFlags::{}".format(n)
                               for n in rec.targeting)))
    special = "AttackSpecialEffect::{}".format(rec.special_effect_name)
    return (
        "    AttackPropertiesEntry{{  // [${:02X}]\n"
        "        .id = AttackId::{},\n"
        "        .record = AttackProperties{{\n"
        "            .targeting     = {},\n"
        "            .element       = {},\n"
        "            .traits        = {},\n"
        "            .flags1        = {},\n"
        "            .flags2        = {},\n"
        "            .mpCost        = {},\n"
        "            .power         = {},\n"
        "            .misc          = {},\n"
        "            .hitRate       = {},\n"
        "            .specialEffect = {},\n"
        "            .statuses      = {},\n"
        "        }},\n"
        "    }},\n"
    ).format(
        rec.index,
        rec.name,
        targeting,
        _of_or_empty("ElementSet", "Element", rec.elements),
        _of_or_empty("AttackTraitSet", "AttackTrait", rec.traits),
        _of_or_empty("AttackFlags1", "AttackFlag1", rec.flags1),
        _of_or_empty("AttackFlags2", "AttackFlag2", rec.flags2),
        rec.mp_cost, rec.power,
        _of_or_empty("AttackMiscFlags", "AttackMiscFlag", rec.misc),
        rec.hit_rate, special,
        _of_or_empty("StatusSet", "StatusId", rec.statuses))


def render_inc(records):
    lines = [_HEADER_COMMON,
             "// AttackPropertiesEntry rows in ATTACK index order ($00..$FF),\n"
             "// one designated-initializer row per record, #included inside\n"
             "// the kAttackPropertiesEn array in\n"
             "// src/data/attack_properties.cpp. Each row's identity is its\n"
             "// .id field — the AttackId enumerator (the full unified ATTACK\n"
             "// value space); a compile-time assert verifies id == position.\n"
             "// The packed .record stays byte-identical to the 14 ROM bytes.\n"
             "// Flag bytes render through the of(...) builders so every set\n"
             "// bit is named; empty sets render as TypeName{}. mpCost /\n"
             "// power / hitRate are decimal (semantic magnitudes);\n"
             "// specialEffect renders as its AttackSpecialEffect enumerator\n"
             "// (ostinato/attack_effects.h — the special-effect dispatch\n"
             "// index), with $FF as AttackSpecialEffect::NONE.\n\n"]
    for rec in records:
        lines.append(_render_row(rec))
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 14-byte magic_prop record; field names and order mirror the\n"
    "// battle-ram.txt:210-249 record layout. Values are the exact ROM bytes —\n"
    "// deliberately independent of the typed-surface rows in\n"
    "// magic_prop_en_data.inc, so decomposition/builder drift in either\n"
    "// artifact fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedAttackRecord {\n"
    "    std::uint8_t targeting;\n"
    "    std::uint8_t element;\n"
    "    std::uint8_t traits;\n"
    "    std::uint8_t flags1;\n"
    "    std::uint8_t flags2;\n"
    "    std::uint8_t mpCost;\n"
    "    std::uint8_t power;\n"
    "    std::uint8_t misc;\n"
    "    std::uint8_t hitRate;\n"
    "    std::uint8_t specialEffect;\n"
    "    std::uint8_t status1, status2, status3, status4;\n"
    "};\n"
    "static_assert(sizeof(ExpectedAttackRecord) == 14,\n"
    "              \"fixture record must stay byte-identical to a ROM magic_prop"
    " record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (raw decimal\n"
    "// index — the fixture stays independent of the port's AttackId header)\n"
    "// alongside the raw record bytes. Mirrors ostinato::AttackPropertiesEntry\n"
    "// without depending on it.\n"
    "struct ExpectedAttackEntry {\n"
    "    std::uint8_t id;\n"
    "    ExpectedAttackRecord record;\n"
    "};\n"
)


def _fixture_row(rec):
    h = ["0x{:02X}".format(b) for b in rec.raw]
    return (
        "    {{ .id = {:>3},  // ${:02X} {}\n"
        "      .record = {{ .targeting = {}, .element = {}, .traits = {},\n"
        "                  .flags1 = {}, .flags2 = {}, .mpCost = {},"
        " .power = {},\n"
        "                  .misc = {}, .hitRate = {}, .specialEffect = {},\n"
        "                  .status1 = {}, .status2 = {}, .status3 = {},"
        " .status4 = {} }} }},\n"
    ).format(rec.index, rec.index, rec.name, *h)


def render_fixture(records):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_attack_properties.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture id == position, table id enumerator ==\n"
             "// position, and a 14-byte memcmp of the packed record against\n"
             "// src/data/generated/magic_prop_en_data.inc's row.\n"
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
             "inline constexpr std::array<ExpectedAttackEntry, {}> "
             "kExpectedAttackEntries = {{{{  // ROM MagicProp (EN)\n"
             .format(len(records))]
    for rec in records:
        lines.append(_fixture_row(rec))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ------------------------------------------------------------------

def run(magic_prop_dat, const_inc, inc_out, fixture_out, check_only=False):
    symbols = Symbols(const_inc)
    records = read_records(magic_prop_dat, symbols)

    if check_only:
        print("OK: {} records x {} bytes; every byte decomposed and "
              "round-tripped.".format(len(records), RECORD_SIZE))
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
    magic_prop = args.magic_prop_dat
    const_inc = args.const_inc
    if args.source_root:
        if not magic_prop:
            magic_prop = os.path.join(args.source_root, "src", "battle",
                                      "magic_prop_en.dat")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return magic_prop, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (magic_prop_en.dat + const.inc "
                         "resolved under it)")
    ap.add_argument("--magic-prop-dat", help="path to magic_prop_en.dat")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out",
                    default="src/data/generated/magic_prop_en_data.inc",
                    help="output path for the AttackProperties rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/magic_prop_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    magic_prop, const_inc = _resolve(args)
    if not magic_prop or not const_inc:
        ap.error("provide --source-root, or both --magic-prop-dat and "
                 "--const-inc")
    try:
        return run(magic_prop, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except (ParseError, MagicPropError) as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
