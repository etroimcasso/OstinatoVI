// The effect bytes of a 30-byte item-properties record: the field-equipment
// effects byte, the five relic-effects bytes, and one-byte slices of the
// 32-status space. No upstream symbol source exists for these bits — the
// meanings live in the prose RAM map (original-src/notes/battle-ram.txt:
// 323-381, the $11D2-$11DF cells CalcEquipEffect copies these bytes into),
// cited per enum below. Byte offsets refer to the record layout in
// src/data/item_properties.h.
#pragma once

#include <concepts>
#include <cstdint>

#include "ostinato/flag_set.h"
#include "ostinato/status_id.h"

namespace ostinato {

// Record byte +5 — field equipment effects ($11DF, battle-ram.txt:377-381).
enum class FieldEffect : std::uint8_t {
    CHARM_BANGLE = 0x01,  // bit 0: 50% less random battles
    MOOGLE_CHARM = 0x02,  // bit 1: no random battles
    SPRINT_SHOES = 0x20,  // bit 5: 1.5x walk speed
    TINTINABAR   = 0x80,  // bit 7: tintina bar effect (doesn't work — upstream's
                          //        own note, preserved verbatim)
};

// Record byte +9 — relic effects 1 ($11D5, battle-ram.txt:326-334).
enum class RelicEffect1 : std::uint8_t {
    RAISE_FIGHT_DAMAGE = 0x01,  // bit 0: raise fight damage (atlas armlet,
                                //        hero ring)
    RAISE_MAGIC_DAMAGE = 0x02,  // bit 1: raise magic damage (double earrings
                                //        or hero ring)
    HP_PLUS_25         = 0x04,  // bit 2: HP +25% (red cap)
    HP_PLUS_50         = 0x08,  // bit 3: HP +50% (muscle belt)
    HP_PLUS_12_5       = 0x10,  // bit 4: HP +12.5% (green beret)
    MP_PLUS_25         = 0x20,  // bit 5: MP +25% (minerva)
    MP_PLUS_50         = 0x40,  // bit 6: MP +50% (crystal orb)
    MP_PLUS_12_5       = 0x80,  // bit 7: MP +12.5% (bard's hat)
};

// Record byte +10 — relic effects 2 ($11D6, battle-ram.txt:335-343).
enum class RelicEffect2 : std::uint8_t {
    RAISE_PREEMPTIVE_RATE = 0x01,  // bit 0: increase pre-emptive attack rate
                                   //        (gale hairpin)
    PREVENT_BACK_PINCER   = 0x02,  // bit 1: prevent back/pincer attacks
                                   //        (back guard)
    FIGHT_TO_JUMP         = 0x04,  // bit 2: fight -> jump (dragoonboots)
    MAGIC_TO_X_MAGIC      = 0x08,  // bit 3: magic -> x-magic (gem box)
    SKETCH_TO_CONTROL     = 0x10,  // bit 4: sketch -> control (fakemustache)
    SLOT_TO_GP_RAIN       = 0x20,  // bit 5: slot -> gp rain (coin toss)
    STEAL_TO_CAPTURE      = 0x40,  // bit 6: steal -> capture (thief glove)
    CONTINUOUS_JUMP       = 0x80,  // bit 7: jump continuously (dragon horn)
};

// Record byte +11 — relic effects 3 ($11D7, battle-ram.txt:344-352).
enum class RelicEffect3 : std::uint8_t {
    RAISE_STEAL_RATE       = 0x01,  // bit 0: increase steal rate (sneak ring)
    RAISE_MAGIC_DAMAGE     = 0x02,  // bit 1: raise magic damage (single
                                    //        earring or hero ring)
    RAISE_SKETCH_RATE      = 0x04,  // bit 2: increase sketch rate (beret)
    RAISE_CONTROL_RATE     = 0x08,  // bit 3: increase control rate (coronet)
    HIT_100_IGNORE_MBLOCK  = 0x10,  // bit 4: 100% hit rate, ignore target's
                                    //        mblock (sniper sight)
    MP_COST_50_PERCENT     = 0x20,  // bit 5: MP cost = 50% (gold hairpin)
    MP_COST_1              = 0x40,  // bit 6: MP cost = 1 (economizer)
    RAISE_VIGOR_50_PERCENT = 0x80,  // bit 7: raise vigor +50% (hyper wrist)
};

// Record byte +12 — relic effects 4 ($11D8, battle-ram.txt:353-360). Bit 7 is
// undocumented in the RAM map and stays unnamed.
enum class RelicEffect4 : std::uint8_t {
    FIGHT_TO_X_FIGHT     = 0x01,  // bit 0: fight -> x-fight (offering)
    RANDOM_COUNTER       = 0x02,  // bit 1: randomly counter (black belt)
    RANDOM_EVADE         = 0x04,  // bit 2: randomly evade (beads)
    USE_WEAPON_2_HANDED  = 0x08,  // bit 3: uses weapon 2-handed (gauntlet)
    EQUIP_2_WEAPONS      = 0x10,  // bit 4: can equip 2 weapons (genji glove)
    EQUIP_HEAVY_ITEMS    = 0x20,  // bit 5: can equip heavy items (merit award)
    PROTECT_WEAK_ALLIES  = 0x40,  // bit 6: protects weak allies (true knight)
};

// Record byte +13 — relic effects 5 ($11D9, battle-ram.txt:361-367). Bits 5
// and 6 are undocumented in the RAM map and stay unnamed.
enum class RelicEffect5 : std::uint8_t {
    SHELL_WHEN_HP_LOW = 0x01,  // bit 0: casts shell when HP is low (barrier
                               //        ring, czarina ring)
    SAFE_WHEN_HP_LOW  = 0x02,  // bit 1: casts safe when HP is low (mithril
                               //        glove, czarina ring)
    WALL_WHEN_HP_LOW  = 0x04,  // bit 2: casts wall when HP is low
    DOUBLE_EXPERIENCE = 0x08,  // bit 3: double experience (exp. egg)
    DOUBLE_GP         = 0x10,  // bit 4: double GP (cat hood)
    MAKE_UNDEAD       = 0x80,  // bit 7: make character undead (relic ring)
};

// The block-graphic sub-field of a record's special-effect byte (bits 0-1 —
// $11BE "----mpbb", battle-ram.txt:298-302).
enum class BlockGraphic : std::uint8_t {
    DAGGER      = 0,
    SWORD       = 1,
    SHIELD      = 2,
    ZEPHYR_CAPE = 3,
};

// The can-block bits of the same byte (battle_main.asm:2598-2606 routes them
// into the $11D0/$11D1 block-graphic masks).
enum class BlockAbility : std::uint8_t {
    PHYSICAL = 0x04,  // bit 2: can block physical attacks
    MAGIC    = 0x08,  // bit 3: can block magic attacks
};

// The weapon special-effect index (special-effect byte bits 4-7; the attack
// setup shifts it into the effect dispatcher at battle_main.asm:6954-6957,
// "weapons can only use $00-$0f"). Each name comes from the index's handler
// header in battle_main.asm's attacker/target special-effect jump tables.
enum class WeaponSpecialEffect : std::uint8_t {
    NONE                 = 0x0,
    THIEFKNIFE           = 0x1,  // battle_main.asm:10134
    ATMA_WEAPON          = 0x2,  // battle_main.asm:10449
    INSTANT_KILL         = 0x3,  // battle_main.asm:9899 (instant kill with "x")
    MAN_EATER            = 0x4,  // battle_main.asm:9158
    DRAINER              = 0x5,  // battle_main.asm:10297
    SOUL_SABRE           = 0x6,  // battle_main.asm:10287
    MP_CRITICAL          = 0x7,  // battle_main.asm:10225 (use mp for critical)
    SNIPER_HAWKEYE       = 0x8,  // battle_main.asm:9170
    DICE                 = 0x9,  // battle_main.asm:10671
    VALIANTKNIFE         = 0xA,  // battle_main.asm:10319
    TEMPEST              = 0xB,  // battle_main.asm:10337
    HEAL_ROD             = 0xC,  // battle_main.asm:10308
    SCIMITAR_ZANTETSUKEN = 0xD,  // battle_main.asm:9103
    OGRE_NIX             = 0xE,  // battle_main.asm:10175 (organyx)
};

// The item-use effect of a consumable's special-effect byte. The item-use
// path offsets the value by $48 into the same special-effect dispatch space
// (battle_main.asm:7024-7028); each name comes from the offset index's
// handler header ($49 magicite ... $4D warp stone). $4E has no dedicated
// handler — DRIED_MEAT is the plain-restore slot.
enum class ItemUseEffect : std::uint8_t {
    MAGICITE   = 1,  // -> $49, battle_main.asm:10350
    SUPER_BALL = 2,  // -> $4A, battle_main.asm:10425
    SMOKE_BOMB = 3,  // -> $4B, battle_main.asm:10555
    ELIXIR     = 4,  // -> $4C, battle_main.asm:10635 (elixir/megalixir)
    WARP_STONE = 5,  // -> $4D, battle_main.asm:10461 (warp/warp stone)
    DRIED_MEAT = 6,  // -> $4E
};

// The consumable-role bits of record byte +19 — the item-use path walks them
// with an asl chain at battle_main.asm:7035-7059 (each name from that
// chain's branch comments). The weapon role of the same byte is the
// WeaponFlags space; the roles never overlap in the corpus.
enum class ItemUseFlag : std::uint8_t {
    INVERT_ON_UNDEAD  = 0x02,  // bit 1: invert damage to undead
    RESTORES_HP       = 0x08,  // bit 3: restore hp
    RESTORES_MP       = 0x10,  // bit 4: restore mp
    REMOVES_STATUS    = 0x20,  // bit 5: item removes status
    FRACTIONAL_DAMAGE = 0x80,  // bit 7: damage is a fraction of total hp/mp
};

// The mode bits of the spell-cast byte (+18): bit 6 enables the 1-in-4
// random cast when attacking (CheckWeaponMagic, battle_main.asm:8664-8673);
// bit 7 routes the cast-when-used-as-item path (InitTarget_01,
// battle_main.asm:6525-6533 — set on the rods and elemental shields, the
// battle-usable casters). The low 6 bits are the spell index.
enum class SpellCastMode : std::uint8_t {
    RANDOM_ON_ATTACK = 0x40,  // bit 6
    CAST_ON_ITEM_USE = 0x80,  // bit 7
};

using FieldEffectSet  = FlagSet<FieldEffect>;
using RelicEffect1Set = FlagSet<RelicEffect1>;
using RelicEffect2Set = FlagSet<RelicEffect2>;
using RelicEffect3Set = FlagSet<RelicEffect3>;
using RelicEffect4Set = FlagSet<RelicEffect4>;
using RelicEffect5Set = FlagSet<RelicEffect5>;
using ItemUseFlagSet  = FlagSet<ItemUseFlag>;

static_assert(sizeof(FieldEffectSet) == 1 && sizeof(RelicEffect1Set) == 1 &&
              sizeof(RelicEffect2Set) == 1 && sizeof(RelicEffect3Set) == 1 &&
              sizeof(RelicEffect4Set) == 1 && sizeof(RelicEffect5Set) == 1,
              "item effect flag sets must stay byte-identical to their ROM bytes");

// A one-byte slice of the 32-status space: the statuses whose StatusId lands
// in byte ByteIndex under the StatusSet packing rule (byte id/8, bit id%8 —
// ostinato/status_set.h). The item record stores single status bytes (+6 =
// status 1, +7 = status 2, +8 = status 3, +25 = status 2 space), so each
// field carries the slice type matching its byte. sizeof == 1 keeps every
// instantiation byte-identical to its ROM byte.
template <std::uint8_t ByteIndex>
struct StatusByteSet {
    static_assert(ByteIndex < 4, "the status space spans four bytes");

