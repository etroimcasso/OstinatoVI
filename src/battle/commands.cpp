#include "battle/commands.h"

#include "battle/targeting.h"
#include "data/battle_tables.h"
#include "ostinato/attack_flags.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "ostinato/weapon_flags.h"

namespace ostinato::battle {

namespace {

// With an Offering, Fight makes eight attacks rather than two.
constexpr int kFightAttacksAfterTheFirst = 1;
constexpr int kOfferingAttacksAfterTheFirst = 7;

// An Imp monster without its Imp critical hits this hard.
constexpr std::uint8_t kImpMonsterPower = 1;

}  // namespace

StatusSet itemStatuses(const ItemProperties& properties) {
    return StatusSet{{properties.hitRateOrDefense, properties.elementsAbsorbed.bits, properties.elementsNullified.bits,
                      properties.elementsWeak.bits}};
}

std::uint8_t attackerLevel(const BattleState& state, const AttackInFlight& attack, BattlerSlot attacker) {
    return state.at(attack.scratch.sketcher.value_or(attacker)).level;
}

void initAttacker(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    const Battler& battler = state.at(attacker);
    attack.attackerStat = attack.record.traits.has(AttackTrait::PHYSICAL) ? battler.vigor : battler.magicPower;
    attack.scratch.weaponSpellcast = {};
    attack.attackerLevel = attackerLevel(state, attack, attacker);
}

void loadWeaponAttack(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker, bool offHand) {
    const Battler& battler = state.at(attacker);
    attack.attackerStat = battler.vigor;
    attack.attackerLevel = attackerLevel(state, attack, attacker);
    if (battler.relicEffects3.has(RelicEffect3::HIT_100_IGNORE_MBLOCK)) {
        attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    }
    if (attack.attack == AttackId::SPECIAL && !battler.status.has(StatusId::IMP)) {
        attack.scratch.nameKind = AttackNameKind::MonsterSpecialAttack;
    }

    const Hand& hand = offHand ? battler.shieldHand : battler.weaponHand;
    attack.offHand = offHand;
    attack.record.power = hand.battlePower;
    attack.twoHanded = hand.weaponFlags.has(WeaponFlags::TWO_HAND);
    attack.rowIgnored = hand.weaponFlags.has(WeaponFlags::BACK_ROW);
    attack.automaticCritical = false;
    attack.record.element = hand.element;
    attack.record.hitRate = hand.hitRate;
    attack.scratch.weaponSpellcast = hand.spellCast;
    attack.effect = SpecialEffect::of(hand.blockEffect.weaponEffect());
    attack.secondary = static_cast<std::uint8_t>(static_cast<std::uint8_t>(hand.item) + 1);

    if (battler.relicEffects4.has(RelicEffect4::FIGHT_TO_X_FIGHT)) {
        attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
        attack.rules.randomTarget = true;
        attack.canBeCritical = false;
        attack.scratch.weaponSpellcast = {};
    }
    if (attack.record.power != 0 && attacker.isMonster() && battler.status.has(StatusId::IMP) &&
        !battler.impCritical) {
        attack.record.power = kImpMonsterPower;
    }
}

BattleCommandId commandForAttack(AttackId attack) {
    for (auto entry = kAiCommandsForAttack.rbegin(); entry != kAiCommandsForAttack.rend(); ++entry) {
        if (attack >= entry->attack) {
            return entry->command;
        }
    }
    return BattleCommandId::MAGIC;
}

void fightAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor) {
    const bool offering = state.at(actor).relicEffects4.has(RelicEffect4::FIGHT_TO_X_FIGHT);
    attack.scratch.attacksLeft = offering ? kOfferingAttacksAfterTheFirst : kFightAttacksAfterTheFirst;
    attack.targets = removeAbsentTargets(state, attack.targets);
    rememberTargets(attack);
    attack.canBeCritical = true;
    attack.scratch.fightCommand = attack.command;
    runAttack(state, hooks, script, attack, actor);
}

}  // namespace ostinato::battle
