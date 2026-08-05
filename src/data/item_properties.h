// Item properties: the 256-record item_prop table — item, weapon, armor, and
// relic stats in one 30-byte record. The row data is generated
// (src/data/generated/item_prop_en_data.inc); this header owns the record
// type, its record-local packed wrappers, the entry type, and the accessors.
//
// No RAM-map byte table documents the record itself; the layout authority is
// the consumer access sites (GetItemPropPtr's x30 stride,
// src/menu/item.asm:1001-1012 and src/battle/battle_main.asm:7177-7180, plus
// the per-field offsets cited on each member below) and the $11D2-$11DF cells
// CalcEquipEffect copies bytes +5..+13 into (battle_main.asm:2480-2533,
// notes/battle-ram.txt:318-381).
//
// Fields +15 and +18..+27 change role by the record's ItemType — e.g. +20 is
// battle power on a weapon, defense on armor, and HP/MP restored on a
// consumable. Members carry equipment-primary names and types; the full
// per-type role tables live in docs/contracts/item-shop-data.md.
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/attack_id.h"
#include "ostinato/element_set.h"
#include "ostinato/equip_permissions.h"
#include "ostinato/flag_set.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/item_type.h"
#include "ostinato/item_usage.h"
#include "ostinato/stat_boost_pair.h"
#include "ostinato/targeting.h"
#include "ostinato/weapon_flags.h"

namespace ostinato {

using ItemUsageSet  = FlagSet<ItemUsage>;
using WeaponFlagSet = FlagSet<WeaponFlags>;

// Record byte +0: the item's type in the low 3 bits (menus mask with $07 —
// src/menu/item.asm:565-570) packed with its usage flags (ITEM_USAGE bits
// $10/$20/$40 — item.asm:569-570 tests MENU on the same byte). Bits 3 and 7
// are unused across the corpus and unnamed.
struct ItemTypeUsage {
    std::uint8_t packed = 0;

    constexpr ItemType type() const {
        return static_cast<ItemType>(packed & 0x07);
    }

    constexpr ItemUsageSet usage() const {
        return ItemUsageSet{static_cast<std::uint8_t>(packed & ~0x07u)};
    }

    // Builder from the type and the usage flags:
    // ItemTypeUsage::of(ItemType::CONSUMABLE, ItemUsage::BATTLE,
    // ItemUsage::MENU).
    static constexpr ItemTypeUsage of(ItemType type,
                                      std::same_as<ItemUsage> auto... usage) {
        ItemTypeUsage result{static_cast<std::uint8_t>(type)};
        ((result.packed |= static_cast<std::uint8_t>(usage)), ...);
        return result;
    }
};

static_assert(sizeof(ItemTypeUsage) == 1,
              "ItemTypeUsage must be byte-identical to the ROM type/usage byte");

// Record byte +18: the spell-cast byte — a spell index in the low 6 bits
// plus the two SpellCastMode bits. CheckWeaponMagic masks #$3f for the spell
// and tests bit 6 for the random cast on attack
// (battle_main.asm:8664-8673); InitTarget_01 routes bit 7's
// cast-when-used-as-item path (battle_main.asm:6525-6533).
struct ItemSpellCast {
    std::uint8_t raw = 0;

    constexpr bool randomOnAttack() const {
        return (raw & static_cast<std::uint8_t>(
                          SpellCastMode::RANDOM_ON_ATTACK)) != 0;
    }

    constexpr bool castOnItemUse() const {
        return (raw & static_cast<std::uint8_t>(
                          SpellCastMode::CAST_ON_ITEM_USE)) != 0;
    }

    constexpr AttackId spell() const {
        return static_cast<AttackId>(raw & 0x3F);
    }

    // Builder from the spell and its mode bits:
    // ItemSpellCast::of(AttackId::FIRE2, SpellCastMode::CAST_ON_ITEM_USE).
    static constexpr ItemSpellCast of(AttackId spell,
                                      std::same_as<SpellCastMode> auto... modes) {
        ItemSpellCast result{
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(spell) & 0x3F)};
        ((result.raw |= static_cast<std::uint8_t>(modes)), ...);
        return result;
    }
};

