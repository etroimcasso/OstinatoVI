#!/usr/bin/env python3
"""Emit the monster-properties table from original-src monster_prop.dat.

Port-time tooling (NOT a build/CI dependency). monster_prop.dat is the raw
384-record x 32-byte monster-properties table (ROM CF/0000); the record layout
authority is LoadMonsterProp + LoadRageProp (src/battle/battle_main.asm:
7307-7436 and 7504-7550), whose per-byte load comments name every field, plus
the RAM map notes/battle-ram.txt:952-970 for the two flag bytes and the packed
metamorph byte. This script reads the .dat straight off disk, decomposes every
byte into the port's typed surfaces, and emits:

  * src/data/generated/monster_prop_data.inc — one designated-initializer
    MonsterProperties row per record (384 records), every field labeled
    inline; the kMonsterProperties array #includes it.
  * tests/fixtures/monster_prop_expected.h — the same 384 records as raw
    32-byte rows (the ground-truth byte contract) for a full-corpus
    byte-equivalence test.

Symbol names (MONSTER identity, ITEM attack-graphic names, STATUS_ID status
names, ELEMENT names) resolve against original-src/include/const.inc; the two
flag bytes at +18/+19 have no upstream symbol source (their meanings live only
in the prose RAM map), so their bit->name tables below mirror
include/ostinato/monster_flags.h — any drift is caught at compile +
full-corpus memcmp.

Structural guarantees, hard-errored at emit time:
  * the .dat is exactly 12288 bytes (384 x 32 — any other length is the wrong
    artifact);
  * every record index has a MONSTER name, and the MONSTER space is exactly
    384 ids;
  * every decomposed byte reconstructs to the source byte (a blocked status
    homed outside status bytes 1-3, an element bit without a name, or an
    unnamed status id is an escalation, not a guess).

Python 3 standard library only; targets 3.9+.

Usage:
    parse_monster_prop.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_monster_prop.py --monster-prop-dat PATH --const-inc PATH \\
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

RECORD_COUNT = 384
RECORD_SIZE = 32
EXPECTED_LEN = RECORD_COUNT * RECORD_SIZE

# --- bit->name tables (mirror include/ostinato/monster_flags.h) ---

# Record byte +18 -> $3C95, battle-ram.txt:965-970 ("ui-h-n-m"). Bits 1/3/5
# are unused in the layout; they keep named UNUSED_n enumerators so a corpus
# byte that sets one still renders through a named surface.
_TRAIT_BITS = (
    (0x01, "DIES_AT_ZERO_MP"),
    (0x02, "UNUSED_1"),
    (0x04, "DONT_DISPLAY_NAME"),
    (0x08, "UNUSED_3"),
    (0x10, "HUMAN"),
    (0x20, "UNUSED_5"),
    (0x40, "IMP_CRITICAL"),
    (0x80, "UNDEAD"),
)

# Record byte +19 -> $3C80, battle-ram.txt:952-960 ("c?ksruph").
_BATTLE_BITS = (
    (0x01, "HARDER_TO_RUN"),
    (0x02, "FIRST_STRIKE"),
    (0x04, "CANT_SUPLEX"),
    (0x08, "CANT_RUN"),
    (0x10, "CANT_SCAN"),
    (0x20, "CANT_SKETCH"),
    (0x40, "SPECIAL_EVENT"),
    (0x80, "CANT_CONTROL"),
)

# Blocked statuses cover status bytes 1-3 only (record bytes +20..+22); the
# record has no fourth immunity byte (the loader applies a constant for it —
# battle_main.asm:7515-7517). Status ids 0..23 are the only legal residents.
_BLOCKED_STATUS_MAX_ID = 23

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
    """The const.inc enums the decomposition resolves against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        for enum_name in ("MONSTER", "ITEM", "STATUS_ID", "ELEMENT"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(enum_name))
        self.monster_names = self._names_by_value("MONSTER")
        self.item_names = self._names_by_value("ITEM")
        self.status_names = self._names_by_value("STATUS_ID")
        self.element_by_bit = self._names_by_value("ELEMENT")
        # The MONSTER space is exactly the 384 record indices — a mismatch
        # means the wrong const.inc (or a repin changed the id space).
        max_id = max(self.monster_names)
        if max_id != RECORD_COUNT - 1:
            raise ParseError(const_inc, 0,
                             "MONSTER max id {} != {} (record count mismatch)"
                             .format(max_id, RECORD_COUNT - 1))

    def _names_by_value(self, enum_name):
        # First declaration wins (trailing aliases never shadow the primary).
        by_value = {}
        for m in self.parsed.enum(enum_name).members:
            by_value.setdefault(m.value, m.name)
        return by_value


