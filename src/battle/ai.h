// A monster's AI script run on its turn and when it is attacked.
//
// A battler with a script reads one of its two parts (data/ai_scripts.h): the turn part when its
// turn comes, the counterattack part when it has been attacked. Reading a part queues the actions
// it reaches — attacks, commands, dialogs, formation changes, variable and switch changes, effects —
// as the battler's actions, or as its counterattacks while a counterattack is running. Nothing here
// shows anything; every picture is the queued actions' own.
//
//     readyAi(state, slot);                                  // when the monster takes the slot
//     runAiTurn(state, executor, slot);                      // its turn
//     runAiCounter(state, executor, slot, BattlerSet::of(attacker));  // it was attacked
//
// How a part is read:
//
//   - Every run starts at the top of the part. A block's conditions are tested in order; a block
//     whose condition fails is skipped to its endIf(), and the run counts the block as skipped and
//     goes on after it. The part's end() after a failed block ends the run.
//   - A block that acts goes on to its endIf(), which ends the run: the next run starts again from
//     the top. A condition after acting steps in the same block tests the battle as it is then —
//     the actions queued before it have not happened yet.
//   - wait() ends the run and remembers the step after it and how many blocks the run had skipped.
//     The next run reads from the top as always; when it comes to a step that acts having skipped
//     that same number of blocks, it goes on from the remembered step instead. It does so once.
//   - The targets a step aims at are the run's: none on a turn, the attacker on a counterattack.
//     setTarget() and a condition that names a target change them, and they stay for every later
//     action of the run; a skipped block puts back the ones the run started with.
//   - A confused battler's attacks, commands and items go with no targets, and are aimed when
//     queued. Every battle command takes its advance wait and its aim as it is queued, except the
//     AI's Roulette, which takes neither.
//
// The rules, and the line in the original each comes from, are specified in
// docs/contracts/battle_ai.md.
#pragma once

#include "battle/action_executor.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::battle {

// Set where the two parts of `slot`'s script start, from Battler::aiScript: the turn part, and the
// counterattack part when the script has one. A script with no counterattack part leaves the one
// the slot already holds. Nothing else is reset — where the parts stopped, the battler's own
// variable and timer all stay as the slot had them. A battler with no script is left as it is.
void readyAi(BattleState& state, BattlerSlot slot);

// `actor`'s turn: its turn part is read with no targets and queues what it reaches as `actor`'s
// actions. `executor` aims the commands it queues.
void runAiTurn(BattleState& state, ActionExecutor& executor, BattlerSlot actor);

// `actor` answers an attack: its counterattack part is read with `answering` as its targets (the
// battler it answers, or none for a battler answering its own death) and queues what it reaches as
// counterattacks while one is running.
//
// The part is read once per battle step: a second call in the same step does nothing. The run is
// restricted — only a block opened by ifMonstersDead() or always() may act — when the battler died
// this step without being attacked by another, when Quick is running for another battler, when
// the statuses it is answering with hold Sleep, Confuse, Berserk, Stop or Frozen, or when it is
// charmed.
void runAiCounter(BattleState& state, ActionExecutor& executor, BattlerSlot actor, BattlerSet answering);

}  // namespace ostinato::battle
