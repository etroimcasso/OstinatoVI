#!/usr/bin/env python3
"""Emit the fixed-record attack/weapon/item animation tables from original-src.

Port-time tooling (NOT a build/CI dependency). Reads five rip .dat binaries plus
the inline ItemAnimPtrs word table in btlgfx_main.asm and emits the port's typed
surfaces for each. All record layouts are pinned by the btlgfx loaders:

  * AttackAnimProp        attack_anim_prop_en.dat  406 x 14 B  (InitAnimProp,
                          btlgfx_main.asm:23567-23652)
  * AttackGfxProp         attack_gfx_prop_en.dat   650 x 6 B   (LoadAnimGfxProp,
                          :24224-24242; LoadAnimGfx bit15=2bpp, :24304)
  * WeaponAnimProp        weapon_anim_prop.dat     93 x 8 B    (InitWeaponAnim,
                          :23661-23735)
  * MonsterAttackAnimProp monster_attack_anim_prop.dat 35 x 8 B (same record)
  * ItemJumpThrowAnim     item_jump_throw_anim.dat 257 x 1 B   (CmdAnim_08/16,
                          :27469-27498 name every jump/throw class)
  * ItemAnimPtrs          btlgfx_main.asm:48980-49012, 32 words (CmdAnim_01,
                          :27959-27972; each is row*14, $ffff = none)

Symbol names (ITEM identity for weapons / usable items / item rows) resolve
against original-src/include/const.inc; the 35 MonsterAttackAnimation names are
the corpus-derived enumerators shared with parse_monster_special_anim.

Structural guarantees, hard-errored at emit time:
  * each .dat length is an exact multiple of its record width;
  * AttackGfxProp frame-data indices are < 2948 (the AttackAnimFrames count);
  * the weapon/monster record's trailing pad byte is 0 on every row;
  * the weapon/monster thrown byte's low-7 weapon-animation type is 0-4;
  * ItemAnimPtrs holds exactly 32 words, each $ffff or a multiple of 14 whose
    quotient is a valid AttackAnimProp row (< 406);
  * every ITEM / MonsterAttackAnimation identity resolves to a name.

Python 3 standard library only; targets 3.9+.

Usage:
    parse_anim_prop.py --source-root PATH --repo-root PATH
    (Pass --check-only to validate + assert without writing files.)
"""

from __future__ import annotations

import argparse
import os
import sys

import common
import parse_const_enums as pce
import parse_monster_special_anim as psa
from common import ParseError

ATTACK_ANIM_ROWS = 406
ATTACK_ANIM_WIDTH = 14
ATTACK_GFX_ROWS = 650
ATTACK_GFX_WIDTH = 6
WEAPON_ROWS = 93
MONSTER_ATTACK_ROWS = 35
WEAPON_WIDTH = 8
ITEM_JUMP_THROW_ROWS = 257       # row 0 unarmed + 256 items
ITEM_ANIM_PTR_WORDS = 32
USABLE_ITEM_FIRST = 0xE0         # ItemAnimPtrs row i -> ITEM id 0xE0 + i

FRAME_COUNT = 2948               # AttackAnimFrames entry count (frameDataIndex <)
ANIM_PTR_MULTIPLIER = 14         # ItemAnimPtrs stores row*14

_ITEM_ANIM_PTRS_LABEL = "ItemAnimPtrs:"

# The 8 jump-animation and 16 throw-animation class enumerators, matching
# include/ostinato/item_throw_animation.h (names from btlgfx_main.asm:27474-27498).
_JUMP_CLASS_NAMES = (
    "UNARMED", "THICK_KNIFE", "THIN_KNIFE", "SWORD", "KATANA", "ROD", "SPEAR",
    "HAWK_EYE_SNIPER",
)
_THROW_CLASS_NAMES = (
    "THICK_KNIFE", "THIN_KNIFE", "SWORD", "KATANA", "ROD", "SPEAR",
    "HAWK_EYE_SNIPER", "UNKNOWN_07", "FIRE_SKEAN", "WATER_EDGE", "BOLT_EDGE",
    "INVIZ_EDGE", "SHADOW_EDGE", "FULL_MOON_MORNING_STAR_RISING_SUN",
    "BOOMERANG", "UNKNOWN_0F",
)