# --- byte decomposition ------------------------------------------------------

class MonsterPropError(Exception):
    pass


def _decompose_bits(byte, bit_table, what, index):
    """Split a flag byte into its named bits. Both monster flag bytes name all
    eight bits, so decomposition is total; the residue check guards the table
    itself."""
    names = []
    residue = byte
    for bit, name in bit_table:
        if byte & bit:
            names.append(name)
            residue &= ~bit
    if residue:
        raise MonsterPropError(
            "record {:#05x}: {} byte {:#04x} left residue {:#04x} "
            "— escalate, never guess".format(index, what, byte, residue))
    return names


def decompose_metamorph(byte, index):
    """Byte +17 -> (pack_index, rate_index): pppiiiii per battle-ram.txt:962-964
    (low 5 bits select the item pack, high 3 bits the probability row), decoded
    by TargetEffect_12 (battle_main.asm:9385-9409)."""
    pack_index = byte & 0x1F
    rate_index = byte >> 5
    if (rate_index << 5) | pack_index != byte:
        raise MonsterPropError(
            "record {:#05x}: metamorph byte {:#04x} failed the pack/rate "
            "round-trip".format(index, byte))
    return pack_index, rate_index


def decompose_special_attack(byte, index):
    """Byte +31 -> (effect_class, cant_dodge, no_damage) per the monster
    special-attack setup (battle_main.asm:8195-8235): bit 7 = can't dodge,
    bit 6 = deals no damage (status-only), low 6 bits = effect class."""
    effect_class = byte & 0x3F
    cant_dodge = bool(byte & 0x80)
    no_damage = bool(byte & 0x40)
    packed = effect_class | (0x80 if cant_dodge else 0) | \
        (0x40 if no_damage else 0)
    if packed != byte:
        raise MonsterPropError(
            "record {:#05x}: special-attack byte {:#04x} failed the "
            "decompose round-trip".format(index, byte))
    return effect_class, cant_dodge, no_damage


def special_attack_builder(effect_class, status_names, index):
    """The effect-class byte -> its per-band builder call, following the
    dispatch's own decode order (battle_main.asm:8225-8235): below $20 the
    value is the StatusId the attack inflicts; $20-$2F is a damage-multiplier
    boost of (value - $20); $30/$31 drain HP/MP; $32 upward removes reflect
    with any bits past $32 dead at dispatch (carried as the labeled dead
    residual so the byte round-trips)."""
    if effect_class < 0x20:
        name = status_names.get(effect_class)
        if name is None:
            raise MonsterPropError(
                "record {:#05x}: special-attack status class {:#04x} has no "
                "STATUS_ID name".format(index, effect_class))
        return "MonsterSpecialAttack::inflictStatus(StatusId::{})".format(name)
    if effect_class < 0x30:
        return "MonsterSpecialAttack::damageBoost({})".format(
            effect_class - 0x20)
    if effect_class == 0x30:
        return "MonsterSpecialAttack::drainHp()"
    if effect_class == 0x31:
        return "MonsterSpecialAttack::drainMp()"
    residual = effect_class - 0x32
    if residual == 0:
        return "MonsterSpecialAttack::removeReflect()"
    return ("MonsterSpecialAttack::removeReflect("
            "/*deadResidualBits=*/{})").format(residual)


def decompose_elements(byte, element_by_bit, what, index):
    names = []
    for bit_pos in range(8):
        bit = 1 << bit_pos
        if byte & bit:
            name = element_by_bit.get(bit)
            if name is None:
                raise MonsterPropError(
                    "record {:#05x}: {} element bit {:#04x} has no ELEMENT "
                    "name".format(index, what, bit))
            names.append(name)
    return names


def decompose_statuses(status_bytes, status_names, first_id, max_id, what,
                       index):
    """Status bytes -> StatusId names (id = first_id + byte*8 + bit,
    ascending). max_id caps the legal id space: 23 for the 3-byte blocked
    block (no blocked-status-4 byte exists), 31 for the 4 innate bytes."""
    names = []
    for byte_i, byte in enumerate(status_bytes):
        for bit_pos in range(8):
            if byte & (1 << bit_pos):
                status_id = first_id + byte_i * 8 + bit_pos
                if status_id > max_id:
                    raise MonsterPropError(
                        "record {:#05x}: {} status id {} exceeds max {} "
                        "— escalate, never guess"
                        .format(index, what, status_id, max_id))
                name = status_names.get(status_id)
                if name is None:
                    raise MonsterPropError(
                        "record {:#05x}: {} status id {} has no STATUS_ID "
                        "name".format(index, what, status_id))
                names.append(name)
    return names


