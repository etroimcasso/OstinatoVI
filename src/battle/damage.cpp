#include "battle/damage.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "ostinato/attack_flags.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"

namespace ostinato::battle {

static_assert(kDamageCap == kLargestDamageNumber, "a ledger entry is never more than a numeral shows");

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

// The multiplier is left alone for an attack that ignores defense, unless a Throw turned that off.
bool multiplierIgnored(const AttackInFlight& attack) {
    return attack.ignoreDefenseStopsMultiplier && attack.record.traits.has(AttackTrait::IGNORE_DEFENSE);
}

// `damage` multiplied by the attack's steps, which are then used up. No steps, or an ignored
// multiplier, leaves both as they are.
int multiplied(AttackInFlight& attack, int damage) {
    if (attack.multiplier == 0 || multiplierIgnored(attack)) {
        return damage;
    }
    const std::uint16_t result = applyMultiplier(static_cast<std::uint16_t>(damage), attack.multiplier);
    attack.multiplier = 0;
    return result;
}

// --- the damage to one target ------------------------------------------------------------------

// A factor of n takes the damage to n two-hundred-fifty-sixths of itself, rounded down, plus 1.
constexpr int kFactorShift = 8;

int scaledBy(int damage, int factor) {
    return ((damage * factor) >> kFactorShift) + 1;
}

// The variance draw keeps its top three bits: a factor of 224 to 255.
constexpr std::uint8_t kVarianceBits = 0xE0;

// A defense of 255 leaves a factor of 0; any other leaves 255 less the defense.
constexpr int kStrongestDefense = 255;
// While a Golem or Interceptor blocks, every target's defense counts as this.
constexpr int kBlockedDefense = 192;
// Safe against a blow, Shell against a spell.
constexpr int kSafeShellFactor = 170;

// A fractional attack does power sixteenths of the pool.
constexpr int kFractionShift = 4;

// A weakness doubles the damage only below this.
constexpr int kWeaknessDoublesBelow = 32768;

// The Atma Weapon: the damage times its strength, divided by 64, plus 1; from 501 the beam is
// longer and from 1001 longer again. The beams are the second values of the Fight animation.
constexpr int kAtmaShift = 6;
constexpr int kAtmaMediumBeamFrom = 501;
constexpr int kAtmaLongBeamFrom = 1001;
constexpr std::uint8_t kAtmaMediumBeam = 91;
constexpr std::uint8_t kAtmaLongBeam = 92;
// The wielder's HP is counted in 256s, from 1.
constexpr int kHpCountShift = 8;

// A Zombie's blow poisons on a draw below the first and blinds on one below the second.
constexpr std::uint8_t kZombiePoisonBelow = 16;
constexpr std::uint8_t kZombieBlindBelow = 32;

std::size_t at(BattlerSlot slot) {
    return static_cast<std::size_t>(slot.index());
}

bool shares(ElementSet a, ElementSet b) {
    return (a.bits & b.bits) != 0;
}

// A battler's HP, or its MP for an attack on MP.
int current(const Battler& battler, bool mp) {
    return mp ? battler.mp : battler.hp;
}

int maximum(const Battler& battler, bool mp) {
    return mp ? battler.maxMp : battler.maxHp;
}

// Power sixteenths of the target's pool: its maximum for an Item or a revive-type attack, else what
// it has left after what this attack already takes from it. At least 1.
int fractionalDamage(const BattleState& state, const AttackInFlight& attack, BattlerSlot target) {
    const Battler& struck = state.at(target);
    const bool mp = attack.record.flags1.has(AttackFlag1::AFFECT_MP);
    int pool = 0;
    if (attack.command == BattleCommandId::ITEM || attack.record.traits.has(AttackTrait::RESURRECTION_TARGET)) {
        pool = maximum(struck, mp);
    } else {
        // What the ledger takes is taken from the HP side, also for an attack on MP.
        pool = std::max(0, current(struck, mp) - attack.scratch.ledger.taken[at(target)].value_or(0));
    }
    // The original keeps 16 bits of the result, which only a pool times a power of 1,048,576 or more
    // could pass. Every fractional attack and item has a power of 16 or less, so a pool of at most
    // 65,535 never reaches it (docs/contracts/battle_damage.md, "The fraction").
    const int damage = (pool * attack.record.power) >> kFractionShift;
    return damage == 0 ? 1 : damage;
}

// The damage before the elements: attack.damage varied and cut by the target's defenses, with the
// multiplier's remaining steps applied.
int modifiedDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target) {
    int damage = attack.damage;
    if (!attack.scratch.damageModification) {
        return damage;
    }
    damage = scaledBy(damage, state.rng.next() | kVarianceBits);

    const AttackProperties& record = attack.record;
    const Battler& struck = state.at(target);
    const bool blow = !record.flags1.has(AttackFlag1::AFFECT_MP) && record.traits.has(AttackTrait::PHYSICAL);
    if (!record.traits.has(AttackTrait::IGNORE_DEFENSE)) {
        const int defense = blow ? struck.defense : struck.magicDefense;
        int factor = 0;
        if (defense != kStrongestDefense) {
            const bool blocked = attack.scratch.golemBlocked || attack.scratch.interceptorBlocked;
            factor = kStrongestDefense - (blocked ? kBlockedDefense : defense);
        }
        damage = scaledBy(damage, factor);
        if (struck.status.has(blow ? StatusId::PROTECT : StatusId::SHELL)) {
            damage = scaledBy(damage, kSafeShellFactor);
        }
        if (blow) {
            if (struck.defending) {
                damage >>= 1;
            }
            if (struck.backRow) {
                damage >>= 1;
            }
        } else if (struck.status.has(StatusId::MORPH)) {
            damage >>= 1;
        }
    }
    if (!record.flags2.has(AttackFlag2::RESTORE_HP_MP) && attacker.isCharacter() && target.isCharacter()) {
        damage >>= 1;
    }
    return multiplied(attack, damage);
}