static_assert(sizeof(ItemSpellCast) == 1,
              "ItemSpellCast must be byte-identical to the ROM spell-cast byte");

// Record byte +26: two packed nibbles, each an index into the battle code's
// EquipEvadeTbl boost table — low nibble evade, high nibble mblock
// (battle_main.asm:2513-2530 adds the indexed boosts to $11A8/$11AA; the menu
// draws them in the same order, src/menu/item.asm:1744-1757).
struct EvadeBlockPair {
    std::uint8_t packed = 0;

    constexpr std::uint8_t evadeIndex() const { return packed & 0x0F; }
    constexpr std::uint8_t mblockIndex() const { return packed >> 4; }

    // Builder from the two table indices; nibbles are plain unsigned indices,
    // so every emitted row round-trips to its ROM byte.
    static constexpr EvadeBlockPair of(std::uint8_t evadeIndex,
                                       std::uint8_t mblockIndex) {
        return EvadeBlockPair{static_cast<std::uint8_t>(
            (evadeIndex & 0x0F) | ((mblockIndex & 0x0F) << 4))};
    }
};

static_assert(sizeof(EvadeBlockPair) == 1,
              "EvadeBlockPair must be byte-identical to the ROM evade/mblock byte");

// Record byte +27: the special-effect byte, packed per the record's role. On
// equipment, bits 0-3 are the block info CalcEquipEffect copies to $11BE and
// decomposes inline (graphic bits 0-1, can-block bits 2-3 —
// battle_main.asm:2584-2606, battle-ram.txt:298-302), and bits 4-7 are the
// weapon special-effect index the attack setup shifts into the effect
// dispatcher (battle_main.asm:6954-6957). On a consumable, the whole byte is
// the item-use effect (offset by $48 into the same dispatch space,
// battle_main.asm:7024-7028), $00 means no effect (battle-ram.txt:245), and
// bit 7 disables the effect outright — the corpus uses $FF
// (battle_main.asm:6870-6872).
struct ItemSpecialEffect {
    std::uint8_t packed = 0;

    // Equipment reading.
    constexpr WeaponSpecialEffect weaponEffect() const {
        return static_cast<WeaponSpecialEffect>(packed >> 4);
    }
    constexpr BlockGraphic blockGraphic() const {
        return static_cast<BlockGraphic>(packed & 0x03);
    }
    constexpr bool blocksPhysical() const {
        return (packed & static_cast<std::uint8_t>(BlockAbility::PHYSICAL)) != 0;
    }
    constexpr bool blocksMagic() const {
        return (packed & static_cast<std::uint8_t>(BlockAbility::MAGIC)) != 0;
    }

    // Consumable reading.
    constexpr bool itemUseDisabled() const { return (packed & 0x80) != 0; }
    constexpr ItemUseEffect itemUseEffect() const {
        return static_cast<ItemUseEffect>(packed);
    }

    // A weapon effect with no block info (block-info nibble all zero).
    static constexpr ItemSpecialEffect weapon(WeaponSpecialEffect effect) {
        return ItemSpecialEffect{
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(effect) << 4)};
    }

    // The full equipment byte: weapon effect, block graphic, and can-block
    // bits. ItemSpecialEffect::equipment(WeaponSpecialEffect::NONE,
    // BlockGraphic::SWORD, BlockAbility::PHYSICAL).
    static constexpr ItemSpecialEffect equipment(
        WeaponSpecialEffect effect, BlockGraphic graphic,
        std::same_as<BlockAbility> auto... abilities) {
        ItemSpecialEffect result = weapon(effect);
        result.packed |= static_cast<std::uint8_t>(graphic);
        ((result.packed |= static_cast<std::uint8_t>(abilities)), ...);
        return result;
    }

    // A consumable's item-use effect.
    static constexpr ItemSpecialEffect itemUse(ItemUseEffect effect) {
        return ItemSpecialEffect{static_cast<std::uint8_t>(effect)};
    }

    // The disabled consumable byte ($FF).
    static constexpr ItemSpecialEffect disabled() {
        return ItemSpecialEffect{0xFF};
    }
};