# The 5 low-7-bit weapon-animation types of the thrown byte (+5), matching
# include/ostinato/thrown_animation_flags.h. Only type 1 is named upstream
# (btlgfx_main.asm:28469); the corpus uses 0-4.
_WEAPON_ANIM_TYPE_NAMES = (
    "UNKNOWN_0", "STAR_OR_GAMBLER", "UNKNOWN_2", "UNKNOWN_3", "UNKNOWN_4",
)


class Symbols(object):
    """The const.inc ITEM enum + the shared MonsterAttackAnimation names."""

    def __init__(self, const_inc):
        self.parsed = common.parse_ca65_constants(const_inc,
                                                  skip_body_enums=pce.SKIP)
        if self.parsed.enum("ITEM") is None:
            raise ParseError(const_inc, 0, "expected enum 'ITEM' not found")
        self.item_names = {}
        for m in self.parsed.enum("ITEM").members:
            self.item_names.setdefault(m.value, m.name)
        self.monster_anim_names = psa._ANIMATION_NAMES
        if len(self.monster_anim_names) != MONSTER_ATTACK_ROWS:
            raise ParseError(const_inc, 0,
                             "MonsterAttackAnimation name count {} != {}"
                             .format(len(self.monster_anim_names),
                                     MONSTER_ATTACK_ROWS))

    def item(self, value):
        name = self.item_names.get(value)
        if name is None:
            raise ParseError("<const.inc>", 0,
                             "item id {:#04x} has no ITEM name".format(value))
        return name


# --- byte helpers ------------------------------------------------------------

def _word(data, offset):
    return data[offset] | (data[offset + 1] << 8)


def _read_dat(path, width, rows):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) != width * rows:
        raise ParseError(path, 0,
                         "{} bytes, expected {} ({} x {} — wrong artifact)"
                         .format(len(data), width * rows, rows, width))
    return data


# --- typed-surface emitters --------------------------------------------------

def _anim_ref(word):
    if word == 0xFFFF:
        return "AnimationRef::NONE"
    if word & 0x8000:
        return "AnimationRef::withHighBit({})".format(word & 0x7FFF)
    return "AnimationRef::of({})".format(word)


def _tile_offset(word):
    if word & 0x8000:
        return "AnimationTileOffset::of2bpp({})".format(word & 0x7FFF)
    return "AnimationTileOffset::of({})".format(word)


def _init_function(byte):
    if byte & 0x80:
        return "AnimationInitFunction::withHighBit({})".format(byte & 0x7F)
    return "AnimationInitFunction::of({})".format(byte)


def _thrown(byte):
    type_name = _WEAPON_ANIM_TYPE_NAMES[byte & 0x7F]
    builder = "thrown" if byte & 0x80 else "of"
    return "ThrownAnimationFlags::{}(WeaponAnimationType::{})".format(
        builder, type_name)


def _item_throw(byte):
    jump = _JUMP_CLASS_NAMES[(byte >> 4) & 0x07]
    thrown = _THROW_CLASS_NAMES[byte & 0x0F]
    builder = "fightAnimation" if byte & 0x80 else "of"
    return ("ItemThrowAnimation::{}(JumpAnimationClass::{}, "
            "ThrowAnimationClass::{})".format(builder, jump, thrown))


# --- banner ------------------------------------------------------------------

def _banner(source_lines):
    body = "".join("// Source: {}\n".format(s) for s in source_lines)
    return ("// AUTO-GENERATED by tools/asm_parser/parse_anim_prop.py\n"
            + body +
            "// (original-src pinned at 1ea47b5)\n"
            "// DO NOT EDIT BY HAND — regenerate via:\n"
            "//   python3 tools/asm_parser/parse_anim_prop.py \\\n"
            "//       --source-root original-src --repo-root .\n\n")


def _fixture_head(struct_text):
    return ("#pragma once\n\n"
            "#include <array>\n"
            "#include <cstdint>\n\n"
            "namespace ostinato::test {\n\n" + struct_text + "\n")


# --- AttackAnimProp ----------------------------------------------------------

