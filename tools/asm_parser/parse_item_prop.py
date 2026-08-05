#!/usr/bin/env python3
"""Emit the item-properties table from original-src item_prop_en.dat.

Port-time tooling (NOT a build/CI dependency). item_prop_en.dat is the raw
256-record x 30-byte item-properties table (ROM D8/5000) — item, weapon,
armor, and relic stats in one record. No RAM-map byte table documents the
record itself; the layout authority is the consumer access sites (the x30
stride in GetItemPropPtr, src/menu/item.asm:1001-1012 and
src/battle/battle_main.asm:7177-7180, plus the per-field reads cited in
src/data/item_properties.h) and the $11D2-$11DF cells CalcEquipEffect copies
bytes +5..+13 into (battle_main.asm:2480-2533, notes/battle-ram.txt:318-381).
This script reads the .dat straight off disk, decomposes every byte into the
port's typed surfaces, and emits:

  * src/data/generated/item_prop_en_data.inc — one designated-initializer
    ItemProperties row per record (256 records), every field labeled inline;
    the kItemPropertiesEn array #includes it.
  * tests/fixtures/item_prop_expected.h — the same 256 records as raw 30-byte
    rows (the ground-truth byte contract) for a full-corpus byte-equivalence
    test.

Symbol names (ITEM identity, CHAR equip slots, ATTACK spell names, TARGET
flags, STATUS_ID statuses, ELEMENT names, ITEM_TYPE / ITEM_USAGE values)
resolve against original-src/include/const.inc; the field-effect and relic
bytes at +5/+9..+13 have no upstream symbol source (their meanings live only
in the prose RAM map), so their bit->name tables below mirror
include/ostinato/item_effects.h — any drift is caught at compile + full-corpus
memcmp.

Fields +15 and +18..+27 change role by the record's ItemType (equipment vs
consumable readings). Rows carry equipment-primary member names, and the
role-overloaded bytes render role-shaped named surfaces: +18 decomposes into
its spell + SpellCastMode bits, +19 into WEAPON_FLAG names on weapons /
ItemUseFlag names on consumables (the two dead bits render their named
kDeadItemFlagBit0/kDeadItemFlagBit6 constants), and +27 into the weapon/block
equipment surface or the named ItemUseEffect.

Structural guarantees, hard-errored at emit time:
  * the .dat is exactly 7680 bytes (256 x 30 — any other length is the wrong
    artifact);
  * every decomposed byte reconstructs to the source byte (an undocumented
    field-effect / relic bit, a negative-zero stat nibble, targeting residue,
    an unnamed special-effect index, or a mode-less spell-cast byte is an
    escalation, not a guess);
  * every record index has an ITEM name, every equip slot a CHAR name, every
    spell byte an ATTACK name.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_item_prop.py --source-root PATH --inc-out FILE --fixture-out FILE
    parse_item_prop.py --item-prop-dat PATH --const-inc PATH \\
                       --inc-out FILE --fixture-out FILE
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import sys

import common
import parse_const_enums as pce
import parse_magic_prop as pmp
from common import ParseError

RECORD_COUNT = 256
RECORD_SIZE = 30
EXPECTED_LEN = RECORD_COUNT * RECORD_SIZE

PLAYABLE_SLOTS = 14      # equip-mask bits 0-13 are CHAR slots
EQUIP_IMP = 0x4000       # bit 14 — imp gear (battle_main.asm:2535-2541)
EQUIP_HEAVY = 0x8000     # bit 15 — heavy gear (equip.asm:2300-2307)

# --- bit->name tables (mirror include/ostinato/item_effects.h) ---

# Record byte +5 -> $11DF (battle-ram.txt:377-381).
_FIELD_EFFECT_BITS = (
    (0x01, "CHARM_BANGLE"),
    (0x02, "MOOGLE_CHARM"),
    (0x20, "SPRINT_SHOES"),
    (0x80, "TINTINABAR"),
)

# Record bytes +9..+13 -> $11D5..$11D9 (battle-ram.txt:326-367). Undocumented
# positions (relic 4 bit 7; relic 5 bits 5-6) are absent — a record using one
# hard-errors.
_RELIC1_BITS = (
    (0x01, "RAISE_FIGHT_DAMAGE"),
    (0x02, "RAISE_MAGIC_DAMAGE"),
    (0x04, "HP_PLUS_25"),
    (0x08, "HP_PLUS_50"),
    (0x10, "HP_PLUS_12_5"),
    (0x20, "MP_PLUS_25"),
    (0x40, "MP_PLUS_50"),
    (0x80, "MP_PLUS_12_5"),
)
_RELIC2_BITS = (
    (0x01, "RAISE_PREEMPTIVE_RATE"),
    (0x02, "PREVENT_BACK_PINCER"),
    (0x04, "FIGHT_TO_JUMP"),
    (0x08, "MAGIC_TO_X_MAGIC"),
    (0x10, "SKETCH_TO_CONTROL"),
    (0x20, "SLOT_TO_GP_RAIN"),
    (0x40, "STEAL_TO_CAPTURE"),
    (0x80, "CONTINUOUS_JUMP"),
)
_RELIC3_BITS = (
    (0x01, "RAISE_STEAL_RATE"),
    (0x02, "RAISE_MAGIC_DAMAGE"),
    (0x04, "RAISE_SKETCH_RATE"),
    (0x08, "RAISE_CONTROL_RATE"),
    (0x10, "HIT_100_IGNORE_MBLOCK"),
    (0x20, "MP_COST_50_PERCENT"),
    (0x40, "MP_COST_1"),
    (0x80, "RAISE_VIGOR_50_PERCENT"),
)
_RELIC4_BITS = (
    (0x01, "FIGHT_TO_X_FIGHT"),
    (0x02, "RANDOM_COUNTER"),
    (0x04, "RANDOM_EVADE"),
    (0x08, "USE_WEAPON_2_HANDED"),
    (0x10, "EQUIP_2_WEAPONS"),
    (0x20, "EQUIP_HEAVY_ITEMS"),
    (0x40, "PROTECT_WEAK_ALLIES"),
)
_RELIC5_BITS = (
    (0x01, "SHELL_WHEN_HP_LOW"),
    (0x02, "SAFE_WHEN_HP_LOW"),
    (0x04, "WALL_WHEN_HP_LOW"),
    (0x08, "DOUBLE_EXPERIENCE"),
    (0x10, "DOUBLE_GP"),
    (0x80, "MAKE_UNDEAD"),
)

# Byte +19 consumable-role bits (battle_main.asm:7035-7059 asl chain) —
# mirror ostinato::ItemUseFlag.
_ITEM_USE_FLAG_BITS = (
    (0x02, "INVERT_ON_UNDEAD"),
    (0x08, "RESTORES_HP"),
    (0x10, "RESTORES_MP"),
    (0x20, "REMOVES_STATUS"),
    (0x80, "FRACTIONAL_DAMAGE"),
)

# Byte +19 dead bits — no consumer in the tree (see the
# kDeadItemFlagBit0/kDeadItemFlagBit6 notes in src/data/item_properties.h).
# Each ports verbatim on its rows via its named constant.
_DEAD_ITEM_FLAG_BITS = {0x01: "kDeadItemFlagBit0", 0x40: "kDeadItemFlagBit6"}

# Byte +18 mode bits (CheckWeaponMagic battle_main.asm:8664-8673 for bit 6;
# InitTarget_01 :6525-6533 for bit 7) — mirror ostinato::SpellCastMode.
_SPELL_CAST_MODE_BITS = (
    (0x40, "RANDOM_ON_ATTACK"),
    (0x80, "CAST_ON_ITEM_USE"),
)

# Byte +27 equipment-role high nibble (battle_main.asm:6954-6957) — mirror
# ostinato::WeaponSpecialEffect; names from the per-index handler headers in
# battle_main.asm's special-effect jump tables.
_WEAPON_EFFECT_NAMES = {
    0x0: "NONE",
    0x1: "THIEFKNIFE",
    0x2: "ATMA_WEAPON",
    0x3: "INSTANT_KILL",
    0x4: "MAN_EATER",
    0x5: "DRAINER",
    0x6: "SOUL_SABRE",
    0x7: "MP_CRITICAL",
    0x8: "SNIPER_HAWKEYE",
    0x9: "DICE",
    0xA: "VALIANTKNIFE",
    0xB: "TEMPEST",
    0xC: "HEAL_ROD",
    0xD: "SCIMITAR_ZANTETSUKEN",
    0xE: "OGRE_NIX",
}

# Byte +27 consumable-role values (offset by $48 into the dispatch space,
# battle_main.asm:7024-7028) — mirror ostinato::ItemUseEffect.
_ITEM_USE_EFFECT_NAMES = {
    1: "MAGICITE",
    2: "SUPER_BALL",
    3: "SMOKE_BOMB",
    4: "ELIXIR",
    5: "WARP_STONE",
    6: "DRIED_MEAT",
}

# Byte +27 equipment-role low nibble (battle-ram.txt:298-302) — mirror
# ostinato::BlockGraphic / ostinato::BlockAbility.
_BLOCK_GRAPHIC_NAMES = {0: "DAGGER", 1: "SWORD", 2: "SHIELD", 3: "ZEPHYR_CAPE"}
_BLOCK_ABILITY_BITS = ((0x04, "PHYSICAL"), (0x08, "MAGIC"))

_ITEM_TYPE_CONSUMABLE = 6


class ItemPropError(Exception):
    pass


# --- symbol resolution -------------------------------------------------------

class Symbols(object):
    """The const.inc enums the decomposition resolves against."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        for enum_name in ("ITEM", "CHAR", "ATTACK", "TARGET", "STATUS_ID",
                          "ELEMENT", "ITEM_TYPE", "ITEM_USAGE",
                          "WEAPON_FLAG"):
            if self.parsed.enum(enum_name) is None:
                raise ParseError(const_inc, 0,
                                 "expected enum '{}' not found".format(enum_name))
        self.item_names = self._names_by_value("ITEM")
        self.char_names = self._names_by_value("CHAR")
        self.attack_names = self._names_by_value("ATTACK")
        self.status_names = self._names_by_value("STATUS_ID")
        self.element_by_bit = self._names_by_value("ELEMENT")
        self.type_names = self._names_by_value("ITEM_TYPE")
        self.usage_bits = tuple(
            (m.value, m.name)
            for m in self.parsed.enum("ITEM_USAGE").members)
        self.weapon_flag_bits = tuple(
            (m.value, m.name)
            for m in self.parsed.enum("WEAPON_FLAG").members)
        self.target = {m.name: m.value
                       for m in self.parsed.enum("TARGET").members}

    def _names_by_value(self, enum_name):
        # First declaration wins (trailing aliases like CHAR::KUPEK never
        # shadow the primary name).
        by_value = {}
        for m in self.parsed.enum(enum_name).members:
            by_value.setdefault(m.value, m.name)
        return by_value


