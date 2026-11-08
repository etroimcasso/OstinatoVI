// Morph's meter: how long a morphed party member stays morphed, and what is left of the party's
// Morph supply afterward.
//
// When Morph goes on, the battle starts a meter at its fullest and works out how fast it runs down
// from the party's Morph supply (game::Party::morphSupply): the smaller the supply, the faster it
// runs. The meter runs down on every time update; when it runs out the supply is gone and the
// party member reverts. A Revert before then keeps the part of the supply the meter had left.
//
//     battle::morphMenuChanged(state, slot);   // Morph or Revert just went on or came off
//     battle::runDownMorph(state, slot);       // a time update, for the morphed battler
//     battle::saveMorphSupply(state);          // the supply as the meter now stands
//
// The battle calls the first two itself through CommandExecutor (its refreshMorph() and
// tickMorph()), and the battle's end calls the third.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_effects.md § Morph.
#pragma once

#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::battle {

// After Morph or Revert: unless a counterattack is running, a battler whose gauge is full may open
// its menu again. Morph and Revert trade places in its menu rows. A battler now morphed starts the
// meter, unless someone is morphed already or the battle keeps Terra morphed throughout; one now
// reverted keeps what the meter had left of the supply, shows no meter, and has its commands
// refreshed (refreshCommandMenu() in battle/command_menu.h).
void morphMenuChanged(BattleState& state, BattlerSlot slot);

// One time update of the morphed battler's meter. When it runs out, the supply is used up, no one
// is morphed, and a Revert is added as the battler's counterattack (addCounterAction() in
// battle/actions.h). The meter shown (Battler::morphGauge) follows it.
void runDownMorph(BattleState& state, BattlerSlot slot);

// Keep the part of the party's Morph supply the meter has left: the supply times the meter's high
// byte, over 256. Nothing changes while no one is morphed.
void saveMorphSupply(BattleState& state);

}  // namespace ostinato::battle
