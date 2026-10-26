// The battle core: the two clocks that drive a battle.
//
// A battle runs on two clocks at once. Battle time fills the ATB gauges and runs the status
// timers; it moves on every displayed frame, attack animations included, and advances the gauges
// at most once every two frames. The battle loop takes turns: one step per frame, in which it
// runs the most urgent thing waiting — an action that interrupted the line, a counterattack, a
// battler finishing its advance wait, a battler's turn — and checks whether the battle is over.
// While a battle script plays, the loop waits for it and time keeps running.
//
// A host drives both from its frame tick:
//
//     tickBattleTime(state, presentation.timeRuns(), executor);
//     if (presentation.idle()) {
//         if (auto outcome = stepBattle(state, executor)) { ... }  // the battle is over
//     }
//     presentation.consume(state.requests);
//     state.requests.clear();
//
// BattleSim (battle/battle_sim.h) does this for a whole battle, from its start to its end.
//
// The rules, and the line in the original each comes from, are specified in
// docs/contracts/battle_core.md.
#pragma once

#include <optional>

#include "battle/action_executor.h"
#include "battle/battle_state.h"

namespace ostinato::battle {

// Advance battle time by one displayed frame. `timeRunning` is false while the presentation has
// stopped time (a battle event, the entry animation). Time also stops while a command's sub-menu
// is open in wait mode (BattleState::timeStoppedByMenu).
void tickBattleTime(BattleState& state, bool timeRunning, ActionExecutor& executor);

// Run the battle loop for one frame: from where it is waiting until it next waits — for the next
// frame or for a battle script to finish. Call it only when no battle script is playing. Returns
// the outcome once the battle is over, and keeps returning it.
std::optional<BattleOutcome> stepBattle(BattleState& state, ActionExecutor& executor);

// Work out the battler's ATB rate and status-timer step from its speed and Haste or Slow.
void recalcAtbRate(BattleState& state, BattlerSlot slot);

// Work out who is still fighting on each side (BattleState::alive and the counts).
void refreshAliveSets(BattleState& state);

// Work out how hard it is to run from the monsters standing, and whether the party can run.
void refreshRunDifficulty(BattleState& state);

// The checks after every command, which a battle's start also runs once. checkTheFallen() deals
// with the battlers the command felled — HP to 0, links released, Reraise, a Banon who fell;
// settleAfterAction() then empties the gauges of the fallen, works out what the command left
// pending for each battler (PendingUpdates), and opens or holds back their next turn.
void checkTheFallen(BattleState& state, ActionExecutor& executor);
void settleAfterAction(BattleState& state, ActionExecutor& executor);

}  // namespace ostinato::battle
