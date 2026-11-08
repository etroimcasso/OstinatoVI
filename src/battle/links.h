// The links one battler makes to another and what they offer: a party member controlling a monster
// is given the monster's control attacks as its menu.
//
// Control takes hold through its special effect on the monster (battle/target_effects.h), which
// fills the controller's menu with controlMenu(); the attack frame fills it again each time the
// monster acts under control, so the rows follow the monster's MP.
//
//     battle::controlMenu(state, controller, monster);
//     const battle::ControlMenuEntry& first = state.at(controller).controlMenu[0];   // its first attack
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_effects.md.
#pragma once

#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::battle {

// Fill `controller`'s Battler::controlMenu from `monster`'s four control attacks: each row the attack,
// whether the monster has the MP it costs, and its targeting. A slot with no attack is a row that
// cannot be chosen. A monster with 65,535 MP can cast none of them: the original adds one to the MP
// before comparing, and the sum wraps to 0.
void controlMenu(BattleState& state, BattlerSlot controller, BattlerSlot monster);

}  // namespace ostinato::battle