static_assert(sizeof(ItemSpecialEffect) == 1,
              "ItemSpecialEffect must be byte-identical to the ROM special-effect byte");

// Record byte +19 under its consumable role: builder and view over the
// ItemUseFlag bits, byte-identical to the WeaponFlagSet member that carries
// the byte (the roles share the field; they never overlap in the corpus).
constexpr WeaponFlagSet itemUseFlags(std::same_as<ItemUseFlag> auto... flags) {
    return WeaponFlagSet{ItemUseFlagSet::of(flags...).bits};
}

constexpr ItemUseFlagSet itemUseView(WeaponFlagSet flags) {
    return ItemUseFlagSet{flags.bits};
}

// Two record-byte-+19 bits port verbatim as dead data — set in the corpus
// but read by no code in the tree:
//
//   * Bit 0, on exactly three defensive items (Paladin Shld, Memento Ring,
//     Safety Bit). The battle side copies hand-slot bytes to the
//     per-character weapon-effects cells but only ever tests bits 1/5/6/7,
//     relic bytes are never copied, and every menu read masks the 2-hand
//     bit.
//   * Bit 6, on exactly Magicite and Super Ball. The battle item-use chain
//     shifts it through untested (battle_main.asm:7041-7042) and the menu
//     restore path tests only bits 3/4/7; both items' behavior comes
//     entirely from their ItemUseEffect dispatch.
inline constexpr std::uint8_t kDeadItemFlagBit0 = 0x01;
inline constexpr std::uint8_t kDeadItemFlagBit6 = 0x40;

// One 30-byte item-properties record (item_prop_en.dat, ROM D8/5000). Member
// order and widths mirror the ROM record byte-for-byte — pinned by the
// static_asserts below and the full-corpus byte-equivalence test. Each
// member's citation is its layout authority.
struct ItemProperties {
    ItemTypeUsage typeAndUsage;         // +0   item.asm:565-570
    EquipPermissions equippableBy;      // +1-2 item.asm:1358, equip.asm:2287-2317
    std::uint8_t spellLearnRate;        // +3   item.asm:1717
    AttackId spellLearned;              // +4   item.asm:1720
    FieldEffectSet fieldEffects;        // +5   battle_main.asm:2487 -> $11DF
    Status1Set status1Protection;       // +6   battle_main.asm:2490 -> $11D2
    Status2Set status2Protection;       // +7   (16-bit half of +6)  -> $11D3
    Status3Set status3Granted;          // +8   battle_main.asm:2492 -> $11D4
    RelicEffect1Set relicEffects1;      // +9   (16-bit half of +8)  -> $11D5
    RelicEffect2Set relicEffects2;      // +10  battle_main.asm:2494 -> $11D6
    RelicEffect3Set relicEffects3;      // +11  (16-bit half of +10) -> $11D7
    RelicEffect4Set relicEffects4;      // +12  battle_main.asm:2496 -> $11D8
    RelicEffect5Set relicEffects5;      // +13  (16-bit half of +12) -> $11D9
    Targeting targeting;                // +14  battle_main.asm:6510
    ElementSet element;                 // +15  item.asm:1830 (role varies by
                                        //      ItemType — see docs/contracts/)
    StatBoostPair vigorSpeed;           // +16  item.asm:1583, battle_main.asm:2498
    StatBoostPair staminaMagicPower;    // +17  item.asm:1603
    ItemSpellCast spellCast;            // +18  battle_main.asm:2642, 6517
    WeaponFlagSet weaponFlags;          // +19  item.asm:1654-1668,
                                        //      battle_main.asm:2644 -> $11DA
                                        //      (role varies by ItemType)
    std::uint8_t power;                 // +20  item.asm:1624/1701/2476
                                        //      (role varies by ItemType)
    std::uint8_t hitRateOrDefense;      // +21  item.asm:1629, battle_main.asm:2640
                                        //      (role varies by ItemType)
    ElementSet elementsAbsorbed;        // +22  item.asm:1948, battle_main.asm:2559
                                        //      (role varies by ItemType)
    ElementSet elementsNullified;       // +23  item.asm:1953, battle_main.asm:7032
                                        //      (role varies by ItemType)
    ElementSet elementsWeak;            // +24  item.asm:1958, battle_main.asm:2556
                                        //      (role varies by ItemType)
    Status2Set status2Set;              // +25  battle_main.asm:2552 -> $11BC
                                        //      (cursed-gear class)
    EvadeBlockPair evadeMagicBlock;     // +26  item.asm:1744, battle_main.asm:2513
    ItemSpecialEffect specialEffect;    // +27  battle_main.asm:2584/7024
                                        //      (role varies by ItemType)
    std::uint16_t price;                // +28-29 (little-endian)
                                        //      shop.asm:1140-1148
};

