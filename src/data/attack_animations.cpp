#include "data/attack_animations.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The generated rows (array-body .inc files). Each carries its identity as a
// typed field; the compile-time asserts below verify identity == position.
constexpr std::array<AttackAnimationPropertiesEntry, 406>
    kAttackAnimationProperties = {{
#include "data/generated/attack_anim_prop_data.inc"
    }};

constexpr std::array<AnimationGraphicsPropertiesEntry, 650>
    kAnimationGraphicsProperties = {{
#include "data/generated/attack_gfx_prop_data.inc"
    }};

constexpr std::array<WeaponAnimationPropertiesEntry, 93>
    kWeaponAnimationProperties = {{
#include "data/generated/weapon_anim_prop_data.inc"
    }};

constexpr std::array<MonsterAttackAnimationPropertiesEntry, 35>
    kMonsterAttackAnimationProperties = {{
#include "data/generated/monster_attack_anim_prop_data.inc"
    }};

constexpr std::array<UsableItemAnimationEntry, 32> kUsableItemAnimations = {{
#include "data/generated/item_anim_ptrs_data.inc"
    }};

constexpr std::array<MonsterOverlapEntry, 384> kMonsterOverlaps = {{
#include "data/generated/monster_overlap_data.inc"
    }};

// Every entry's identity field must equal its array position.
static_assert([] {
    for (std::size_t i = 0; i < kAttackAnimationProperties.size(); ++i) {
        if (kAttackAnimationProperties[i].index != i) return false;
    }
    return true;
}(), "kAttackAnimationProperties index fields must match positions");

static_assert([] {
    for (std::size_t i = 0; i < kAnimationGraphicsProperties.size(); ++i) {
        if (kAnimationGraphicsProperties[i].index != i) return false;
    }
    return true;
}(), "kAnimationGraphicsProperties index fields must match positions");

static_assert([] {
    for (std::size_t i = 0; i < kWeaponAnimationProperties.size(); ++i) {
        if (static_cast<std::size_t>(kWeaponAnimationProperties[i].item) != i) {
            return false;
        }
    }
    return true;
}(), "kWeaponAnimationProperties item fields must match positions");

static_assert([] {
    for (std::size_t i = 0; i < kMonsterAttackAnimationProperties.size(); ++i) {
        if (static_cast<std::size_t>(
                kMonsterAttackAnimationProperties[i].animation) != i) {
            return false;
        }
    }
    return true;
}(), "kMonsterAttackAnimationProperties animation fields must match positions");

// The usable-item table covers ITEM ids $e0..$ff, in order.
static_assert([] {
    for (std::size_t i = 0; i < kUsableItemAnimations.size(); ++i) {
        if (static_cast<std::size_t>(kUsableItemAnimations[i].item) !=
            0xE0 + i) {
            return false;
        }
    }
    return true;
}(), "kUsableItemAnimations must cover ITEM ids $e0..$ff in order");

static_assert([] {
    for (std::size_t i = 0; i < kMonsterOverlaps.size(); ++i) {
        if (static_cast<std::size_t>(kMonsterOverlaps[i].monster) != i) {
            return false;
        }
    }
    return true;
}(), "kMonsterOverlaps monster fields must match positions");

// kItemThrowAnimations lives in the header (namespace scope); its 256 item rows
// are keyed by ITEM id in order.
static_assert([] {
    for (std::size_t i = 0; i < kItemThrowAnimations.size(); ++i) {
        if (static_cast<std::size_t>(kItemThrowAnimations[i].item) != i) {
            return false;
        }
    }
    return true;
}(), "kItemThrowAnimations item fields must match positions");

}  // namespace

const AttackAnimationProperties& attackAnimationProperties(
    std::uint16_t index) {
    assert(index < kAttackAnimationProperties.size() &&
           "attack-animation index out of range");
    return kAttackAnimationProperties[index].record;
}

std::span<const AttackAnimationPropertiesEntry> attackAnimationProperties() {
    return kAttackAnimationProperties;
}

const AnimationGraphicsProperties& animationGraphicsProperties(
    std::uint16_t index) {
    assert(index < kAnimationGraphicsProperties.size() &&
           "animation-graphics index out of range");
    return kAnimationGraphicsProperties[index].record;
}

std::span<const AnimationGraphicsPropertiesEntry> animationGraphicsProperties() {
    return kAnimationGraphicsProperties;
}

const WeaponAnimationProperties& weaponAnimationProperties(ItemId item) {
    const auto raw = static_cast<std::size_t>(item);
    assert(raw < kWeaponAnimationProperties.size() &&
           "item is not a weapon with an animation record");
    return kWeaponAnimationProperties[raw].record;
}

std::span<const WeaponAnimationPropertiesEntry> weaponAnimationProperties() {
    return kWeaponAnimationProperties;
}

const WeaponAnimationProperties& monsterAttackAnimationProperties(
    MonsterAttackAnimation animation) {
    const auto raw = static_cast<std::size_t>(animation);
    assert(raw < kMonsterAttackAnimationProperties.size() &&
           "monster attack-animation row out of range");
    return kMonsterAttackAnimationProperties[raw].record;
}

std::span<const MonsterAttackAnimationPropertiesEntry>
monsterAttackAnimationProperties() {
    return kMonsterAttackAnimationProperties;
}

ItemThrowAnimation itemThrowAnimation(ItemId item) {
    // Every ITEM id ($00..$ff) has a row (the unarmed slot is separate).
    return kItemThrowAnimations[static_cast<std::size_t>(item)].animation;
}

std::span<const ItemThrowAnimationEntry> itemThrowAnimations() {
    return kItemThrowAnimations;
}

std::span<const UsableItemAnimationEntry> usableItemAnimations() {
    return kUsableItemAnimations;
}

std::uint8_t monsterOverlap(MonsterId monster) {
    const auto raw = static_cast<std::size_t>(monster);
    assert(raw < kMonsterOverlaps.size() && "monster id out of range");
    return kMonsterOverlaps[raw].yShift;
}

std::span<const MonsterOverlapEntry> monsterOverlaps() {
    return kMonsterOverlaps;
}

}  // namespace ostinato
