// The damage an attack does: its base damage, the relic bonus on it and the damage multiplier, then
// the damage to each target it hits.
//
// The attack frame (battle/attack.h) works an attack out in steps, and these are four of them:
//
//     baseDamage(state, attack, attacker);   // attack.damage from the power, the stat and the level
//     // ... the frame halves it for a split and for a back-row attacker ...
//     scaleDamage(attack);                   // attack.damage times the multiplier's steps
//     // ... then for each target hit:
//     targetDamage(state, attack, attacker, target, script);   // into attack.scratch.ledger
//
// magicDamage() is the spell formula on its own, for anything that works a spell out with no battle
// around it.
//
// Every value here is a whole number of HP or MP. The behavior, and the line in the original each
// rule comes from, is specified in docs/contracts/battle_damage.md.
#pragma once

#include <cstdint>

#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"

namespace ostinato::battle {

// The most an attack's damage holds. The relic bonus and the multiplier stop here rather than go
// past it.
inline constexpr int kDamageWordMax = 65535;

// The most one battler's entry in the damage ledger holds: what one attack can take from it, or give
// it, at most.
inline constexpr int kDamageCap = 9999;

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

// The damage the attack does to `target`, added to attack.scratch.ledger: taken, or healed when the
// attack restores. Nothing is written for an attack of power 0. In order:
//
// 1. The damage. An attack with AttackFlag2::FRACTIONAL_DAMAGE does power sixteenths of the
//    target's HP (or MP, for AttackFlag1::AFFECT_MP): of its maximum for an Item or an attack with
//    AttackTrait::RESURRECTION_TARGET, else of what it has left after what the ledger already takes
//    from it; at least 1. Any other attack starts from attack.damage and, while
//    attack.scratch.damageModification holds, varies by 224 to 255 two-hundred-fifty-sixths (one
//    draw from state.rng) and then, unless it ignores defense, is cut by the target's defense (magic
//    defense for a spell or an attack on MP; a defense of 255 leaves 1, and while a Golem or
//    Interceptor blocks every target counts as having 192), by Safe against a blow or Shell against a
//    spell, and halved for a Defending or back-row target of a blow or a Morphed target of a spell.
//    A party member's attack on a party member is halved unless it restores. Last, the multiplier's
//    steps added since scaleDamage() are applied and used up.
// 2. Who gains. A petrified target takes 0 whatever the attack. With AttackTrait::INVERT_ON_UNDEAD,
//    an attack that would kill or zombify an undead target does nothing to it at all; a cure hurts
//    an undead target; and a drain on an undead or Zombie target runs backward.
// 3. The elements. An attack whose every element Forcefield has nullified does 0; one the target
//    absorbs heals it instead; one it nullifies does 0, one it halves half, and one it is weak to
//    double, unless the damage is already 32,768 or more.
// 4. The Atma Weapon (WeaponSpecialEffect::ATMA_WEAPON) scales the damage by the wielder's level
//    and how much of its HP it has left, and from 501 damage on lengthens its beam: the attack's
//    animation is rewritten in `script` with the longer beam as its second value.
// 5. A blow that no Golem or Interceptor blocked wakes the target, clears its Muddle (unless the
//    blow itself puts either on) and frees it from Control; from a Zombie attacker it is also
//    poisoned one time in sixteen and blinded one time in sixteen (one draw from state.rng).
// 6. A drain takes no more than the loser has left and, while attack.drainCapped holds, gives no
//    more than the gainer is missing. Unless the drainer is already waiting to answer someone, the
//    target becomes its last attacker, waiting to be answered when the drain was a blow (from a
//    Zombie drainer, only when its draw gave neither Poison nor Blind); and the drainer gains, or
//    loses, the same amount in the ledger.
//
// Each ledger entry adds to what is there and stops at kDamageCap. The damage goes through 16-bit
// cuts the game makes: an Atma Weapon hit strong enough wraps past 65,535 (docs/Bugs.md, "A hard
// enough Atma Weapon hit does almost nothing").
//
// `attacker` is whoever the attack is worked out for: the sketcher, when a Sketch makes a monster's
// attack.
void targetDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                  BattleScriptBuilder& script);

}  // namespace ostinato::battle