class Record(object):
    """One decomposed 32-byte record: raw bytes + the typed-surface names.

    Field extraction follows the LoadMonsterProp/LoadRageProp load comments
    (battle_main.asm:7307-7436, 7504-7550); the u16 fields (hp/mp/xp/gold)
    are ROM little-endian.
    """

    def __init__(self, index, raw, symbols):
        assert len(raw) == RECORD_SIZE
        self.index = index
        self.raw = list(raw)
        name = symbols.monster_names.get(index)
        if name is None:
            raise MonsterPropError(
                "record {:#05x} has no MONSTER name".format(index))
        self.name = name
        self.speed = raw[0]
        self.attack_power = raw[1]
        self.hit_rate = raw[2]
        self.evade = raw[3]
        self.magic_block = raw[4]
        self.defense = raw[5]
        self.magic_defense = raw[6]
        self.magic_power = raw[7]
        self.hp = raw[8] | (raw[9] << 8)
        self.mp = raw[10] | (raw[11] << 8)
        self.experience = raw[12] | (raw[13] << 8)
        self.gold = raw[14] | (raw[15] << 8)
        self.level = raw[16]
        self.metamorph_pack, self.metamorph_rate = \
            decompose_metamorph(raw[17], index)
        self.traits = _decompose_bits(raw[18], _TRAIT_BITS, "trait", index)
        self.battle_flags = _decompose_bits(raw[19], _BATTLE_BITS,
                                            "battle-flag", index)
        self.blocked_statuses = decompose_statuses(
            raw[20:23], symbols.status_names, 0, _BLOCKED_STATUS_MAX_ID,
            "blocked", index)
        self.absorb_elements = decompose_elements(
            raw[23], symbols.element_by_bit, "absorbed", index)
        self.nullify_elements = decompose_elements(
            raw[24], symbols.element_by_bit, "nullified", index)
        self.weak_elements = decompose_elements(
            raw[25], symbols.element_by_bit, "weak", index)
        self.attack_graphic = raw[26]
        self.attack_graphic_name = symbols.item_names.get(raw[26])
        if self.attack_graphic_name is None:
            raise MonsterPropError(
                "record {:#05x}: attack-graphic byte {:#04x} has no ITEM name"
                .format(index, raw[26]))
        self.innate_statuses = decompose_statuses(
            raw[27:31], symbols.status_names, 0, 31, "innate", index)
        (self.special_effect_class, self.special_cant_dodge,
         self.special_no_damage) = decompose_special_attack(raw[31], index)
        self.special_builder = special_attack_builder(
            self.special_effect_class, symbols.status_names, index)


def read_records(dat_path, symbols):
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != EXPECTED_LEN:
        raise MonsterPropError(
            "{}: {} bytes, expected {} (384 records x 32 — wrong artifact)"
            .format(dat_path, len(data), EXPECTED_LEN))
    return [Record(i, data[i * RECORD_SIZE:(i + 1) * RECORD_SIZE], symbols)
            for i in range(RECORD_COUNT)]


# --- rendering ---------------------------------------------------------------

# Emitted-file header: AUTO-GENERATED line, Source lines, upstream pin,
# DO-NOT-EDIT + exact regeneration command, then a consumption paragraph.
_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_monster_prop.py\n"
    "// Source: src/battle/monster_prop.dat (MonsterProp, ROM CF/0000,\n"
    "//         384 records x 32 bytes; layout per the LoadMonsterProp and\n"
    "//         LoadRageProp load comments in src/battle/battle_main.asm\n"
    "//         (:7307-7436, :7504-7550) and notes/battle-ram.txt:952-970)\n"
    "// Source: include/const.inc (MONSTER / ITEM / STATUS_ID / ELEMENT "
    "values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_monster_prop.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/monster_prop_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/monster_prop_expected.h\n"
    "\n"
)


def _of_or_empty(type_name, member_scope, names):
    if not names:
        return "{}{{}}".format(type_name)
    return "{}::of({})".format(
        type_name, ", ".join("{}::{}".format(member_scope, n) for n in names))


