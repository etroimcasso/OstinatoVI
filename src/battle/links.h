// The links one battler makes to another, what they offer, and how the battle lets them go.
//
// Special effects make the links (battle/target_effects.h): Control (Battler::controlling and
// Battler::controlledBy), Love Token (Battler::bodyguard and Battler::guarding), Charm
// (Battler::charming and Battler::charmedBy) and Zinger (BattleState::heldByZinger and
// BattleState::swallowedByZinger). A party member controlling a monster is given the monster's
// control attacks as its menu with controlMenu(); the attack frame fills it again each time the
// monster acts under control, so the rows follow the monster's MP.
//
//     battle::controlMenu(state, controller, monster);
//     const battle::ControlMenuEntry& first = state.at(controller).controlMenu[0];   // its first attack
//
// After every action the battle checks the links, through CommandExecutor: each battler that can no
// longer take part in Control has its link marked as ending (endControlIfUnable()); a battler that
// died, left the field or ran away lets go of Zinger, Love Token and Charm (releaseLinks()); and then
// every Control link marked as ending is let go (releaseEndedControl()).
//
//     battle::endControlIfUnable(state, slot);    // for each battler on the field
//     battle::releaseLinks(state, slot);          // for one that fell or left
//     battle::releaseEndedControl(state, slot);   // for every battler, the last first
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

// The statuses that end a Control link held by or on a battler: Dead, Petrify, Zombie, Berserk,
// Confuse, Sleep, Dance, Stop, Rage and Frozen.
extern const StatusSet kStatusesThatEndControl;

// A battler with any of kStatusesThatEndControl has its Control link marked as ending
// (Battler::controlEnding), whichever end of the link it holds. A battler with no link is left as
// it is.
void endControlIfUnable(BattleState& state, BattlerSlot slot);

// A Control link marked as ending is let go (releaseControl()), whether the battler is the monster
// held or the party member holding it. A link not marked is left alone.
void releaseEndedControl(BattleState& state, BattlerSlot slot);

// Let go of the Control link between `controller` and `monster`. On a US cartridge the controller's
// menu goes back to its own commands; it stops chanting, straight off the status; the monster is
// drawn facing its own way again. Both empty their gauges once the action is over, drop every action
// they had waiting, and do not open their menus.
void releaseControl(BattleState& state, BattlerSlot controller, BattlerSlot monster);

// A battler that died, left the field or ran lets go of what it is linked to. The party member a
// Zinger swallowed brings the monster back: two actions jump the line, one keeping the monster off
// the field without letting the battle end, one bringing it back with the HP it had, fading in from
// the top. A Love Token or a Charm is let go at both ends, whichever end the battler held.
void releaseLinks(BattleState& state, BattlerSlot slot);

}  // namespace ostinato::battle