// The Atma Weapon's damage, by the wielder's level and how much of its HP it has left, and the
// beam it shows.
int atmaWeapon(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker, int damage,
               BattleScriptBuilder& script) {
    const Battler& wielder = state.at(attacker);
    // The wielder is a party member, whose HP is at most its maximum of at most 9,999, so the
    // strength is at most its level.
    const int strength =
        ((wielder.hp >> kHpCountShift) + 1) * wielder.level / ((wielder.maxHp >> kHpCountShift) + 1);
    // The result keeps 16 bits (docs/Bugs.md, "A hard enough Atma Weapon hit does almost nothing").
    const auto result = static_cast<std::uint16_t>(((damage * strength) >> kAtmaShift) + 1);
    if (result >= kAtmaMediumBeamFrom) {
        attack.secondary = result >= kAtmaLongBeamFrom ? kAtmaLongBeam : kAtmaMediumBeam;
        script.replaceAnimation(animationOf(attack));
    }
    return result;
}

// Mark `battler` as attacked by `by`, waiting to answer it when `waits`, unless it is already
// waiting to answer someone.
void markAttackedBy(Battler& battler, BattlerSlot by, bool waits) {
    if (!battler.retaliationPending) {
        battler.lastAttacker = by;
        battler.retaliationPending = waits;
    }
}

// A drain takes no more than the loser has, and gives no more than the gainer is missing.
int drained(const BattleState& state, const AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
            bool heals, int damage) {
    if (!attack.scratch.damageModification) {
        return damage;
    }
    const bool mp = attack.record.flags1.has(AttackFlag1::AFFECT_MP);
    const Battler& loser = state.at(heals ? attacker : target);
    const Battler& gainer = state.at(heals ? target : attacker);
    damage = std::min(damage, current(loser, mp));
    if (attack.drainCapped) {
        // HP and MP never exceed their maximum, so what the gainer is missing is never negative.
        damage = std::min(damage, maximum(gainer, mp) - current(gainer, mp));
    }
    return damage;
}

