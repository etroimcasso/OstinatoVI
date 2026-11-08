#include "battle/battle_hooks.h"

#include "battle/attacker_effects.h"
#include "battle/damage.h"
#include "battle/status.h"

namespace ostinato::battle {

void BattleHooks::baseDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    battle::baseDamage(state, attack, attacker);
}

void BattleHooks::attackerEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker,
                                 BattleScriptBuilder& script) {
    battle::attackerEffect(state, attack, attacker, script);
}

void BattleHooks::scaleDamage(BattleState&, AttackInFlight& attack) {
    battle::scaleDamage(attack);
}

void BattleHooks::targetDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                               BattleScriptBuilder& script) {
    battle::targetDamage(state, attack, attacker, target, script);
}

void BattleHooks::applyDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker,
                              BattleScriptBuilder& script) {
    battle::applyDamage(state, attack, attacker, script);
}

void BattleHooks::statusEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target) {
    battle::statusEffect(state, attack, attacker, target);
}

void BattleHooks::applyPendingStatuses(BattleState& state) {
    battle::applyStatusChanges(state);
}

}  // namespace ostinato::battle
