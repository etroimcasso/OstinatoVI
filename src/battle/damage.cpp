#include "battle/damage.h"

#include <algorithm>

#include "ostinato/attack_flags.h"
#include "ostinato/item_effects.h"

namespace ostinato::battle {

namespace {

// A spell cast at level 1 or higher starts from four times its power.
constexpr int kSpellPowerFactor = 4;
// The power × stat × level product is divided by 32.
constexpr int kSpellProductShift = 5;

// A monster's blow starts from four times its power.
constexpr int kMonsterPowerFactor = 4;
// The power, the vigor and the level squared are divided by 256.
constexpr int kBlowProductShift = 8;

// Seven quarters of the power, the way it is built up: half again, halved, then the power added
// once more, each step rounded down.
int twoHandedPower(int power) {
    return (((power >> 1) + power) >> 1) + power;
}

}  // namespace

std::uint16_t magicDamage(std::uint8_t power, std::uint8_t magicStat, std::uint8_t level) {
    const int base = level == 0 ? power : power * kSpellPowerFactor;
    const int product = power * magicStat * level;
    // The sum is a 16-bit value and wraps past 65535 (docs/Bugs.md, "A strong enough spell does
    // almost nothing at level 99").
    return static_cast<std::uint16_t>(base + (product >> kSpellProductShift));
}

std::uint16_t physicalDamage(const BattleState& state, const AttackInFlight& attack,
                             BattlerSlot attacker) {
    const int level = attack.attackerLevel;
    int power = attack.record.power;
    if (attacker.isMonster()) {
        power *= kMonsterPowerFactor;
    }
    if (attack.twoHanded) {
        power = twoHandedPower(power);
    }
    const int strength = power + attack.attackerStat;
    // The first product by the level keeps only 16 bits (docs/Bugs.md, "A strong enough blow loses
    // most of its damage at a high level").
    const int once = static_cast<std::uint16_t>(strength * level);
    int damage = (once * level) >> kBlowProductShift;

    if (attacker.isCharacter()) {
        // A party member's level is at most 99, so the damage so far is at most 65535 × 99 / 256 =
        // 25,343 and this sum stays below 65,536: at most (2 × 255 + 25,343) / 2 + 25,343 = 38,269.
        damage = ((attack.record.power * 2 + damage) >> 1) + damage;
        const RelicEffect4Set relics = state.at(attacker).relicEffects4;
        if (relics.has(RelicEffect4::FIGHT_TO_X_FIGHT)) {
            damage >>= 1;
        }
        if (relics.has(RelicEffect4::EQUIP_2_WEAPONS)) {
            damage -= damage >> 2;
        }
    }
    return static_cast<std::uint16_t>(damage);
}

std::uint16_t relicBonus(const BattleState& state, const AttackInFlight& attack, BattlerSlot attacker,
                         std::uint16_t damage) {
    if (attack.record.flags2.has(AttackFlag2::RESTORE_HP_MP)) {
        return damage;
    }
    const Battler& battler = state.at(attacker);
    const bool physical = !attack.record.flags1.has(AttackFlag1::AFFECT_MP) &&
                          attack.record.traits.has(AttackTrait::PHYSICAL);
    int bonus = 0;
    if (physical) {
        if (battler.relicEffects1.has(RelicEffect1::RAISE_FIGHT_DAMAGE)) {
            bonus = damage >> 2;
        }
    } else if (battler.relicEffects1.has(RelicEffect1::RAISE_MAGIC_DAMAGE)) {
        bonus = damage >> 1;
    } else if (battler.relicEffects3.has(RelicEffect3::RAISE_MAGIC_DAMAGE)) {
        bonus = damage >> 2;
    }
    return static_cast<std::uint16_t>(std::min(damage + bonus, kDamageWordMax));
}

void baseDamage(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    const std::uint16_t base =
        attack.record.traits.has(AttackTrait::PHYSICAL)
            ? physicalDamage(state, attack, attacker)
            : magicDamage(attack.record.power, attack.attackerStat, attack.attackerLevel);
    attack.damage = relicBonus(state, attack, attacker, base);
}

std::uint16_t applyMultiplier(std::uint16_t damage, int steps) {
    const int half = damage >> 1;
    return static_cast<std::uint16_t>(std::min(damage + steps * half, kDamageWordMax));
}

void scaleDamage(AttackInFlight& attack) {
    if (attack.multiplier == 0) {
        return;
    }
    if (attack.ignoreDefenseStopsMultiplier && attack.record.traits.has(AttackTrait::IGNORE_DEFENSE)) {
        return;
    }
    attack.damage = applyMultiplier(static_cast<std::uint16_t>(attack.damage), attack.multiplier);
    attack.multiplier = 0;
}

}  // namespace ostinato::battle
