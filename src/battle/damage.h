// The damage an attack does before it reaches any one target: its base damage, the relic bonus on
// it, and the damage multiplier.
//
// The attack frame (battle/attack.h) works an attack out in steps, and three of them are here:
//
//     baseDamage(state, attack, attacker);   // attack.damage from the power, the stat and the level
//     // ... the frame halves it for a split and for a back-row attacker ...
//     scaleDamage(attack);                   // attack.damage times the multiplier's steps
//
// magicDamage() is the spell formula on its own, for anything that works a spell out with no battle
// around it.
//
// Every value here is a whole number of HP or MP. The behavior, and the line in the original each
// rule comes from, is specified in docs/contracts/battle_damage.md.
#pragma once

#include <cstdint>

#include "battle/attack.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::battle {

// The most an attack's damage holds. The relic bonus and the multiplier stop here rather than go
// past it.
inline constexpr int kDamageWordMax = 65535;

// A spell's damage from its power, the caster's magic stat (BattleState's magic power, as the attack
// carries it in AttackInFlight::attackerStat) and the caster's level:
//
//     power × 4 + power × magicStat × level / 32
//
// A spell cast at level 0 starts from its power alone instead of four times it. The result is a
// 16-bit value and wraps past 65535: a strong spell cast by a strong caster at a high level does very
// little damage (docs/Bugs.md, "A strong enough spell does almost nothing at level 99").
[[nodiscard]] std::uint16_t magicDamage(std::uint8_t power, std::uint8_t magicStat, std::uint8_t level);

// A physical attack's damage from the attack's power, the attacker's vigor stat
// (AttackInFlight::attackerStat) and level:
//
//     (power + vigor) × level × level / 256
//
// A monster's power counts four times over. A weapon held in both hands (AttackInFlight::twoHanded)
// counts its power seven quarters over, each quarter rounded down as it is added. A party member's
// damage then becomes one and a half times itself plus its power; with an Offering
// (RelicEffect4::FIGHT_TO_X_FIGHT) that is halved, and with RelicEffect4::EQUIP_2_WEAPONS still set
// (a Genji Glove holding one weapon) a quarter of it is taken off.
//
// The first multiplication by the level keeps only 16 bits, so a strong enough blow at a high level
// does less damage than a weaker one (docs/Bugs.md, "A strong enough blow loses most of its damage at
// a high level").
[[nodiscard]] std::uint16_t physicalDamage(const BattleState& state, const AttackInFlight& attack,
                                           BattlerSlot attacker);

// `damage` with the attacker's relic bonus. An attack that restores HP or MP gets none. An attack on
// MP counts as magic whatever it is, and otherwise a physical attack counts as physical:
//
// - physical: RelicEffect1::RAISE_FIGHT_DAMAGE adds a quarter;
// - magic: RelicEffect1::RAISE_MAGIC_DAMAGE (two items that raise magic damage) adds half, and
//   RelicEffect3::RAISE_MAGIC_DAMAGE alone (one of them) adds a quarter.
//
// A quarter or a half is rounded down.
//
// The result stops at kDamageWordMax.
[[nodiscard]] std::uint16_t relicBonus(const BattleState& state, const AttackInFlight& attack,
                                       BattlerSlot attacker, std::uint16_t damage);

// The attack's base damage: magicDamage() or physicalDamage() by the attack's PHYSICAL trait, then
// relicBonus(). Writes it to attack.damage.
//
// `attacker` is whoever the attack is worked out for: the sketcher, when a Sketch makes a monster's
// attack.
void baseDamage(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker);

// `damage` plus half of it for each of `steps`, stopping at kDamageWordMax. Half is rounded down
// once, so 3 damage with one step is 4.
[[nodiscard]] std::uint16_t applyMultiplier(std::uint16_t damage, int steps);

// attack.damage multiplied by attack.multiplier's steps, which are then used up (set to 0).
//
// An attack that ignores defense is not multiplied while attack.ignoreDefenseStopsMultiplier holds,
// and its steps are kept rather than used up.
void scaleDamage(AttackInFlight& attack);

}  // namespace ostinato::battle