def _render_special_attack(rec):
    """The per-band builder call, with the modifier bits chained on as
    .withCantDodge() / .withNoDamage() when set."""
    call = rec.special_builder
    if rec.special_cant_dodge:
        call += ".withCantDodge()"
    if rec.special_no_damage:
        call += ".withNoDamage()"
    return call


def _render_row(rec):
    return (
        "    MonsterPropertiesEntry{{  // [${:03X}]\n"
        "        .id = MonsterId::{},\n"
        "        .record = MonsterProperties{{\n"
        "            .speed           = {},\n"
        "            .attackPower     = {},\n"
        "            .hitRate         = {},\n"
        "            .evade           = {},\n"
        "            .magicBlock      = {},\n"
        "            .defense         = {},\n"
        "            .magicDefense    = {},\n"
        "            .magicPower      = {},\n"
        "            .hp              = {},\n"
        "            .mp              = {},\n"
        "            .experience      = {},\n"
        "            .gold            = {},\n"
        "            .level           = {},\n"
        "            .metamorph       = MetamorphInfo::of({{ .packIndex = {},"
        " .rate = MetamorphRate::{} }}),\n"
        "            .traitFlags      = {},\n"
        "            .battleFlags     = {},\n"
        "            .blockedStatuses = {},\n"
        "            .absorbElements  = {},\n"
        "            .nullifyElements = {},\n"
        "            .weakElements    = {},\n"
        "            .attackGraphic   = ItemId::{},\n"
        "            .innateStatuses  = {},\n"
        "            .specialAttack   = {},\n"
        "        }},\n"
        "    }},\n"
    ).format(
        rec.index,
        rec.name,
        rec.speed, rec.attack_power, rec.hit_rate, rec.evade, rec.magic_block,
        rec.defense, rec.magic_defense, rec.magic_power,
        rec.hp, rec.mp, rec.experience, rec.gold,
        rec.level,
        rec.metamorph_pack, _METAMORPH_RATE_NAMES[rec.metamorph_rate],
        _of_or_empty("MonsterTraitFlags", "MonsterTraitFlag", rec.traits),
        _of_or_empty("MonsterBattleFlags", "MonsterBattleFlag",
                     rec.battle_flags),
        _of_or_empty("BlockedStatusSet", "StatusId", rec.blocked_statuses),
        _of_or_empty("ElementSet", "Element", rec.absorb_elements),
        _of_or_empty("ElementSet", "Element", rec.nullify_elements),
        _of_or_empty("ElementSet", "Element", rec.weak_elements),
        rec.attack_graphic_name,
        _of_or_empty("StatusSet", "StatusId", rec.innate_statuses),
        _render_special_attack(rec))


def render_inc(records):
    lines = [_HEADER_COMMON,
             "// MonsterPropertiesEntry rows in MONSTER index order\n"
             "// ($000..$17F), one designated-initializer row per record,\n"
             "// #included inside the kMonsterProperties array in\n"
             "// src/data/monster_properties.cpp. Each row's identity is its\n"
             "// .id field — the MonsterId enumerator; a compile-time assert\n"
             "// verifies id == position. The packed .record stays\n"
             "// byte-identical to the 32 ROM bytes (hp/mp/experience/gold\n"
             "// are the ROM's little-endian u16 values). Flag and status\n"
             "// bytes render through the of(...) builders so every set bit\n"
             "// is named; empty sets render as TypeName{}. Stats and u16\n"
             "// magnitudes are decimal; the metamorph byte renders through\n"
             "// its labeled fields (MetamorphInfo::of({ .packIndex = N,\n"
             "// .rate = MetamorphRate::X })) and the special-attack byte\n"
             "// through its per-band builder (inflictStatus / damageBoost /\n"
             "// drainHp / drainMp / removeReflect) with the modifier bits\n"
             "// chained as .withCantDodge() / .withNoDamage().\n\n"]
    for rec in records:
        lines.append(_render_row(rec))
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 32-byte monster_prop record; field names and order mirror the\n"
    "// LoadMonsterProp/LoadRageProp load comments (battle_main.asm:7307-7436,\n"
    "// :7504-7550), with the u16 fields split into their little-endian byte\n"
    "// pairs. Values are the exact ROM bytes — deliberately independent of\n"
    "// the typed-surface rows in monster_prop_data.inc, so decomposition or\n"
    "// builder drift in either artifact fails the full-corpus\n"
    "// byte-equivalence test.\n"
    "struct ExpectedMonsterRecord {\n"
    "    std::uint8_t speed;\n"
    "    std::uint8_t attackPower;\n"
    "    std::uint8_t hitRate;\n"
    "    std::uint8_t evade;\n"
    "    std::uint8_t magicBlock;\n"
    "    std::uint8_t defense;\n"
    "    std::uint8_t magicDefense;\n"
    "    std::uint8_t magicPower;\n"
    "    std::uint8_t hpLo, hpHi;\n"
    "    std::uint8_t mpLo, mpHi;\n"
    "    std::uint8_t experienceLo, experienceHi;\n"
    "    std::uint8_t goldLo, goldHi;\n"
    "    std::uint8_t level;\n"
    "    std::uint8_t metamorph;\n"
    "    std::uint8_t traitFlags;\n"
    "    std::uint8_t battleFlags;\n"
    "    std::uint8_t blockedStatus1, blockedStatus2, blockedStatus3;\n"
    "    std::uint8_t absorbElements;\n"
    "    std::uint8_t nullifyElements;\n"
    "    std::uint8_t weakElements;\n"
    "    std::uint8_t attackGraphic;\n"
    "    std::uint8_t innateStatus1, innateStatus2, innateStatus3, "
    "innateStatus4;\n"
    "    std::uint8_t specialAttack;\n"
    "};\n"
    "static_assert(sizeof(ExpectedMonsterRecord) == 32,\n"
    "              \"fixture record must stay byte-identical to a ROM "
    "monster_prop record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (raw decimal\n"
    "// index — the fixture stays independent of the port's MonsterId header)\n"
    "// alongside the raw record bytes. Mirrors\n"
    "// ostinato::MonsterPropertiesEntry without depending on it.\n"
    "struct ExpectedMonsterEntry {\n"
    "    std::uint16_t id;\n"
    "    ExpectedMonsterRecord record;\n"
    "};\n"
)


