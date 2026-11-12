// The close of a battle once it has an outcome: what each ending runs before the party is handed back.
//
// The battle loop hands its outcome here through ActionExecutor::playBattleEnd() (CommandExecutor
// calls playBattleEnd()), and then writes the party back (writeBack() in battle/battle_start.h):
//
//     battle::playBattleEnd(state, executor, outcome);
//
// What each outcome runs:
//
//   BattleWon                        the win, whole (below), the wrap-up inside it
//   BattleLost                       the wrap-up, then the outcome's message
//   BattleEnded RanOutOfTime,        the wrap-up
//     EndNow, TimerExpired,
//     EatenByZoneEater
//   BattleEnded FinalBattleNextStage the acting battler steps back, then the wrap-up
//   BattleEnded GauLeapt             Gau leaves the party, then the rages are learned — no wrap-up
//   BattleEnded RagesLearned         the rages are learned — no wrap-up
//
// The wrap-up (wrapUpBattle()) closes the party's menus, lets a beat pass, puts away what every party
// member still holds and merges the items obtained into the inventory. In order:
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
// The win, in order: the acting battler steps back; the party celebrates (every menu closes, then,
// unless the formation turns the fanfare off or Shadow has just left, the fanfare — not when the
// field's music plays on — and battle event 1); the wrap-up; in the colosseum the prize, told of,
// and nothing else. Otherwise: the experience each ally gets, told of; for each living party member,
// last slot first, the first Cat Hood doubling the gold, Terra's Morph supply, the experience (twice
// with an Exp. Egg), and for the first twelve characters the spells their equipment and esper teach;
// the magic points, told of when anyone learned with them; for each living party member, last slot
// first, the levels it reaches with what each teaches, then the spells it has just learned; the lores
// Strago saw; Mog's dance; the Cursed Shield's curse lifted; the drops, highest monster slot first,
// each merged and told of; the gold, told of and added to the party's gil. The rules are in
// battle/rewards.h; every message is a RunBattleScript holding one BattleMessage.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_rewards.md.
#pragma once

#include <cstdint>

#include "battle/action_executor.h"
#include "battle/battle_state.h"

namespace ostinato::battle {

// The frames the wrap-up waits between closing the menus and putting the items away.
inline constexpr int kWrapUpWaitFrames = 32;

// Below this, a monster's random draw at the win gives its rare drop; otherwise its common one.
inline constexpr std::uint8_t kRareDropBelow = 32;

void wrapUpBattle(BattleState& state, ActionExecutor& executor);

// Play out `outcome`, the battle's outcome.
void playBattleEnd(BattleState& state, ActionExecutor& executor, const BattleOutcome& outcome);

}  // namespace ostinato::battle
