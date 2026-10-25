// Adding and removing a battler's actions.
//
// Every action the battle creates goes through one of four functions, which differ in where it
// waits:
//
//     addAction(state, slot, action);           // the end of the battler's own list
//     addCounterAction(state, slot, action);    // its counterattack list
//     addActionOrCounter(state, slot, action);  // the counterattack list while one is running
//     addImmediateAction(state, action);        // the line that runs before anything else
//
// A command's MP cost is worked out when it is added. A battle command chosen for a battler's
// turn also gets its advance wait: addAutomaticAction() does both.
//
// The rules are specified in docs/contracts/battle_core.md.
#pragma once

#include <cstdint>

#include "battle/action_executor.h"
#include "battle/action_queue.h"
#include "battle/battle_state.h"

namespace ostinato::battle {

// Add `action` to the end of the battler's list.
void addAction(BattleState& state, BattlerSlot slot, Action action);

// Add `action` to the battler's counterattack list. A battler with no counterattack waiting also
// joins the counterattack line.
void addCounterAction(BattleState& state, BattlerSlot slot, Action action);

// Add `action` as a counterattack while a counterattack is running, and to the battler's own
// list otherwise.
void addActionOrCounter(BattleState& state, BattlerSlot slot, Action action);

// Add `action` to the line of actions that run before anything else, in the order they were added.
void addImmediateAction(BattleState& state, Action action);

// Lengthen the battler's advance wait by the command's wait and have the command aimed. The wait
// adds to whatever is already set — one for each pending command — and stops at 254; a wait not
// yet set counts as 255.
void addAdvanceWait(BattleState& state, ActionExecutor& executor, BattlerSlot slot, CommandAction& command);

// Give a battle command chosen for the battler its advance wait and aim, and add it to the
// battler's list.
void addAutomaticAction(BattleState& state, ActionExecutor& executor, BattlerSlot slot, CommandAction command);

// The MP `command` costs `slot`: a Magic, X-Magic, Lore or Summon spell costs a party member what
// its spell list says and a monster the spell's base cost; everything else costs nothing, and so
// does any action Mimic adds.
std::uint8_t commandMpCost(const BattleState& state, BattlerSlot slot, const CommandAction& command);

// Take the first action off the battler's list, or off its counterattack list.
void removeAction(BattleState& state, BattlerSlot slot);
void removeCounterAction(BattleState& state, BattlerSlot slot);

// Drop every action the battler has chosen (its counterattacks stay) and take it out of the
// advance-wait line. A full gauge is set to fill again at its next update, and a party member goes
// back to standing normally.
void removeAllActions(BattleState& state, BattlerSlot slot);

// Put the battler in the advance-wait line. Its next pending action waits out its advance wait.
void joinAdvanceWaitLine(BattleState& state, BattlerSlot slot);

// A battler whose gauge has filled: a party member's battle menu opens, if it is on the ground
// and menus are allowed; a monster joins the advance-wait line.
void openMenuOrJoinLine(BattleState& state, BattlerSlot slot);

// While the party is running and a character has just got away, start the party's escape — once.
// Not in the colosseum.
void startPartyEscape(BattleState& state);

// The party's escape, without the checks: it shows "can't run away" when the party cannot run.
void queuePartyEscape(BattleState& state);

// Doom strikes the battler.
void condemnedCountRunsOut(BattleState& state, BattlerSlot slot);

}  // namespace ostinato::battle