# --- byte decomposition ------------------------------------------------------

def _decompose_bits(byte, bit_table, what, index):
    """Split a flag byte into its named bits; hard-error on undocumented bits."""
    names = []
    residue = byte
    for bit, name in bit_table:
        if byte & bit:
            names.append(name)
            residue &= ~bit
    if residue:
        raise ItemPropError(
            "record {:#04x}: {} byte {:#04x} uses undocumented bit(s) {:#04x} "
            "— escalate, never guess".format(index, what, byte, residue))
    return names


def decompose_type_usage(byte, symbols, index):
    """Byte +0: ItemType in the low 3 bits + ITEM_USAGE flags. Bits 3 and 7
    are unused across the corpus; a record setting one hard-errors."""
    type_value = byte & 0x07
    type_name = symbols.type_names.get(type_value)
    if type_name is None:
        raise ItemPropError(
            "record {:#04x}: item type {} has no ITEM_TYPE name"
            .format(index, type_value))
    usage_names = []
    residue = byte & ~0x07 & 0xFF
    for bit, name in symbols.usage_bits:
        if residue & bit:
            usage_names.append(name)
            residue &= ~bit
    if residue:
        raise ItemPropError(
            "record {:#04x}: type/usage byte {:#04x} uses unused bit(s) "
            "{:#04x} — escalate, never guess".format(index, byte, residue))
    return type_name, usage_names