void addToLedger(DamageLedger& ledger, BattlerSlot slot, int amount, bool heals) {
    std::optional<std::uint16_t>& entry = (heals ? ledger.healed : ledger.taken)[at(slot)];
    entry = static_cast<std::uint16_t>(std::min(entry.value_or(0) + amount, kDamageCap));
}

// --- taking the damage -------------------------------------------------------------------------

// A battler brought to 0 HP, or to 0 MP when it dies of that: its HP is 0, the weapon's spell is
// not cast, and unless it is a Zombie it is set to die.
void broughtDown(Battler& battler, AttackInFlight& attack) {
    battler.hp = 0;
    attack.scratch.weaponSpellcast = {};
    if (!battler.status.has(StatusId::ZOMBIE)) {
        battler.statusesToSet.set(StatusId::DEAD);
    }
}

// The battler's ledger entries taken from its HP or MP. True when it lost anything.
bool takeEntries(BattleState& state, AttackInFlight& attack, BattlerSlot slot) {
    std::optional<std::uint16_t>& taken = attack.scratch.ledger.taken[at(slot)];
    if (taken && state.invincible.has(slot)) {
        taken = 0;
    }
    int loss = taken.value_or(0);
    if (attack.scratch.golemBlocked || attack.scratch.interceptorBlocked) {
        loss = 0;
    }
    const int net = attack.scratch.ledger.healed[at(slot)].value_or(0) - loss;
    if (net == 0) {
        return false;
    }

    Battler& battler = state.at(slot);
    const bool mp = attack.record.flags1.has(AttackFlag1::AFFECT_MP);
    std::uint16_t& pool = mp ? battler.mp : battler.hp;
    const int most = mp ? battler.maxMp : battler.maxHp;
    if (mp) {
        battler.pendingUpdates.enabledMagic = true;
    }
    if (net > 0) {
        pool = static_cast<std::uint16_t>(std::min(pool + net, most));
        return false;
    }
    if (pool > -net) {
        pool = static_cast<std::uint16_t>(pool + net);
        return true;
    }
    pool = 0;
    if (!mp || battler.diesAtZeroMp) {
        broughtDown(battler, attack);
    }
    return true;
}

// One battler's numeral as the script shows it.
DamageNumber numeral(BattlerSlot slot, std::uint16_t amount, bool heals) {
    return DamageNumber{.target = slot, .amount = amount, .heals = heals};
}

void show(BattleScriptBuilder& script, std::vector<DamageNumber> numbers) {
    if (numbers.empty()) {
        return;
    }
    const DamageNumberWait wait =
        numbers.size() >= kNumeralsShownTogether ? DamageNumberWait::UntilGone : DamageNumberWait::CarryOn;
    script.add(DamageNumbersShown{.numbers = std::move(numbers), .wait = wait});
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
    attack.damage = multiplied(attack, attack.damage);
}

void targetDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                  BattleScriptBuilder& script) {
    const AttackProperties& record = attack.record;
    if (record.power == 0) {
        return;
    }
    int damage = record.flags2.has(AttackFlag2::FRACTIONAL_DAMAGE) ? fractionalDamage(state, attack, target)
                                                                   : modifiedDamage(state, attack, attacker, target);

    Battler& struck = state.at(target);
    bool heals = false;
    if (struck.status.has(StatusId::PETRIFY)) {
        damage = 0;
    } else {
        heals = record.flags2.has(AttackFlag2::RESTORE_HP_MP);
        if (record.traits.has(AttackTrait::INVERT_ON_UNDEAD)) {
            bool drainRunsBackward = struck.status.has(StatusId::ZOMBIE);
            if (struck.undead) {
                if (record.statuses.has(StatusId::DEAD) || record.statuses.has(StatusId::ZOMBIE)) {
                    return;
                }
                heals = false;
                drainRunsBackward = true;
            }
            if (drainRunsBackward && record.flags2.has(AttackFlag2::DRAIN)) {
                heals = !heals;
            }
        }
        const ElementSet elements = record.element;
        if (elements.bits != 0) {
            const bool allNullifiedByForcefield =
                (elements.bits & static_cast<std::uint8_t>(~state.elementsNullifiedByForcefield.bits)) == 0;
            if (allNullifiedByForcefield) {
                damage = 0;
            } else if (shares(elements, struck.elementsAbsorbed)) {
                heals = !heals;
            } else if (shares(elements, struck.elementsNullified)) {
                damage = 0;
            } else if (shares(elements, struck.elementsHalved)) {
                damage >>= 1;
            } else if (shares(elements, struck.elementsWeak) && damage < kWeaknessDoublesBelow) {
                damage <<= 1;
            }
        }
    }
    if (attack.effect == SpecialEffect::of(WeaponSpecialEffect::ATMA_WEAPON)) {
        damage = atmaWeapon(state, attack, attacker, damage, script);
    }

    const bool blow = record.traits.has(AttackTrait::PHYSICAL);
    // A drainer waits to answer its target after a blow, not after a spell.
    bool drainerWaits = blow;
    if (blow && !attack.scratch.golemBlocked && !attack.scratch.interceptorBlocked) {
        if (state.at(attacker).status.has(StatusId::ZOMBIE)) {
            const std::uint8_t draw = state.rng.next();
            if (draw < kZombiePoisonBelow) {
                struck.statusesToSet.set(StatusId::POISON);
            } else if (draw < kZombieBlindBelow) {
                struck.statusesToSet.set(StatusId::BLIND);
            }
            drainerWaits = draw >= kZombieBlindBelow;
        }
        for (const StatusId woken : {StatusId::SLEEP, StatusId::CONFUSE}) {
            if (struck.status.has(woken) && !record.statuses.has(woken)) {
                struck.statusesToClear.set(woken);
            }
        }
        struck.controlledBy.reset();
    }
    if (record.flags2.has(AttackFlag2::DRAIN)) {
        damage = drained(state, attack, attacker, target, heals, damage);
        markAttackedBy(state.at(attacker), target, drainerWaits);
        addToLedger(attack.scratch.ledger, attacker, damage, !heals);
    }
    addToLedger(attack.scratch.ledger, target, damage, heals);
}

void applyDamage(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder& script) {
    DamageLedger& ledger = attack.scratch.ledger;
    std::vector<DamageNumber> shown;
    std::vector<DamageNumber> gainsShownAfter;
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        std::optional<std::uint16_t>& taken = ledger.taken[at(slot)];
        const std::optional<std::uint16_t>& healed = ledger.healed[at(slot)];
        if (!taken && !healed) {
            if (attack.scratch.missed.has(slot)) {
                shown.push_back(DamageNumber{.target = slot, .miss = true});
            }
            continue;
        }
        attack.scratch.missed.remove(slot);
        if (takeEntries(state, attack, slot)) {
            Battler& battler = state.at(slot);
            markAttackedBy(battler, attacker, true);
            if (attacker != slot) {
                battler.lastDamagedBy = attacker;
                if (slot.isCharacter()) {
                    state.scratch.blackBeltEligible.add(slot);
                }
            }
        }
        if (attack.scratch.golemBlocked == slot && taken) {
            state.golemHp = static_cast<std::uint16_t>(std::max(0, state.golemHp - *taken));
        }
        if (taken) {
            shown.push_back(numeral(slot, *taken, false));
            if (healed) {
                gainsShownAfter.push_back(numeral(slot, *healed, true));
            }
        } else {
            shown.push_back(numeral(slot, *healed, true));
        }
    }
    show(script, std::move(shown));
    show(script, std::move(gainsShownAfter));
    ledger = {};
}

}  // namespace ostinato::battle
