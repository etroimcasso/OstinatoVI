// The items the party can use in a battle: its whole inventory, the weapon and shield each party
// member holds, and which tools it owns.
//
// The battle's Item, Throw and Tools menus show these, and a party member can swap the item in
// either hand for one from the list. Each item carries what the menus need: whether it can be
// used in battle or thrown, whether it is a tool, a weapon or a shield, how it targets, how many
// the party has, and which party members can hold it.
//
//     loadBattleInventory(state);   // when the battle starts, after the party is loaded
//     state.inventory.items[3].quantity;
//     state.inventory.weaponHands[0].item;   // what party member 0 holds in their weapon hand
//     state.inventory.tools.has(ItemId::DRILL);
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "game/character_record.h"
#include "ostinato/battle_slot_mask.h"
#include "ostinato/item_id.h"
#include "ostinato/targeting.h"

namespace ostinato::battle {

struct BattleState;

// One item as the battle's menus see it.
struct BattleItem {
    ItemId item = ItemId::EMPTY;
    bool usableInBattle = false;  // the Item command can use it
    bool throwable = false;       // the Throw command can throw it
    bool isTool = false;
    bool isWeapon = false;
    bool isShield = false;
    Targeting targeting;
    std::uint8_t quantity = 0;
    BattleSlotMask equippableBy;  // the party slots whose member can hold it in a hand
};

// The tools the party owns, Noiseblaster to Autocrossbow.
struct ToolSet {
    std::uint8_t owned = 0;

    static constexpr bool isTool(ItemId item) {
        return item >= ItemId::NOISEBLASTER && item <= ItemId::AUTOCROSSBOW;
    }
    constexpr bool has(ItemId tool) const {
        return isTool(tool) && (owned & bit(tool)) != 0;
    }
    constexpr void add(ItemId tool) {
        if (isTool(tool)) {
            owned |= bit(tool);
        }
    }

private:
    static constexpr std::uint8_t bit(ItemId tool) {
        return static_cast<std::uint8_t>(1u << (static_cast<int>(tool) - static_cast<int>(ItemId::NOISEBLASTER)));
    }
};

struct BattleInventory {
    std::array<BattleItem, game::kInventorySlots> items{};  // the party's inventory, slot for slot
    std::array<BattleItem, 4> weaponHands{};  // by party slot
    std::array<BattleItem, 4> shieldHands{};
    ToolSet tools;
};

// The battle's view of one item, for the party as it stands in `state`.
BattleItem battleItem(const BattleState& state, ItemId item, std::uint8_t quantity);

// Build the battle inventory from the party's inventory and what each party member holds.
void loadBattleInventory(BattleState& state);

// Note what the party knows of its other abilities for the battle's menus: how many swdtechs and
// blitzes, which dances, and which rages, in order.
void loadKnownSkills(BattleState& state);

}  // namespace ostinato::battle