def read_attack_anim(path):
    # The three animation words (sprite/bg1/bg3) and the special-graphics word
    # index btlgfx tables (LoadAnimGfxProp at InitAnimProp:23588/23603/23629);
    # range-validating each index against its target table is a cross-table
    # check for when those tables are modelled (s2 / Phase 4). This layer's
    # guarantee is byte identity via the full-corpus memcmp, so no numeric bound
    # is asserted on the words here beyond the $ffff = none sentinel handling.
    data = _read_dat(path, ATTACK_ANIM_WIDTH, ATTACK_ANIM_ROWS)
    return [data[i * ATTACK_ANIM_WIDTH:(i + 1) * ATTACK_ANIM_WIDTH]
            for i in range(ATTACK_ANIM_ROWS)]


def render_attack_anim_inc(rows):
    out = [_banner(["btlgfx_main.asm:48939 (AttackAnimProp, ROM d0/7fb2, EN "
                    "variant)"]),
           "// AttackAnimationPropertiesEntry rows in table order, #included\n"
           "// inside the kAttackAnimationProperties array in\n"
           "// src/data/attack_animations.cpp. Identity is the decimal .index\n"
           "// (the 406-row space has no symbolic names); a compile-time assert\n"
           "// verifies index == position. Animation words render through\n"
           "// AnimationRef (NONE / of / withHighBit); the init byte through\n"
           "// AnimationInitFunction; palettes, sound, and delay are decimal.\n\n"]
    for i, r in enumerate(rows):
        out.append(
            "    AttackAnimationPropertiesEntry{{  // [${:03X}]\n"
            "        .index = {},\n"
            "        .record = AttackAnimationProperties{{\n"
            "            .spriteAnimation    = {},\n"
            "            .bg1Animation       = {},\n"
            "            .bg3Animation       = {},\n"
            "            .spritePalette      = {},\n"
            "            .bg1Palette         = {},\n"
            "            .bg3Palette         = {},\n"
            "            .defaultSoundEffect = {},\n"
            "            .initFunction       = {},\n"
            "            .specialGraphics    = {},\n"
            "            .multiTargetDelay   = {},\n"
            "        }},\n"
            "    }},\n".format(
                i, i,
                _anim_ref(_word(r, 0)), _anim_ref(_word(r, 2)),
                _anim_ref(_word(r, 4)),
                r[6], r[7], r[8], r[9], _init_function(r[10]),
                _anim_ref(_word(r, 11)), r[13]))
    return "".join(out)


def render_attack_anim_fixture(rows):
    struct = (
        "// One raw 14-byte AttackAnimProp record alongside its decimal index.\n"
        "struct ExpectedAttackAnimProp {\n"
        "    std::uint16_t index;\n"
        "    std::array<std::uint8_t, 14> bytes;\n"
        "};\n")
    out = [_banner(["btlgfx_main.asm:48939 (AttackAnimProp, ROM d0/7fb2)"]),
           _fixture_head(struct),
           "inline constexpr std::array<ExpectedAttackAnimProp, {}>\n"
           "kExpectedAttackAnimProp = {{{{\n".format(len(rows))]
    for i, r in enumerate(rows):
        hexbytes = ", ".join("0x{:02X}".format(b) for b in r)
        out.append("    {{ .index = {:>3}, .bytes = {{ {} }} }},\n"
                   .format(i, hexbytes))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- AttackGfxProp -----------------------------------------------------------

def read_attack_gfx(path):
    data = _read_dat(path, ATTACK_GFX_WIDTH, ATTACK_GFX_ROWS)
    rows = []
    for i in range(ATTACK_GFX_ROWS):
        base = i * ATTACK_GFX_WIDTH
        rec = data[base:base + ATTACK_GFX_WIDTH]
        frame_index = _word(rec, 2)
        if frame_index >= FRAME_COUNT:
            raise ParseError(path, 0,
                             "row {}: frame-data index {} >= {} — escalate, "
                             "never guess".format(i, frame_index, FRAME_COUNT))
        rows.append(rec)
    return rows


