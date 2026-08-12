#include "data/battle_tables.h"

#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The dance/background tables are in key order, so a key indexes its own row.
// Everything the accessors do rests on that, so it is checked at compile time.
constexpr bool danceOrder() {
    for (std::size_t i = 0; i < kDanceBackgrounds.size(); ++i) {
        if (kDanceBackgrounds[i].dance != static_cast<DanceId>(i)) {
            return false;
        }
    }
    return true;
}

constexpr bool backgroundOrder() {
    for (std::size_t i = 0; i < kBackgroundDances.size(); ++i) {
        if (kBackgroundDances[i].background !=
            static_cast<BattleBackgroundId>(i)) {
            return false;
        }
    }
    return true;
}

constexpr bool itemTypeOrder() {
    for (std::size_t i = 0; i < kItemTypeBattleFlags.size(); ++i) {
        if (kItemTypeBattleFlags[i].type != static_cast<ItemType>(i)) {
            return false;
        }
    }
    return true;
}

constexpr bool characterOrder() {
    for (std::size_t i = 0; i < kDesperationAttacks.size(); ++i) {
        if (kDesperationAttacks[i].character != static_cast<CharacterId>(i)) {
            return false;
        }
    }
    return true;
}

static_assert(danceOrder(), "kDanceBackgrounds must be in dance order");
static_assert(backgroundOrder(),
              "kBackgroundDances must be in background order");
static_assert(itemTypeOrder(),
              "kItemTypeBattleFlags must be in item-type order");
static_assert(characterOrder(),
              "kDesperationAttacks must be in character order");

}  // namespace

BattleBackgroundId danceBackground(DanceId dance) {
    const auto i = static_cast<std::size_t>(dance);
    assert(i < kDanceBackgrounds.size() && "dance out of range");
    return kDanceBackgrounds[i].background;
}

DanceId backgroundDance(BattleBackgroundId background) {
    const auto i = static_cast<std::size_t>(background);
    assert(i < kBackgroundDances.size() && "battle background out of range");
    return kBackgroundDances[i].dance;
}

std::int16_t equipEvadeBoost(std::uint8_t rating) {
    assert(rating < kEquipEvadeBoost.size() && "evade rating out of range");
    return kEquipEvadeBoost[rating].boost;
}

std::uint8_t aiCommandSize(AiScriptCommand command) {
    const auto first = static_cast<std::uint8_t>(kAiCommandSizes.front().command);
    const auto value = static_cast<std::uint8_t>(command);
    assert(value >= first && "not an AI script command");
    const auto i = static_cast<std::size_t>(value - first);
    assert(i < kAiCommandSizes.size() && "not an AI script command");
    return kAiCommandSizes[i].size;
}

ItemTypeBattleFlags itemTypeBattleFlags(ItemType type) {
    const auto i = static_cast<std::size_t>(type);
    assert(i < kItemTypeBattleFlags.size() && "item type out of range");
    return kItemTypeBattleFlags[i].flags;
}

AttackId desperationAttack(CharacterId character) {
    const auto i = static_cast<std::size_t>(character);
    assert(i < kDesperationAttacks.size() && "character out of range");
    return kDesperationAttacks[i].attack;
}

}  // namespace ostinato
