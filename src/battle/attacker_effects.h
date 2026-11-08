// The special effects that act on an attack and its attacker before the hit check: a Thiefknife
// turning a blow into a steal, the Dice rolling the damage, Ogre Nix breaking, Spiraler giving up
// its caster, Warp ending the battle, GP Rain paying out the party's gil.
//
// The attack frame calls attackerEffect() once per attack, after the damage is split and the hits
// in the back are found, and before any target is checked (battle/attack.h). Every effect works on
// the attack in flight and the battle state; what it leaves is read by the rest of the frame — the
// damage, the multiplier, the targets hit, a spell to cast next, a message to show.
//
//     BattleHooks hooks;                 // calls attackerEffect() at the frame's seam
//     runAttack(state, hooks, script, attack, attacker);
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_effects.md.
#pragma once

#include <cstdint>

#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "game/character_record.h"

namespace ostinato::battle {

// The attack's special effect on its attacker (AttackInFlight::effect). An attack with no effect,
// or with one that acts only on its targets, is left as it is. Exploder writes into `script`: it is
// shown on its user alone before it is shown on its targets.
void attackerEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder& script);

// Hide `slot` and set it to die once the attack's statuses take effect, and mark it to answer
// itself unless it is already waiting to answer someone. `waitsToAnswer` is whether the mark is
// left ready (Battler::retaliationPending).
void hideAndDie(BattleState& state, BattlerSlot slot, bool waitsToAnswer);

// Take `amount` gil from the party, at most 65,535; returns how much was taken. A party with less
// than `amount` gives up all it has.
std::uint32_t takeGil(game::Party& party, std::uint32_t amount);

}  // namespace ostinato::battle
