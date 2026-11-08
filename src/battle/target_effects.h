// The special effects that act on each target an attack hits: the instant kills, Steal and
// Metamorph taking items, Control and Sketch catching a monster, Seize and Love Token linking two
// battlers, Doom on the undead, Rippler swapping statuses.
//
// The attack frame calls targetEffect() for each target hit, after the attack's statuses on it are
// worked out and before its damage (battle/attack.h); a spell bounced off Reflect calls it again for
// each target it bounces to. An effect that misses the target returns true: the target is shown
// "Miss" and the frame does not work out its damage.
//
//     BattleHooks hooks;                 // calls targetEffect() at the frame's seam
//     runAttack(state, hooks, script, attack, attacker);
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_effects.md.
#pragma once

#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::battle {

// The attack's special effect (AttackInFlight::effect) on `target`, made by `attacker`. Returns true
// when the effect missed the target, which is then also among the battlers shown "Miss"
// (AttackScratch::missed). An attack with no effect, or with one that acts only on its attacker,
// leaves the target as it is. The instant kills, Sniper, Metamorph and Sketch write into `script`.
//
// Every target hit has already been marked to answer the attacker (recordLastHit()), which is what
// the effects that set their user to die rely on: that mark is left as it is.
bool targetEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                  BattleScriptBuilder& script);

}  // namespace ostinato::battle
