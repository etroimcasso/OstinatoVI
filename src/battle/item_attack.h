// The attack an item makes when it is used as itself: a consumable used with Item, a weapon thrown
// with Throw, a tool worked with Tools.
//
// An item that casts a spell (a rod, an elemental shield, a thrown Fire Skean, the Bio Blaster)
// makes that spell instead; initTargeting() tells the two apart. For an item that is the attack
// itself, itemAttack() builds the attack from the item's record:
//
//     const bool itemIsTheAttack = initTargeting(attack, BattleCommandId::TOOLS, toolAsAttack);
//     if (itemIsTheAttack) {
//         itemAttack(state, attack, actor, ItemId::DRILL, ItemUse::ThrownOrWorked);
//     }
//
// The rules, and the line in the original each comes from, are specified in
// docs/contracts/battle_commands.md § Items.
#pragma once

#include <cstdint>

#include "battle/attack.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "ostinato/item_id.h"

namespace ostinato::battle {

// How the item is used: as a consumable (Item), or thrown or worked as a tool (Throw, Tools).
enum class ItemUse : std::uint8_t {
    Consumed,
    ThrownOrWorked,
};

// Replace the attack's record with the attack `item` makes. A consumable heals, harms and changes
// statuses as its record says and is never physical; a thrown weapon is a physical blow at the
// item's battle power with the user's vigor and level behind it. Either ignores defense and Reflect
// and cannot be dodged. A tool can be dodged, hits at its own hit rate, takes defense into account
// and adds its own effect — Chain Saw's one-in-four instant kill draws a random number.
void itemAttack(BattleState& state, AttackInFlight& attack, BattlerSlot user, ItemId item, ItemUse use);

}  // namespace ostinato::battle