def render_attack_gfx_inc(rows):
    out = [_banner(["btlgfx_main.asm:48889 (AttackGfxProp, ROM d4/d000, EN "
                    "variant)"]),
           "// AnimationGraphicsPropertiesEntry rows in table order, #included\n"
           "// inside the kAnimationGraphicsProperties array in\n"
           "// src/data/attack_animations.cpp. Identity is the decimal .index;\n"
           "// a compile-time assert verifies index == position. The tile word\n"
           "// renders through AnimationTileOffset (of / of2bpp); frame index,\n"
           "// width, and height are decimal.\n\n"]
    for i, r in enumerate(rows):
        out.append(
            "    AnimationGraphicsPropertiesEntry{{  // [${:03X}]\n"
            "        .index = {},\n"
            "        .record = AnimationGraphicsProperties{{\n"
            "            .tileOffset     = {},\n"
            "            .frameDataIndex = {},\n"
            "            .frameWidth     = {},\n"
            "            .frameHeight    = {},\n"
            "        }},\n"
            "    }},\n".format(
                i, i, _tile_offset(_word(r, 0)), _word(r, 2), r[4], r[5]))
    return "".join(out)


def render_attack_gfx_fixture(rows):
    struct = (
        "// One raw 6-byte AttackGfxProp record alongside its decimal index.\n"
        "struct ExpectedAnimationGfxProp {\n"
        "    std::uint16_t index;\n"
        "    std::array<std::uint8_t, 6> bytes;\n"
        "};\n")
    out = [_banner(["btlgfx_main.asm:48889 (AttackGfxProp, ROM d4/d000)"]),
           _fixture_head(struct),
           "inline constexpr std::array<ExpectedAnimationGfxProp, {}>\n"
           "kExpectedAnimationGfxProp = {{{{\n".format(len(rows))]
    for i, r in enumerate(rows):
        hexbytes = ", ".join("0x{:02X}".format(b) for b in r)
        out.append("    {{ .index = {:>3}, .bytes = {{ {} }} }},\n"
                   .format(i, hexbytes))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- Weapon / MonsterAttack shared record ------------------------------------

def _read_weapon_records(path, rows, width=WEAPON_WIDTH):
    data = _read_dat(path, width, rows)
    recs = []
    for i in range(rows):
        base = i * width
        rec = data[base:base + width]
        if rec[7] != 0:
            raise ParseError(path, 0,
                             "row {}: trailing pad byte {:#04x} != 0 — "
                             "escalate, never guess".format(i, rec[7]))
        weapon_anim_type = rec[5] & 0x7F
        if weapon_anim_type >= len(_WEAPON_ANIM_TYPE_NAMES):
            raise ParseError(path, 0,
                             "row {}: thrown byte {:#04x} weapon-animation type "
                             "{} outside the corpus 0..4 space — escalate, "
                             "never guess".format(i, rec[5], weapon_anim_type))
        recs.append(rec)
    return recs


def _weapon_record_literal(r):
    return (
        "WeaponAnimationProperties{{\n"
        "        .handAnimations = {{ {}, {} }},\n"
        "        .weaponPalette  = {},\n"
        "        .hitAnimation   = {},\n"
        "        .hitPalette     = {},\n"
        "        .thrown         = {},\n"
        "        .soundEffect    = {},\n"
        "        .pad7           = {},\n"
        "    }}".format(r[0], r[1], r[2], r[3], r[4], _thrown(r[5]), r[6], r[7]))


def render_weapon_inc(recs, symbols):
    out = [_banner(["btlgfx_main.asm:48897 (WeaponAnimProp, ROM ec/e400)"]),
           "// WeaponAnimationPropertiesEntry rows keyed by the weapon ITEM id\n"
           "// (0-92), #included inside the kWeaponAnimationProperties array in\n"
           "// src/data/attack_animations.cpp. Identity is the .item field; a\n"
           "// compile-time assert verifies it == position. The thrown byte\n"
           "// renders through ThrownAnimationFlags; the rest are decimal (pad7\n"
           "// is the verified-zero trailing byte).\n\n"]
    for i, r in enumerate(recs):
        out.append("    {{ ItemId::{}, {} }},\n"
                   .format(symbols.item(i), _weapon_record_literal(r)))
    return "".join(out)