def decompose_equip_mask(mask, symbols, index):
    """The 16-bit equip mask -> CHAR slot names (bits 0-13, ascending) plus
    the IMP / HEAVY special bits (bits 14/15)."""
    names = []
    for slot in range(PLAYABLE_SLOTS):
        if mask & (1 << slot):
            name = symbols.char_names.get(slot)
            if name is None:
                raise ItemPropError(
                    "record {:#04x}: equip slot {} has no CHAR name"
                    .format(index, slot))
            names.append(("CharacterId", name))
    if mask & EQUIP_IMP:
        names.append(("EquipSpecial", "IMP"))
    if mask & EQUIP_HEAVY:
        names.append(("EquipSpecial", "HEAVY"))
    packed = 0
    for scope, name in names:
        if scope == "CharacterId":
            packed |= 1 << [s for s in range(PLAYABLE_SLOTS)
                            if symbols.char_names.get(s) == name][0]
        else:
            packed |= EQUIP_IMP if name == "IMP" else EQUIP_HEAVY
    if packed != mask:
        raise ItemPropError(
            "record {:#04x}: equip mask decomposition round-trip {:#06x} != "
            "source {:#06x}".format(index, packed, mask))
    return names


def decompose_stat_pair(byte, what, index):
    """A nibble-packed stat-boost byte -> (first, second) signed boosts.

    Bit 3 of a nibble is the sign; $9..$F decode to -1..-7. The $8
    negative-zero nibble decodes to 0 but re-packs to $0, so it cannot
    round-trip through StatBoostPair::of — the corpus never uses it, and a
    record that does hard-errors here.
    """
    def decode(nibble):
        if nibble == 0x8:
            raise ItemPropError(
                "record {:#04x}: {} byte {:#04x} carries the negative-zero "
                "nibble $8, which StatBoostPair::of cannot round-trip — "
                "escalate, never guess".format(index, what, byte))
        return -(nibble & 0x7) if nibble & 0x8 else nibble

    return decode(byte & 0x0F), decode(byte >> 4)