static_assert(sizeof(ItemProperties) == 30,
              "ItemProperties must be byte-identical to a 30-byte item_prop record");
// The byte offsets ARE the contract (per-field citations above).
static_assert(offsetof(ItemProperties, typeAndUsage) == 0);
static_assert(offsetof(ItemProperties, equippableBy) == 1);
static_assert(offsetof(ItemProperties, spellLearnRate) == 3);
static_assert(offsetof(ItemProperties, spellLearned) == 4);
static_assert(offsetof(ItemProperties, fieldEffects) == 5);
static_assert(offsetof(ItemProperties, status1Protection) == 6);
static_assert(offsetof(ItemProperties, status2Protection) == 7);
static_assert(offsetof(ItemProperties, status3Granted) == 8);
static_assert(offsetof(ItemProperties, relicEffects1) == 9);
static_assert(offsetof(ItemProperties, relicEffects2) == 10);
static_assert(offsetof(ItemProperties, relicEffects3) == 11);
static_assert(offsetof(ItemProperties, relicEffects4) == 12);
static_assert(offsetof(ItemProperties, relicEffects5) == 13);
static_assert(offsetof(ItemProperties, targeting) == 14);
static_assert(offsetof(ItemProperties, element) == 15);
static_assert(offsetof(ItemProperties, vigorSpeed) == 16);
static_assert(offsetof(ItemProperties, staminaMagicPower) == 17);
static_assert(offsetof(ItemProperties, spellCast) == 18);
static_assert(offsetof(ItemProperties, weaponFlags) == 19);
static_assert(offsetof(ItemProperties, power) == 20);
static_assert(offsetof(ItemProperties, hitRateOrDefense) == 21);
static_assert(offsetof(ItemProperties, elementsAbsorbed) == 22);
static_assert(offsetof(ItemProperties, elementsNullified) == 23);
static_assert(offsetof(ItemProperties, elementsWeak) == 24);
static_assert(offsetof(ItemProperties, status2Set) == 25);
static_assert(offsetof(ItemProperties, evadeMagicBlock) == 26);
static_assert(offsetof(ItemProperties, specialEffect) == 27);
static_assert(offsetof(ItemProperties, price) == 28);

// One table entry: the record's identity as a typed field (the ItemId
// enumerator — identity is a field, never a comment) alongside the packed
// record, which stays sizeof-locked to the ROM bytes. Every generated row
// reads { .id = ItemId::NAME, .record = { ... } }; a compile-time assert
// verifies id == array position for every entry.
struct ItemPropertiesEntry {
    ItemId id;
    ItemProperties record;
};

// The record for an item, from the English-language table. The table is
// language-variant upstream (item_prop_en.dat / item_prop_jp.dat rip as
// separate files); a Language dispatch axis is added when the JP table
// becomes rippable — until then the EN table is the sole backing store.
const ItemProperties& getItemProperties(ItemId id);

// The full 256-entry EN table (ITEM index order), for iteration and
// full-corpus tests.
std::span<const ItemPropertiesEntry> itemPropertiesEn();

}  // namespace ostinato
