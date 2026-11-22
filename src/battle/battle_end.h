// The close of a battle once it has an outcome: what each ending runs before the party is handed back.
//
// The end plays in parts (BattleEndPart in battle/battle_state.h), and battle time runs through it as it
// runs through a fight. Each part runs to the battle scripts it asks for and stops; the next part runs
// once those scripts have played, so their frames pass first. BattleSim (battle/battle_sim.h) calls
// continueBattleEnd() through ActionExecutor::continueBattleEnd() on every tick the presentation is idle,
// from the tick the battle finds its outcome, and writes the party back (writeBack() in
// battle/battle_start.h) once it returns true:
//
//     if (battle::continueBattleEnd(state, executor, outcome)) {
//         const battle::BattleResult handedBack = battle::writeBack(state);
//     }
//
// playBattleEnd() plays the whole end with no frame between its parts.
//
// What each outcome runs, part by part:
//
//   BattleWon                        StepBack, Celebration, CloseMenus, PutAway, Rewards, Drops (below);
//                                    in the colosseum PutAway ends with the prize, and the win with it
//   BattleLost                       CloseMenus, PutAway ending with the outcome's message
//   BattleEnded RanOutOfTime,        CloseMenus, PutAway
//     EndNow, TimerExpired,
//     EatenByZoneEater
//   BattleEnded FinalBattleNextStage StepBack (the acting battler steps back), CloseMenus, PutAway
//   BattleEnded GauLeapt             Gau leaves the party, then the rages are learned — no wrap-up, and
//                                    nothing that stops
//   BattleEnded RagesLearned         the rages are learned — no wrap-up, and nothing that stops
//
// The wrap-up (wrapUpBattle(), the CloseMenus and PutAway parts) closes the party's menus, lets a beat
// pass, puts away what every party member still holds and merges the items obtained into the inventory.
// In order:
//   1. the Morph supply is kept as the meter now stands (saveMorphSupply() in battle/morph.h);
//   2. for party slots 3 to 0, the Morph meter shown is emptied and the slot's battle menu closes
//      (a CloseBattleMenu request each);
//   3. no battle menu may open again (BattleState::menusDisabled);
//   4. 32 frames pass (a Wait script of kWrapUpWaitFrames), battle time running: a morph whose meter
//      runs out in them leaves the Morph supply at nothing;
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
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"

namespace ostinato::battle {

// The frames the wrap-up waits between closing the menus and putting the items away.
inline constexpr int kWrapUpWaitFrames = 32;

// Below this, a monster's random draw at the win gives its rare drop; otherwise its common one.
inline constexpr std::uint8_t kRareDropBelow = 32;

// The wrap-up's two parts in a row, with no frame between them.
void wrapUpBattle(BattleState& state, ActionExecutor& executor);

// The abilities `actor` learns on reaching `level` (learnAbilitiesAtLevel() in battle/rewards.h),
// and the first technique and the first blitz of the win told of; `shown` records which have been.
void learnLevelAbilities(BattleState& state, CharacterPropId actor, std::uint8_t level, AbilityMessagesShown& shown);

// Every level `record` has earned, one at a time: each gained (levelUp() in battle/leveling.h) and its
// abilities learned with learnLevelAbilities(). While `tellLevel` is set, the first level gained is
// told of as "gained a level" naming party slot `partySlot` — the win sets it for each member, so
// one message covers every level that member gains. Returns whether the message is still to tell:
// `tellLevel` when no level was earned, false otherwise.
//
//     battle::AbilityMessagesShown shown;
//     battle::reachEarnedLevels(state, state.party.characters[record], slot, true, shown);
bool reachEarnedLevels(BattleState& state, game::CharacterRecord& record, std::uint32_t partySlot, bool tellLevel,
                       AbilityMessagesShown& shown);

// Play `outcome`'s end on from where it stands (BattleState::ending, set on the first call) to the next
// battle script a part asks for. Returns true once every part has run and the last one's scripts have
// played — on a call that finds nothing left to run.
bool continueBattleEnd(BattleState& state, ActionExecutor& executor, const BattleOutcome& outcome);

// Play `outcome`'s whole end at once: continueBattleEnd() until it returns true.
void playBattleEnd(BattleState& state, ActionExecutor& executor, const BattleOutcome& outcome);

}  // namespace ostinato::battle