def decompose_status_slice(byte, byte_index, symbols, index):
    """A one-byte status slice -> StatusId names (id = byte_index*8 + bit)."""
    names = []
    for bit_pos in range(8):
        if byte & (1 << bit_pos):
            status_id = byte_index * 8 + bit_pos
            name = symbols.status_names.get(status_id)
            if name is None:
                raise ItemPropError(
                    "record {:#04x}: status id {} has no STATUS_ID name"
                    .format(index, status_id))
            names.append(name)
    return names


def decompose_item_flags(byte, type_value, symbols, index):
    """Byte +19, role-shaped by the record's item type.

    Weapon rows decompose over the WEAPON_FLAG space; non-weapon rows carry
    either the dead bit 0 (three defensive items; no consumer in the tree)
    or the consumable-role ItemUseFlag bits. Returns a render decision:
    ("empty",), ("dead",), ("weapon", names) or ("item_use", names).
    """
    if byte == 0:
        return ("empty",)
    if type_value == 1:  # weapon
        names = []
        residue = byte
        for bit, name in symbols.weapon_flag_bits:
            if byte & bit:
                names.append(name)
                residue &= ~bit
        if residue:
            raise ItemPropError(
                "record {:#04x}: weapon flags byte {:#04x} uses bit(s) "
                "{:#04x} outside the WEAPON_FLAG space — escalate, never "
                "guess".format(index, byte, residue))
        return ("weapon", names)
    if byte in _DEAD_ITEM_FLAG_BITS:
        return ("dead", _DEAD_ITEM_FLAG_BITS[byte])
    names = _decompose_bits(byte, _ITEM_USE_FLAG_BITS, "item-use flags",
                            index)
    return ("item_use", names)


def decompose_spell_cast(byte, symbols, index):
    """Byte +18: None for zero, else (attack_name, mode_names). A nonzero
    byte without a mode bit has no consumer semantics — hard error."""
    if byte == 0:
        return None
    modes = [name for bit, name in _SPELL_CAST_MODE_BITS if byte & bit]
    if not modes:
        raise ItemPropError(
            "record {:#04x}: spell-cast byte {:#04x} has no mode bit — "
            "escalate, never guess".format(index, byte))
    spell = byte & 0x3F
    name = symbols.attack_names.get(spell)
    if name is None:
        raise ItemPropError(
            "record {:#04x}: spell-cast spell {:#04x} has no ATTACK name"
            .format(index, spell))
    return (name, modes)


def decompose_special_effect(byte, type_value, index):
    """Byte +27, role-shaped by the record's item type.

    Consumables carry an item-use effect ($00 none, $FF disabled, else a
    named ItemUseEffect); equipment packs the weapon special-effect index in
    the high nibble and the block info in the low nibble. Returns a render
    decision: ("none",), ("disabled",), ("item_use", name),
    ("weapon", effect) or ("equipment", effect, graphic, ability_names).
    """
    if byte == 0:
        return ("none",)
    if type_value == _ITEM_TYPE_CONSUMABLE:
        if byte == 0xFF:
            return ("disabled",)
        name = _ITEM_USE_EFFECT_NAMES.get(byte)
        if name is None:
            raise ItemPropError(
                "record {:#04x}: item-use effect {:#04x} has no name — "
                "escalate, never guess".format(index, byte))
        return ("item_use", name)
    effect = _WEAPON_EFFECT_NAMES.get(byte >> 4)
    if effect is None:
        raise ItemPropError(
            "record {:#04x}: weapon special effect {:#x} has no name — "
            "escalate, never guess".format(index, byte >> 4))
    low = byte & 0x0F
    if low == 0:
        return ("weapon", effect)
    graphic = _BLOCK_GRAPHIC_NAMES[low & 0x03]
    abilities = [name for bit, name in _BLOCK_ABILITY_BITS if low & bit]
    return ("equipment", effect, graphic, abilities)


