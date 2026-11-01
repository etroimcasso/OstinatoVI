// The battle's own AttackHooks: the parts of an attack the battle works out itself, plugged into the
// attack frame (battle/attack.h).
//
//     BattleHooks hooks;
//     CommandExecutor executor(hooks, text, sprites);   // every command's attacks do real damage
//
// Each damage member is one call into battle/damage.h: the base damage, the multiplier, the damage
// to each target and the damage taken and shown. The statuses and the special effects are still
// AttackHooks' own, which do nothing, so the statuses the damage sets and clears — a battler brought
// to 0 HP set to die, a sleeping target woken by a blow — wait in Battler::statusesToSet and
// statusesToClear without taking effect.
#pragma once

#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::battle {

class BattleHooks : public AttackHooks {
public:
    void baseDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker) override;
    void scaleDamage(BattleState& state, AttackInFlight& attack) override;
    void targetDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                      BattleScriptBuilder& script) override;
    void applyDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker,
                     BattleScriptBuilder& script) override;
};

}  // namespace ostinato::battle
