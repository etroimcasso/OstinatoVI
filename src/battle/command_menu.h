// Which of a party member's battle commands can be chosen right now.
//
// A party member's four commands are set when the battle starts (buildCommandList() in
// battle/party_setup.h). Whenever something changes what it can do — new equipment from the
// battle's Item menu, Morph or Revert, becoming an Imp, being Muted — the battle refreshes them:
//
//     refreshCommandMenu(state, slot);
//     state.at(slot).commands[0].usable;   // shown in white, and choosable
//
// The menu is marked to be drawn again (BattleState::menusToRefresh). The rules, and the line in
// the original each comes from, are specified in docs/contracts/battle_commands.md § The command
// menu.
#pragma once

#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "ostinato/targeting.h"

namespace ostinato::battle {

// Morph needs at least this much in the party's Morph supply.
inline constexpr int kMorphSupplyToMorph = 16;

// The targeting Fight and Capture take in the menu while an Offering is worn: the cursor starts on
// half of the other side and stays on that side.
inline constexpr Targeting kOfferingTargeting =
    Targeting::of(TargetFlags::ENEMY, TargetFlags::INIT_HALF, TargetFlags::ONE_SIDE);

// Decide which of the party member's commands can be chosen, and mark its menu to be drawn again.
// An Imp can use only the commands an Imp may use. Morph needs the Morph supply; Runic needs a
// Runic weapon and Bushido a Bushido weapon in either hand; Magic, X-Magic and Lore cannot be used
// while Muted. With an Offering, Fight and Capture take kOfferingTargeting, and keep it.
void refreshCommandMenu(BattleState& state, BattlerSlot slot);

}  // namespace ostinato::battle