class Record(object):
    """One decomposed 30-byte record: raw bytes + the typed-surface names."""

    def __init__(self, index, raw, symbols):
        assert len(raw) == RECORD_SIZE
        self.index = index
        self.raw = list(raw)
        name = symbols.item_names.get(index)
        if name is None:
            raise ItemPropError(
                "record {:#04x} has no ITEM name".format(index))
        self.name = name
        self.type_value = raw[0] & 0x07
        self.type_name, self.usage = decompose_type_usage(raw[0], symbols,
                                                          index)
        self.equip = decompose_equip_mask(raw[1] | (raw[2] << 8), symbols,
                                          index)
        self.spell_learn_rate = raw[3]
        self.spell_learned = symbols.attack_names.get(raw[4])
        if self.spell_learned is None:
            raise ItemPropError(
                "record {:#04x}: spell byte {:#04x} has no ATTACK name"
                .format(index, raw[4]))
        self.field_effects = _decompose_bits(raw[5], _FIELD_EFFECT_BITS,
                                             "field-effects", index)
        self.status1_protection = decompose_status_slice(raw[6], 0, symbols,
                                                         index)
        self.status2_protection = decompose_status_slice(raw[7], 1, symbols,
                                                         index)
        self.status3_granted = decompose_status_slice(raw[8], 2, symbols,
                                                      index)
        self.relic1 = _decompose_bits(raw[9], _RELIC1_BITS, "relic-1", index)
        self.relic2 = _decompose_bits(raw[10], _RELIC2_BITS, "relic-2", index)
        self.relic3 = _decompose_bits(raw[11], _RELIC3_BITS, "relic-3", index)
        self.relic4 = _decompose_bits(raw[12], _RELIC4_BITS, "relic-4", index)
        self.relic5 = _decompose_bits(raw[13], _RELIC5_BITS, "relic-5", index)
        try:
            self.targeting = pmp.decompose_targeting(raw[14], symbols.target,
                                                     index)
        except pmp.MagicPropError as exc:
            raise ItemPropError(str(exc))
        self.element = pmp.decompose_elements(raw[15], symbols.element_by_bit,
                                              index)
        self.vigor_speed = decompose_stat_pair(raw[16], "vigor/speed", index)
        self.stamina_magic = decompose_stat_pair(raw[17], "stamina/mag.pwr",
                                                 index)
        self.spell_cast = decompose_spell_cast(raw[18], symbols, index)
        self.item_flags = decompose_item_flags(raw[19], self.type_value,
                                               symbols, index)
        self.power = raw[20]
        self.hit_rate_or_defense = raw[21]
        self.elements_absorbed = pmp.decompose_elements(
            raw[22], symbols.element_by_bit, index)
        self.elements_nullified = pmp.decompose_elements(
            raw[23], symbols.element_by_bit, index)
        self.elements_weak = pmp.decompose_elements(
            raw[24], symbols.element_by_bit, index)
        self.status2_set = decompose_status_slice(raw[25], 1, symbols, index)
        self.evade_index = raw[26] & 0x0F
        self.mblock_index = raw[26] >> 4
        self.special = decompose_special_effect(raw[27], self.type_value,
                                                index)
        self.price = raw[28] | (raw[29] << 8)


def read_records(dat_path, symbols):
    with open(dat_path, "rb") as fh:
        data = fh.read()
    if len(data) != EXPECTED_LEN:
        raise ItemPropError(
            "{}: {} bytes, expected {} (256 records x 30 — wrong artifact)"
            .format(dat_path, len(data), EXPECTED_LEN))
    return [Record(i, data[i * RECORD_SIZE:(i + 1) * RECORD_SIZE], symbols)
            for i in range(RECORD_COUNT)]


# --- rendering ---------------------------------------------------------------

# Emitted-file header: AUTO-GENERATED line, Source lines, upstream pin,
# DO-NOT-EDIT + exact regeneration command, then a consumption paragraph.
_HEADER_COMMON = (
    "// AUTO-GENERATED by tools/asm_parser/parse_item_prop.py\n"
    "// Source: src/menu/item_prop_en.dat (ItemProp, ROM D8/5000,\n"
    "//         256 records x 30 bytes; layout per the consumer access sites\n"
    "//         cited in src/data/item_properties.h and the $11D2-$11DF cells\n"
    "//         of notes/battle-ram.txt:318-381)\n"
    "// Source: include/const.inc (ITEM / CHAR / ATTACK / TARGET / STATUS_ID /\n"
    "//         ELEMENT / ITEM_TYPE / ITEM_USAGE / WEAPON_FLAG values)\n"
    "// (original-src pinned at 1ea47b5)\n"
    "// DO NOT EDIT BY HAND — regenerate via:\n"
    "//   python3 tools/asm_parser/parse_item_prop.py \\\n"
    "//       --source-root  original-src \\\n"
    "//       --inc-out      src/data/generated/item_prop_en_data.inc \\\n"
    "//       --fixture-out  tests/fixtures/item_prop_expected.h\n"
    "\n"
)

_LINE_WIDTH = 78