    std::uint8_t bits = 0;

    static constexpr bool covers(StatusId id) {
        return static_cast<std::uint8_t>(id) / 8 == ByteIndex;
    }

    constexpr bool has(StatusId id) const {
        return covers(id) &&
               (bits & static_cast<std::uint8_t>(
                           1u << (static_cast<std::uint8_t>(id) % 8))) != 0;
    }

    constexpr void set(StatusId id) {
        // A StatusId outside this slice cannot be represented here; constant
        // evaluation rejects the call outright.
        if (!covers(id)) {
            throw "StatusId outside this status byte's slice";
        }
        bits |= static_cast<std::uint8_t>(
            1u << (static_cast<std::uint8_t>(id) % 8));
    }

    // OR-together builder: Status1Set::of(StatusId::BLIND, StatusId::ZOMBIE).
    // Zero arguments yields the empty set, matching an all-zero ROM byte.
    static constexpr StatusByteSet of(std::same_as<StatusId> auto... ids) {
        StatusByteSet result{};
        (result.set(ids), ...);
        return result;
    }
};

using Status1Set = StatusByteSet<0>;  // StatusId $00-$07 (BLIND..DEAD)
using Status2Set = StatusByteSet<1>;  // StatusId $08-$0F (CONDEMNED..SLEEP)
using Status3Set = StatusByteSet<2>;  // StatusId $10-$17 (DANCE..REFLECT)

static_assert(sizeof(Status1Set) == 1 && sizeof(Status2Set) == 1 &&
              sizeof(Status3Set) == 1,
              "status byte slices must stay byte-identical to their ROM bytes");

}  // namespace ostinato
