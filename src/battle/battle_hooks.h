// The battle's own AttackHooks: the parts of an attack the battle works out itself, plugged into the
// attack frame (battle/attack.h).
//
//     BattleHooks hooks;
//     CommandExecutor executor(hooks, text, sprites);   // every command's attacks do real damage
//
// Each damage member is one call into battle/damage.h: the base damage, the multiplier, the damage
// to each target and the damage taken and shown. The two status members are one call each into
// battle/status.h: an attack's statuses on each target it hits, and every battler's waiting statuses
// taking effect once the attack is over, so a battler brought to 0 HP is dead when the attack ends.
// The special effect on the attacker is one call into battle/attacker_effects.h, the special effect
// on each target one call into battle/target_effects.h, and a controlled monster's controller has
// its menu filled again by one call into battle/links.h.
#pragma once

#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::battle {

class BattleHooks : public AttackHooks {
public:
    void baseDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker) override;
    void attackerEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker,
                        BattleScriptBuilder& script) override;
    void scaleDamage(BattleState& state, AttackInFlight& attack) override;
    void controlMenu(BattleState& state, BattlerSlot controller, BattlerSlot monster) override;
    void targetDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                      BattleScriptBuilder& script) override;
    void applyDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker,
                     BattleScriptBuilder& script) override;
    void statusEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target) override;
    bool targetEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                      BattleScriptBuilder& script) override;
    void applyPendingStatuses(BattleState& state) override;
};

}  // namespace ostinato::battle