def _wrap_call(prefix, tokens, indent):
    """Render prefix(token, token, ...) wrapped at _LINE_WIDTH with the
    continuation lines aligned under the opening parenthesis."""
    single = "{}({})".format(prefix, ", ".join(tokens))
    if len(indent) + len(single) <= _LINE_WIDTH:
        return single
    cont_indent = " " * (len(indent) + len(prefix) + 1)
    lines = []
    current = "{}(".format(prefix)
    for i, token in enumerate(tokens):
        piece = token + ("," if i + 1 < len(tokens) else ")")
        if current.endswith("("):
            candidate = current + piece
        else:
            candidate = current + " " + piece
        if len(indent) + len(candidate) > _LINE_WIDTH and not current.endswith("("):
            lines.append(current)
            current = cont_indent.removeprefix(indent) + piece
        else:
            current = candidate
    lines.append(current)
    return "\n{}".format(indent).join(lines)


def _of_or_empty(type_name, member_scope, names, indent):
    if not names:
        return "{}{{}}".format(type_name)
    return _wrap_call("{}::of".format(type_name),
                      ["{}::{}".format(member_scope, n) for n in names],
                      indent)


def _render_row(rec):
    indent = " " * 12
    type_usage = _wrap_call(
        "ItemTypeUsage::of",
        ["ItemType::{}".format(rec.type_name)] +
        ["ItemUsage::{}".format(n) for n in rec.usage],
        indent)
    equip = ("EquipPermissions{}" if not rec.equip else
             _wrap_call("EquipPermissions::of",
                        ["{}::{}".format(scope, n) for scope, n in rec.equip],
                        indent))
    targeting = ("Targeting{}" if not rec.targeting else
                 _wrap_call("Targeting::of",
                            ["TargetFlags::{}".format(n)
                             for n in rec.targeting],
                            indent))
    flags_kind = rec.item_flags[0]
    if flags_kind == "empty":
        weapon = "WeaponFlagSet{}"
    elif flags_kind == "dead":
        weapon = "WeaponFlagSet{{.bits = {}}}".format(rec.item_flags[1])
    elif flags_kind == "weapon":
        weapon = _of_or_empty(
            "WeaponFlagSet", "WeaponFlags", rec.item_flags[1], indent)
    else:  # item_use
        weapon = _wrap_call(
            "itemUseFlags",
            ["ItemUseFlag::{}".format(n) for n in rec.item_flags[1]],
            indent)
    if rec.spell_cast is None:
        spell_cast = "ItemSpellCast{}"
    else:
        spell_name, modes = rec.spell_cast
        spell_cast = _wrap_call(
            "ItemSpellCast::of",
            ["AttackId::{}".format(spell_name)] +
            ["SpellCastMode::{}".format(m) for m in modes],
            indent)
    special_kind = rec.special[0]
    if special_kind == "none":
        special = "ItemSpecialEffect{}"
    elif special_kind == "disabled":
        special = "ItemSpecialEffect::disabled()"
    elif special_kind == "item_use":
        special = "ItemSpecialEffect::itemUse(ItemUseEffect::{})".format(
            rec.special[1])
    elif special_kind == "weapon":
        special = "ItemSpecialEffect::weapon(WeaponSpecialEffect::{})".format(
            rec.special[1])
    else:  # equipment
        _, effect, graphic, abilities = rec.special
        special = _wrap_call(
            "ItemSpecialEffect::equipment",
            ["WeaponSpecialEffect::{}".format(effect),
             "BlockGraphic::{}".format(graphic)] +
            ["BlockAbility::{}".format(a) for a in abilities],
            indent)
    return (
        "    ItemPropertiesEntry{{  // [${:02X}]\n"
        "        .id = ItemId::{},\n"
        "        .record = ItemProperties{{\n"
        "            .typeAndUsage      = {},\n"
        "            .equippableBy      = {},\n"
        "            .spellLearnRate    = {},\n"
        "            .spellLearned      = AttackId::{},\n"
        "            .fieldEffects      = {},\n"
        "            .status1Protection = {},\n"
        "            .status2Protection = {},\n"
        "            .status3Granted    = {},\n"
        "            .relicEffects1     = {},\n"
        "            .relicEffects2     = {},\n"
        "            .relicEffects3     = {},\n"
        "            .relicEffects4     = {},\n"
        "            .relicEffects5     = {},\n"
        "            .targeting         = {},\n"
        "            .element           = {},\n"
        "            .vigorSpeed        = StatBoostPair::of({}, {}),\n"
        "            .staminaMagicPower = StatBoostPair::of({}, {}),\n"
        "            .spellCast         = {},\n"
        "            .weaponFlags       = {},\n"
        "            .power             = {},\n"
        "            .hitRateOrDefense  = {},\n"
        "            .elementsAbsorbed  = {},\n"
        "            .elementsNullified = {},\n"
        "            .elementsWeak      = {},\n"
        "            .status2Set        = {},\n"
        "            .evadeMagicBlock   = EvadeBlockPair::of({}, {}),\n"
        "            .specialEffect     = {},\n"
        "            .price             = {},\n"
        "        }},\n"
        "    }},\n"
    ).format(
        rec.index,
        rec.name,
        type_usage,
        equip,
        rec.spell_learn_rate,
        rec.spell_learned,
        _of_or_empty("FieldEffectSet", "FieldEffect", rec.field_effects,
                     indent),
        _of_or_empty("Status1Set", "StatusId", rec.status1_protection, indent),
        _of_or_empty("Status2Set", "StatusId", rec.status2_protection, indent),
        _of_or_empty("Status3Set", "StatusId", rec.status3_granted, indent),
        _of_or_empty("RelicEffect1Set", "RelicEffect1", rec.relic1, indent),
        _of_or_empty("RelicEffect2Set", "RelicEffect2", rec.relic2, indent),
        _of_or_empty("RelicEffect3Set", "RelicEffect3", rec.relic3, indent),
        _of_or_empty("RelicEffect4Set", "RelicEffect4", rec.relic4, indent),
        _of_or_empty("RelicEffect5Set", "RelicEffect5", rec.relic5, indent),
        targeting,
        _of_or_empty("ElementSet", "Element", rec.element, indent),
        rec.vigor_speed[0], rec.vigor_speed[1],
        rec.stamina_magic[0], rec.stamina_magic[1],
        spell_cast,
        weapon,
        rec.power,
        rec.hit_rate_or_defense,
        _of_or_empty("ElementSet", "Element", rec.elements_absorbed, indent),
        _of_or_empty("ElementSet", "Element", rec.elements_nullified, indent),
        _of_or_empty("ElementSet", "Element", rec.elements_weak, indent),
        _of_or_empty("Status2Set", "StatusId", rec.status2_set, indent),
        rec.evade_index, rec.mblock_index,
        special,
        rec.price)