def _fixture_row(rec):
    h = ["0x{:02X}".format(b) for b in rec.raw]
    return (
        "    {{ .id = {:>3},  // ${:03X} {}\n"
        "      .record = {{ .speed = {}, .attackPower = {}, .hitRate = {},\n"
        "                  .evade = {}, .magicBlock = {}, .defense = {},\n"
        "                  .magicDefense = {}, .magicPower = {},\n"
        "                  .hpLo = {}, .hpHi = {}, .mpLo = {}, .mpHi = {},\n"
        "                  .experienceLo = {}, .experienceHi = {},"
        " .goldLo = {}, .goldHi = {},\n"
        "                  .level = {}, .metamorph = {}, .traitFlags = {},"
        " .battleFlags = {},\n"
        "                  .blockedStatus1 = {}, .blockedStatus2 = {},"
        " .blockedStatus3 = {},\n"
        "                  .absorbElements = {}, .nullifyElements = {},"
        " .weakElements = {},\n"
        "                  .attackGraphic = {},\n"
        "                  .innateStatus1 = {}, .innateStatus2 = {},"
        " .innateStatus3 = {}, .innateStatus4 = {},\n"
        "                  .specialAttack = {} }} }},\n"
    ).format(rec.index, rec.index, rec.name, *h)


def render_fixture(records):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_monster_properties.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture id == position, table id enumerator ==\n"
             "// position, and a 32-byte memcmp of the packed record against\n"
             "// src/data/generated/monster_prop_data.inc's row.\n"
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
             "inline constexpr std::array<ExpectedMonsterEntry, {}> "
             "kExpectedMonsterEntries = {{{{  // ROM MonsterProp\n"
             .format(len(records))]
    for rec in records:
        lines.append(_fixture_row(rec))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ------------------------------------------------------------------

def run(monster_prop_dat, const_inc, inc_out, fixture_out, check_only=False):
    symbols = Symbols(const_inc)
    records = read_records(monster_prop_dat, symbols)

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
    monster_prop = args.monster_prop_dat
    const_inc = args.const_inc
    if args.source_root:
        if not monster_prop:
            monster_prop = os.path.join(args.source_root, "src", "battle",
                                        "monster_prop.dat")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return monster_prop, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (monster_prop.dat + const.inc "
                         "resolved under it)")
    ap.add_argument("--monster-prop-dat", help="path to monster_prop.dat")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out",
                    default="src/data/generated/monster_prop_data.inc",
                    help="output path for the MonsterProperties rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/monster_prop_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    monster_prop, const_inc = _resolve(args)
    if not monster_prop or not const_inc:
        ap.error("provide --source-root, or both --monster-prop-dat and "
                 "--const-inc")
    try:
        return run(monster_prop, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except (ParseError, MonsterPropError) as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