def render_monster_attack_inc(recs, symbols):
    out = [_banner(["btlgfx_main.asm:48905 (MonsterAttackAnimProp, ROM "
                    "ec/e6e8)"]),
           "// MonsterAttackAnimationPropertiesEntry rows keyed by the\n"
           "// MonsterAttackAnimation enumerator (the 35-row table IS that\n"
           "// index space), #included inside the\n"
           "// kMonsterAttackAnimationProperties array in\n"
           "// src/data/attack_animations.cpp. A compile-time assert verifies\n"
           "// the animation == position. Same record shape as WeaponAnimProp.\n\n"]
    for i, r in enumerate(recs):
        out.append("    {{ MonsterAttackAnimation::{}, {} }},\n"
                   .format(symbols.monster_anim_names[i],
                           _weapon_record_literal(r)))
    return "".join(out)


def _render_weapon_fixture(recs, struct_name, array_name, source):
    struct = (
        "// One raw 8-byte weapon/monster attack-animation record alongside its\n"
        "// decimal index.\n"
        "struct {} {{\n"
        "    std::uint16_t index;\n"
        "    std::array<std::uint8_t, 8> bytes;\n"
        "}};\n".format(struct_name))
    out = [_banner([source]),
           _fixture_head(struct),
           "inline constexpr std::array<{}, {}>\n"
           "{} = {{{{\n".format(struct_name, len(recs), array_name)]
    for i, r in enumerate(recs):
        hexbytes = ", ".join("0x{:02X}".format(b) for b in r)
        out.append("    {{ .index = {:>3}, .bytes = {{ {} }} }},\n"
                   .format(i, hexbytes))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- ItemJumpThrowAnim -------------------------------------------------------

def read_item_jump_throw(path):
    data = _read_dat(path, 1, ITEM_JUMP_THROW_ROWS)
    return list(data)


def render_item_jump_throw_inc(values, symbols):
    out = [_banner(["btlgfx_main.asm:49015 (ItemJumpThrowAnim, ROM d1/0040)"]),
           "// The unarmed row (index 0) and the 256 item rows (keyed by\n"
           "// ItemId), #included at namespace scope in\n"
           "// src/data/attack_animations.h. Each value renders through\n"
           "// ItemThrowAnimation (of / fightAnimation) naming its jump and\n"
           "// throw classes — never a raw packed byte.\n\n"
           "inline constexpr ItemThrowAnimation kUnarmedItemThrowAnimation =\n"
           "    {};\n\n".format(_item_throw(values[0])),
           "inline constexpr std::array<ItemThrowAnimationEntry, 256>\n"
           "kItemThrowAnimations = {{\n"]
    for item_id in range(256):
        value = values[item_id + 1]   # row 0 is unarmed; item k -> row k+1
        out.append("    {{ ItemId::{}, {} }},  // [${:02X}]\n"
                   .format(symbols.item(item_id), _item_throw(value), item_id))
    out.append("}};\n")
    return "".join(out)