def render_inc(records):
    lines = [_HEADER_COMMON,
             "// ItemPropertiesEntry rows in ITEM index order ($00..$FF), one\n"
             "// designated-initializer row per record, #included inside the\n"
             "// kItemPropertiesEn array in src/data/item_properties.cpp. Each\n"
             "// row's identity is its .id field — the ItemId enumerator; a\n"
             "// compile-time assert verifies id == position. The packed\n"
             "// .record stays byte-identical to the 30 ROM bytes. Every\n"
             "// value renders through a named surface: flag bytes and the\n"
             "// packed wrappers use their of(...) builders (empty sets are\n"
             "// TypeName{}), the role-overloaded bytes +18/+19/+27 render\n"
             "// per the record's ItemType (weapon flags vs itemUseFlags;\n"
             "// weapon/equipment vs itemUse special effects; dead +19 bits\n"
             "// render their named kDeadItemFlagBit constants), and\n"
             "// spellLearnRate / power / hitRateOrDefense / price and\n"
             "// the stat-boost and\n"
             "// evade/mblock builder arguments are decimal semantic\n"
             "// magnitudes. The per-ItemType role tables live in\n"
             "// docs/contracts/item-shop-data.md.\n\n"]
    for rec in records:
        lines.append(_render_row(rec))
    return "".join(lines)


_FIXTURE_STRUCT = (
    "// One raw 30-byte item_prop record; field names and order mirror the\n"
    "// src/data/item_properties.h record layout (the 16-bit equip mask and\n"
    "// price appear as their little-endian byte pairs). Values are the exact\n"
    "// ROM bytes — deliberately independent of the typed-surface rows in\n"
    "// item_prop_en_data.inc, so decomposition/builder drift in either\n"
    "// artifact fails the full-corpus byte-equivalence test.\n"
    "struct ExpectedItemRecord {\n"
    "    std::uint8_t typeAndUsage;\n"
    "    std::uint8_t equippableByLo, equippableByHi;\n"
    "    std::uint8_t spellLearnRate;\n"
    "    std::uint8_t spellLearned;\n"
    "    std::uint8_t fieldEffects;\n"
    "    std::uint8_t status1Protection;\n"
    "    std::uint8_t status2Protection;\n"
    "    std::uint8_t status3Granted;\n"
    "    std::uint8_t relicEffects1;\n"
    "    std::uint8_t relicEffects2;\n"
    "    std::uint8_t relicEffects3;\n"
    "    std::uint8_t relicEffects4;\n"
    "    std::uint8_t relicEffects5;\n"
    "    std::uint8_t targeting;\n"
    "    std::uint8_t element;\n"
    "    std::uint8_t vigorSpeed;\n"
    "    std::uint8_t staminaMagicPower;\n"
    "    std::uint8_t spellCast;\n"
    "    std::uint8_t weaponFlags;\n"
    "    std::uint8_t power;\n"
    "    std::uint8_t hitRateOrDefense;\n"
    "    std::uint8_t elementsAbsorbed;\n"
    "    std::uint8_t elementsNullified;\n"
    "    std::uint8_t elementsWeak;\n"
    "    std::uint8_t status2Set;\n"
    "    std::uint8_t evadeMagicBlock;\n"
    "    std::uint8_t specialEffect;\n"
    "    std::uint8_t priceLo, priceHi;\n"
    "};\n"
    "static_assert(sizeof(ExpectedItemRecord) == 30,\n"
    "              \"fixture record must stay byte-identical to a ROM item_prop"
    " record\");\n"
    "\n"
    "// One fixture entry: the record's identity as a typed field (raw decimal\n"
    "// index — the fixture stays independent of the port's ItemId header)\n"
    "// alongside the raw record bytes. Mirrors ostinato::ItemPropertiesEntry\n"
    "// without depending on it.\n"
    "struct ExpectedItemEntry {\n"
    "    std::uint8_t id;\n"
    "    ExpectedItemRecord record;\n"
    "};\n"
)


