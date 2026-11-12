// The close of a battle once it has an outcome: what every ending but two runs before the party is
// handed back.
//
// The wrap-up closes the party's menus, lets a beat pass, puts away what every party member still
// holds and merges the items obtained into the inventory:
//
//     battle::wrapUpBattle(state, executor);
//
// In order:
//   1. the Morph supply is kept as the meter now stands (saveMorphSupply() in battle/morph.h);
//   2. for party slots 3 to 0, the Morph meter shown is emptied and the slot's battle menu closes
//      (a CloseBattleMenu request each);
//   3. no battle menu may open again (BattleState::menusDisabled);
//   4. 32 frames pass (a Wait script of kWrapUpWaitFrames);
//   5. every party member with an item still in hand puts it with the items obtained, whether or
//      not it was marked as holding one, so an item chosen for Item or Throw and never used comes
//      back (putAwayItemsObtained() in battle/battle_inventory.h);
//   6. the items obtained are merged into the inventory (mergeItemsObtained() in
//      battle/items_obtained.h);
//   7. equipment changed in the battle's Item menu takes effect (applyEquipmentChanges() in
//      battle/party_setup.h).
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_rewards.md.
#pragma once

#include "battle/action_executor.h"
#include "battle/battle_state.h"

namespace ostinato::battle {

// The frames the wrap-up waits between closing the menus and putting the items away.
inline constexpr int kWrapUpWaitFrames = 32;

void wrapUpBattle(BattleState& state, ActionExecutor& executor);

}  // namespace ostinato::battle