def render_item_jump_throw_fixture(values):
    out = [_banner(["btlgfx_main.asm:49015 (ItemJumpThrowAnim, ROM d1/0040)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"
           "// The raw 257 ItemJumpThrowAnim bytes (row 0 unarmed, rows 1-256\n"
           "// the items). The test reassembles kUnarmedItemThrowAnimation +\n"
           "// kItemThrowAnimations and memcmps against this.\n"
           "inline constexpr std::array<std::uint8_t, 257>\n"
           "kExpectedItemJumpThrowAnim = {{\n"]
    for chunk_start in range(0, ITEM_JUMP_THROW_ROWS, 12):
        chunk = values[chunk_start:chunk_start + 12]
        out.append("    " + ", ".join("0x{:02X}".format(b) for b in chunk)
                   + ",\n")
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- ItemAnimPtrs ------------------------------------------------------------

def _eval_word_expr(expr, path, lineno):
    """Evaluate a `.word` argument: a literal, or a `<int>*<int>` product."""
    if "*" in expr:
        value = 1
        for part in expr.split("*"):
            operand = common.parse_int_literal(part.strip())
            if operand is None:
                raise ParseError(path, lineno,
                                 "non-literal factor in {!r}".format(expr))
            value *= operand
        return value
    literal = common.parse_int_literal(expr)
    if literal is None:
        raise ParseError(path, lineno, "non-literal word {!r}".format(expr))
    return literal


def read_item_anim_ptrs(asm_path):
    with open(asm_path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()
    started = False
    words = []
    for idx, raw in enumerate(lines):
        code, _comment = common.strip_comment(raw)
        s = code.strip()
        if not started:
            if s == _ITEM_ANIM_PTRS_LABEL:
                started = True
            continue
        if not s:
            continue
        if not s.startswith(".word"):
            break
        words.append(_eval_word_expr(s[len(".word"):].strip(), asm_path,
                                     idx + 1))
    if not started:
        raise ParseError(asm_path, 0, "ItemAnimPtrs label not found")
    if len(words) != ITEM_ANIM_PTR_WORDS:
        raise ParseError(asm_path, 0,
                         "expected {} ItemAnimPtrs words, found {}"
                         .format(ITEM_ANIM_PTR_WORDS, len(words)))
    rows = []
    for i, word in enumerate(words):
        if word == 0xFFFF:
            rows.append((word, None))
            continue
        if word % ANIM_PTR_MULTIPLIER != 0:
            raise ParseError(asm_path, 0,
                             "ItemAnimPtrs word {} ({}) is not a multiple of "
                             "{} — escalate".format(i, word,
                                                    ANIM_PTR_MULTIPLIER))
        row = word // ANIM_PTR_MULTIPLIER
        if row >= ATTACK_ANIM_ROWS:
            raise ParseError(asm_path, 0,
                             "ItemAnimPtrs word {} -> row {} >= {} — escalate"
                             .format(i, row, ATTACK_ANIM_ROWS))
        rows.append((word, row))
    return rows


def render_item_anim_ptrs_inc(rows, symbols):
    out = [_banner(["btlgfx_main.asm:48980 (ItemAnimPtrs, ROM d1/0000, inline "
                    "C1 word table)"]),
           "// UsableItemAnimationEntry rows keyed by the usable ITEM id\n"
           "// ($e0-$ff), #included inside the kUsableItemAnimations array in\n"
           "// src/data/attack_animations.cpp. Each ROM word is a\n"
           "// pre-multiplied AttackAnimProp offset (row*14); the parser stores\n"
           "// the de-multiplied row index as AttackAnimationIndex, and $ffff\n"
           "// as NONE. A compile-time assert verifies the ItemId == position.\n\n"]
    for i, (_word, row) in enumerate(rows):
        item_id = USABLE_ITEM_FIRST + i
        anim = ("AttackAnimationIndex::NONE" if row is None
                else "AttackAnimationIndex::of({})".format(row))
        out.append("    {{ ItemId::{}, {} }},  // [${:02X}]\n"
                   .format(symbols.item(item_id), anim, item_id))
    return "".join(out)


def render_item_anim_ptrs_fixture(rows):
    out = [_banner(["btlgfx_main.asm:48980 (ItemAnimPtrs, ROM d1/0000)"]),
           "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           "namespace ostinato::test {\n\n"
           "// The raw 32 ItemAnimPtrs ROM words (row*14, or 0xFFFF for none).\n"
           "// The test re-multiplies each AttackAnimationIndex by 14 and\n"
           "// memcmps against this to prove byte identity.\n"
           "inline constexpr std::array<std::uint16_t, 32>\n"
           "kExpectedItemAnimPtrs = {{\n"]
    for word, _row in rows:
        out.append("    0x{:04X},\n".format(word))
    out.append("}};\n\n}  // namespace ostinato::test\n")
    return "".join(out)


# --- driver ------------------------------------------------------------------

def _paths(source_root):
    j = os.path.join
    btl = j(source_root, "src", "btlgfx")
    return {
        "attack_anim": j(btl, "attack_anim_prop_en.dat"),
        "attack_gfx": j(btl, "attack_gfx_prop_en.dat"),
        "weapon": j(btl, "weapon_anim_prop.dat"),
        "monster_attack": j(btl, "monster_attack_anim_prop.dat"),
        "item_jump_throw": j(btl, "item_jump_throw_anim.dat"),
        "btlgfx_main": j(btl, "btlgfx_main.asm"),
        "const_inc": j(source_root, "include", "const.inc"),
    }


def _outputs(repo_root):
    gen = os.path.join(repo_root, "src", "data", "generated")
    fix = os.path.join(repo_root, "tests", "fixtures")
    j = os.path.join
    return {
        "attack_anim_inc": j(gen, "attack_anim_prop_data.inc"),
        "attack_anim_fix": j(fix, "attack_anim_prop_expected.h"),
        "attack_gfx_inc": j(gen, "attack_gfx_prop_data.inc"),
        "attack_gfx_fix": j(fix, "attack_gfx_prop_expected.h"),
        "weapon_inc": j(gen, "weapon_anim_prop_data.inc"),
        "weapon_fix": j(fix, "weapon_anim_prop_expected.h"),
        "monster_attack_inc": j(gen, "monster_attack_anim_prop_data.inc"),
        "monster_attack_fix": j(fix, "monster_attack_anim_prop_expected.h"),
        "item_jump_throw_inc": j(gen, "item_jump_throw_anim_data.inc"),
        "item_jump_throw_fix": j(fix, "item_jump_throw_anim_expected.h"),
        "item_anim_ptrs_inc": j(gen, "item_anim_ptrs_data.inc"),
        "item_anim_ptrs_fix": j(fix, "item_anim_ptrs_expected.h"),
    }


def run(source_root, repo_root, check_only=False):
    p = _paths(source_root)
    symbols = Symbols(p["const_inc"])

    attack_anim = read_attack_anim(p["attack_anim"])
    attack_gfx = read_attack_gfx(p["attack_gfx"])
    weapon = _read_weapon_records(p["weapon"], WEAPON_ROWS)
    monster_attack = _read_weapon_records(p["monster_attack"],
                                          MONSTER_ATTACK_ROWS)
    item_jump_throw = read_item_jump_throw(p["item_jump_throw"])
    item_anim_ptrs = read_item_anim_ptrs(p["btlgfx_main"])

    if check_only:
        print("OK: attack_anim {} / attack_gfx {} / weapon {} / monster_attack "
              "{} / item_jump_throw {} / item_anim_ptrs {}; all structural "
              "asserts passed.".format(
                  len(attack_anim), len(attack_gfx), len(weapon),
                  len(monster_attack), len(item_jump_throw),
                  len(item_anim_ptrs)))
        return 0

    o = _outputs(repo_root)
    _write(o["attack_anim_inc"], render_attack_anim_inc(attack_anim))
    _write(o["attack_anim_fix"], render_attack_anim_fixture(attack_anim))
    _write(o["attack_gfx_inc"], render_attack_gfx_inc(attack_gfx))
    _write(o["attack_gfx_fix"], render_attack_gfx_fixture(attack_gfx))
    _write(o["weapon_inc"], render_weapon_inc(weapon, symbols))
    _write(o["weapon_fix"], _render_weapon_fixture(
        weapon, "ExpectedWeaponAnimProp", "kExpectedWeaponAnimProp",
        "btlgfx_main.asm:48897 (WeaponAnimProp, ROM ec/e400)"))
    _write(o["monster_attack_inc"],
           render_monster_attack_inc(monster_attack, symbols))
    _write(o["monster_attack_fix"], _render_weapon_fixture(
        monster_attack, "ExpectedMonsterAttackAnimProp",
        "kExpectedMonsterAttackAnimProp",
        "btlgfx_main.asm:48905 (MonsterAttackAnimProp, ROM ec/e6e8)"))
    _write(o["item_jump_throw_inc"],
           render_item_jump_throw_inc(item_jump_throw, symbols))
    _write(o["item_jump_throw_fix"],
           render_item_jump_throw_fixture(item_jump_throw))
    _write(o["item_anim_ptrs_inc"],
           render_item_anim_ptrs_inc(item_anim_ptrs, symbols))
    _write(o["item_anim_ptrs_fix"],
           render_item_anim_ptrs_fixture(item_anim_ptrs))
    print("Emitted 6 tables (12 files) -> {}"
          .format(os.path.join(repo_root, "src", "data", "generated")))
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
                    help="disassembly root")
    ap.add_argument("--repo-root", default=".", help="repo root for outputs")
    ap.add_argument("--check-only", action="store_true",
                    help="validate + assert without writing files")
    args = ap.parse_args(argv)
    try:
        return run(args.source_root, args.repo_root,
                   check_only=args.check_only)
    except ParseError as exc:
        sys.stderr.write("PARSE ERROR: {}\n".format(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