def _fixture_row(rec):
    h = ["0x{:02X}".format(b) for b in rec.raw]
    return (
        "    {{ .id = {:>3},  // ${:02X} {}\n"
        "      .record = {{ .typeAndUsage = {}, .equippableByLo = {},\n"
        "                  .equippableByHi = {}, .spellLearnRate = {},\n"
        "                  .spellLearned = {}, .fieldEffects = {},\n"
        "                  .status1Protection = {}, .status2Protection = {},\n"
        "                  .status3Granted = {}, .relicEffects1 = {},\n"
        "                  .relicEffects2 = {}, .relicEffects3 = {},\n"
        "                  .relicEffects4 = {}, .relicEffects5 = {},\n"
        "                  .targeting = {}, .element = {}, .vigorSpeed = {},\n"
        "                  .staminaMagicPower = {}, .spellCast = {},\n"
        "                  .weaponFlags = {}, .power = {},\n"
        "                  .hitRateOrDefense = {}, .elementsAbsorbed = {},\n"
        "                  .elementsNullified = {}, .elementsWeak = {},\n"
        "                  .status2Set = {}, .evadeMagicBlock = {},\n"
        "                  .specialEffect = {}, .priceLo = {},"
        " .priceHi = {} }} }},\n"
    ).format(rec.index, rec.index, rec.name, *h)


def render_fixture(records):
    lines = [_HEADER_COMMON,
             "// Test fixture for tests/test_item_properties.cpp — the\n"
             "// ground-truth record bytes. The full-corpus test asserts, per\n"
             "// entry: fixture id == position, table id enumerator ==\n"
             "// position, and a 30-byte memcmp of the packed record against\n"
             "// src/data/generated/item_prop_en_data.inc's row.\n"
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
             "inline constexpr std::array<ExpectedItemEntry, {}> "
             "kExpectedItemEntries = {{{{  // ROM ItemProp (EN)\n"
             .format(len(records))]
    for rec in records:
        lines.append(_fixture_row(rec))
    lines.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(lines)


# --- driver ------------------------------------------------------------------

def run(item_prop_dat, const_inc, inc_out, fixture_out, check_only=False):
    symbols = Symbols(const_inc)
    records = read_records(item_prop_dat, symbols)

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
    item_prop = args.item_prop_dat
    const_inc = args.const_inc
    if args.source_root:
        if not item_prop:
            item_prop = os.path.join(args.source_root, "src", "menu",
                                     "item_prop_en.dat")
        if not const_inc:
            const_inc = os.path.join(args.source_root, "include", "const.inc")
    return item_prop, const_inc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source-root",
                    help="disassembly root (item_prop_en.dat + const.inc "
                         "resolved under it)")
    ap.add_argument("--item-prop-dat", help="path to item_prop_en.dat")
    ap.add_argument("--const-inc", help="path to const.inc")
    ap.add_argument("--inc-out",
                    default="src/data/generated/item_prop_en_data.inc",
                    help="output path for the ItemProperties rows")
    ap.add_argument("--fixture-out",
                    default="tests/fixtures/item_prop_expected.h",
                    help="output path for the raw-byte fixture")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)

    item_prop, const_inc = _resolve(args)
    if not item_prop or not const_inc:
        ap.error("provide --source-root, or both --item-prop-dat and "
                 "--const-inc")
    try:
        return run(item_prop, const_inc, args.inc_out, args.fixture_out,
                   check_only=args.check_only)
    except (ParseError, ItemPropError, pmp.MagicPropError) as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
