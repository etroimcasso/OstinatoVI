#include "battle/attacker_effects.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "battle/automatic_actions.h"
#include "battle/battle_script.h"
#include "battle/damage.h"
#include "ostinato/attack_effects.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_flags.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

namespace {

using AttackerHandler = void (*)(BattleState&, AttackInFlight&, BattlerSlot, BattleScriptBuilder&);

// The effects in the table that nothing in the game carries: no attack, weapon, item or command has
// them. They act as the original's table says all the same.
enum class UncarriedAttackerEffect : std::uint8_t {
    MoreMpForCritical = 0x0F,  // the MP critical at 28 MP
    HalveDamage = 0x41,
    QuarterDamage = 0x42,
};

// The effect space runs from 0 (no effect) to the last command effect.
constexpr std::size_t kEffectCount = static_cast<std::size_t>(CommandEffect::AirAnchor) + 1;

// The console's divide: a divisor of 0 gives 65,535.
constexpr int kDivideByZero = 0xFFFF;

int divide(int dividend, int divisor) {
    return divisor == 0 ? kDivideByZero : dividend / divisor;
}

// One of the set bits of `bits`, each equally likely, counting from the lowest. No bits set draws
// nothing and gives 0.
std::uint8_t pickBit(BattleRng& rng, std::uint8_t bits) {
    const int count = std::popcount(bits);
    if (count == 0) {
        return 0;
    }
    int remaining = rng.below(static_cast<std::uint8_t>(count));
    for (unsigned bit = 0; bit < 8; ++bit) {
        const auto mask = static_cast<std::uint8_t>(1u << bit);
        if ((bits & mask) == 0) {
            continue;
        }
        if (remaining-- == 0) {
            return mask;
        }
    }
    return 0;
}

BattleCommandId shownAs(SpecialAnimation animation) {
    return static_cast<BattleCommandId>(animation);
}

void noEffect(BattleState&, AttackInFlight&, BattlerSlot, BattleScriptBuilder&) {}

// No one is hit; the attack goes on with no targets.
void hitsNoOne(AttackInFlight& attack) {
    attack.params.targetsHit = {};
}

// --- the weapons ---------------------------------------------------------------------------------

// A Thiefknife steals instead of striking on half the draws; a Fight is shown as Capture.
void thiefknifeSteals(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    if (state.rng.coin()) {
        return;
    }
    attack.effect = SpecialEffect::of(CommandEffect::Steal);
    if (attack.command == BattleCommandId::FIGHT) {
        attack.command = BattleCommandId::CAPTURE;
    }
}

void atmaWeapon(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    attack.canBeCritical = false;
}

void drainerDrains(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
    attack.record.flags2.set(AttackFlag2::DRAIN);
}

void soulSabreDrainsMp(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder& script) {
    attack.record.flags1.set(AttackFlag1::AFFECT_MP);
    drainerDrains(state, attack, attacker, script);
}

// The MP a critical costs at least; up to seven more are drawn.
constexpr int kMpForCritical = 12;
constexpr int kMoreMpForCritical = 28;
constexpr std::uint8_t kExtraMpForCriticalBits = 0x07;

// An attack that may be a critical hit, with targets, pays its cost in MP for a certain critical
// when the attacker has the MP. Without it, the attack is a critical one time in 32 as before.
void criticalForMp(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, int baseCost) {
    if (!attack.canBeCritical || attack.scratch.targetCount == 0) {
        return;
    }
    const int cost = baseCost + (state.rng.next() & kExtraMpForCriticalBits);
    Battler& battler = state.at(attacker);
    if (battler.mp < cost) {
        return;
    }
    battler.mp = static_cast<std::uint16_t>(battler.mp - cost);
    attack.automaticCritical = true;
}

void mpCritical(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    criticalForMp(state, attack, attacker, kMpForCritical);
}

void moreMpCritical(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    criticalForMp(state, attack, attacker, kMoreMpForCritical);
}

// The dice animation reads its faces from the attack's animation: the third die's face less one in
// the attack (kNoThirdDie for two dice), the second's less one in the high four bits of the second
// value and the first's in the low four.
constexpr std::uint8_t kNoThirdDie = 0x0F;
constexpr int kDieFaces = 6;
// A Fixed Dice — three dice — has a hit rate of at least this.
constexpr std::uint8_t kThreeDiceHitRate = 3;
// The first die is the draw's low four bits, the second the draw's remainder by 96 in sixteens.
constexpr std::uint8_t kFirstDieBits = 0x0F;
constexpr int kSecondDieRange = 96;
constexpr int kSecondDieStep = 16;
constexpr int kFaceShift = 4;

// The Dice's damage: the faces multiplied, by twice the attacker's level and, when all three dice
// show the same face, by that face. Never more than the damage word holds.
void diceRoll(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.scratch.damageModification = false;
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    std::uint8_t thirdShown = kNoThirdDie;
    const std::uint8_t draw = state.rng.next();
    const int first = (draw & kFirstDieBits) % kDieFaces + 1;
    const int second = (draw % kSecondDieRange) / kSecondDieStep + 1;
    attack.secondary = static_cast<std::uint8_t>((second - 1) << kFaceShift | (first - 1));
    int product = first * second;
    if (attack.record.hitRate >= kThreeDiceHitRate) {
        const int third = state.frameCount % kDieFaces + 1;
        thirdShown = static_cast<std::uint8_t>(third - 1);
        product *= third;
    }
    attack.attack = static_cast<AttackId>(thirdShown);
    // All three the same face: the face shown again in both halves of the second value.
    const int bonus = (thirdShown << kFaceShift | thirdShown) == attack.secondary ? thirdShown : 0;
    // Twice the level is taken in a byte: from level 128 on it wraps.
    const int once = product * static_cast<std::uint8_t>(attack.attackerLevel * 2);
    attack.damage = std::min(once * (bonus + 1), kDamageWordMax);
    if (attack.command == BattleCommandId::FIGHT) {
        attack.command = shownAs(SpecialAnimation::DiceRoll);
    }
}

// The Valiant Knife adds what the attacker has lost of its HP to the damage. The damage word wraps
// past 65,535.
void valiantKnife(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    const Battler& battler = state.at(attacker);
    attack.damage = static_cast<std::uint16_t>(attack.damage + (battler.maxHp - battler.hp));
}

// A Tempest casts Wind Slash in place of its blow on half the draws.
constexpr std::uint8_t kTempestCastsBelow = 0x80;

void tempestCastsWindSlash(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    if (state.rng.next() >= kTempestCastsBelow) {
        return;
    }
    attack.record.power = 0;
    attack.scratch.followUpSpell = AttackId::WIND_SLASH;
    ++attack.scratch.attacksLeft;
}

void healRod(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
}

// Ogre Nix breaks one time in (the last digit of the wielder's HP + 1), never during a counterattack
// and never while the wielder's battle menu is open; it costs MP for a critical either way.
constexpr int kOgreNixDigit = 10;

void ogreNixBreaks(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder& script) {
    Battler& battler = state.at(attacker);
    if (!state.counterattackRunning && !battler.inBattleMenu &&
        state.rng.below(static_cast<std::uint8_t>(battler.hp % kOgreNixDigit + 1)) == 0) {
        battler.equipmentChanged = true;
        // The hand swinging: the weapon hand on an odd count of attacks still to come.
        const bool shieldHand = (attack.scratch.attacksLeft & 1) == 0;
        (shieldHand ? battler.shieldHand : battler.weaponHand).battlePower = 0;
        const auto position = static_cast<std::size_t>(attacker.position());
        BattleItem& held = shieldHand ? state.inventory.shieldHands[position] : state.inventory.weaponHands[position];
        if (held.item == ItemId::OGRE_NIX) {
            held.item = ItemId::EMPTY;
            held.usableInBattle = false;
            held.throwable = false;
            held.isTool = false;
            held.isWeapon = false;
            held.isShield = false;
            held.quantity = 0;
            attack.scratch.message = kMessageOgreNixBroken;
        }
    }
    mpCritical(state, attack, attacker, script);
}

// --- the attacks ---------------------------------------------------------------------------------

// Golem takes the caster's HP as its own.
void golemGuards(BattleState& state, AttackInFlight&, BattlerSlot attacker, BattleScriptBuilder&) {
    state.golemHp = state.at(attacker).hp;
}

// Sonic Dive ends the user's turn with this action, and passes over every battler petrified, asleep,
// stopped, frozen or hidden.
const StatusSet kSonicDivePassesOver =
    StatusSet::of(StatusId::PETRIFY, StatusId::SLEEP, StatusId::STOP, StatusId::FROZEN, StatusId::HIDE);

void sonicDive(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    state.endTurnAfterAction = true;
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (state.at(slot).status.hasAny(kSonicDivePassesOver)) {
            attack.params.targets.remove(slot);
            attack.params.targetsHit.remove(slot);
        }
    }
}

// Mantra shares the caster's HP among the others it reaches: the caster's HP over the targets but
// itself, the count less one taken in a byte. A Mantra with no one else to reach does 65,535; one
// aimed at no one divides by 255.
void mantra(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    attack.record.traits.set(AttackTrait::NO_DAMAGE_SPLIT);
    attack.scratch.damageModification = false;
    attack.params.targetsHit.remove(attacker);
    const auto others = static_cast<std::uint8_t>(attack.scratch.targetCount - 1);
    attack.damage = divide(state.at(attacker).hp, others);
}

// Whether the frame leaves a battler's mark to answer itself ready when an attacker effect sets it:
// after a blow by a party member in a side attack, or by a monster in a pincer or a side attack.
// LEGACY VERBATIM (docs/Bugs.md): the waiting comes from the processor's carry as the frame's facing
// step (UpdateFacingDirection) leaves it, not from anything about the battler.
bool facingStepLeavesTheMarkReady(const BattleState& state, const AttackInFlight& attack, BattlerSlot attacker) {
    if (!attack.record.traits.has(AttackTrait::PHYSICAL)) {
        return false;
    }
    if (attacker.isCharacter()) {
        return state.battleType == BattleType::Side;
    }
    return state.battleType == BattleType::Pincer || state.battleType == BattleType::Side;
}

// Spiraler: the caster gives up its HP and MP, hides and dies, and is gone once the action is over.
// A party member also stops being one of its own targets; a monster's own bit is not in the byte
// this reads, so a monster stays among them.
void spiraler(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    if (attacker.isCharacter()) {
        attack.params.targets.remove(attacker);
        attack.params.targetsHit.remove(attacker);
        state.leavingAfterAction.add(attacker);
    }
    // Spiraler is a spell, so its own record leaves the mark unready.
    hideAndDie(state, attacker, facingStepLeavesTheMarkReady(state, attack, attacker));
    Battler& battler = state.at(attacker);
    battler.hp = 0;
    battler.mp = 0;
}

// Warp, and a Warp Stone, end the battle as running out of time does — unless the party cannot run.
void warp(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    if (state.cannotRun) {
        attack.scratch.message = kMessageCantEscape;
        hitsNoOne(attack);
        return;
    }
    state.endOfBattleEvent = EndOfBattleEvent::RanOutOfTime;
}

// Exploder: the user's own HP as the damage, at no multiplier, to every target it hit and to itself.
// It is shown first on the user alone, then on its targets.
void exploder(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder& script) {
    attack.multiplier = 0;
    attack.castingSwirlSuppressed = true;
    attack.scratch.damageModification = false;
    const BattlerSet hit = attack.params.targetsHit;
    attack.targets = BattlerSet::of(attacker);
    initParams(attack, attacker);
    script.addParams(attack.params);
    attack.targets = hit;
    initParams(attack, attacker);
    attack.params.targetsHit = hit | BattlerSet::of(attacker);
    attack.damage = state.at(attacker).hp;
    script.addAnimation(animationOf(attack));
}

void blowFish(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    constexpr int kBlowFishDamage = 1000;
    attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    attack.record.traits.set(AttackTrait::NO_DAMAGE_SPLIT);
    attack.scratch.damageModification = false;
    attack.damage = kBlowFishDamage;
}

void pearlWind(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    attack.record.traits.set(AttackTrait::NO_DAMAGE_SPLIT);
    attack.scratch.damageModification = false;
    attack.damage = state.at(attacker).hp;
}

// Reflect??? hits only the battlers with Reflect.
void reflectLore(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (!state.at(slot).status.has(StatusId::REFLECT)) {
            attack.params.targetsHit.remove(slot);
        }
    }
}

// L? Pearl's hit rate is the last digit of the party's gil: it hits the battlers whose level that
// divides.
constexpr std::uint32_t kGilDigit = 10;

void pearlLore(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.record.hitRate = static_cast<std::uint8_t>(state.party.gil % kGilDigit);
}

// Step Mine: the party's steps over the attack's power, or 65,535 when that is more than the damage
// word holds.
void stepMine(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.scratch.damageModification = false;
    const std::uint32_t power = attack.record.power;
    const std::uint32_t steps = state.party.steps;
    if (power == 0 || steps / power > static_cast<std::uint32_t>(kDamageWordMax)) {
        attack.damage = kDamageWordMax;
        return;
    }
    attack.damage = static_cast<int>(steps / power);
}

// Escape by a party member is shown as the party running away.
void escape(BattleState&, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    if (attacker.isMonster()) {
        return;
    }
    attack.command = shownAs(SpecialAnimation::PartyRunsAway);
    attack.params.castingSwirl = true;
}

// Mind Blast picks one of the battlers it hits for each party position, the last position first;
// each target gets one status from the attack for every pick that named it.
void mindBlast(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    for (int position = kPartySlots - 1; position >= 0; --position) {
        attack.scratch.mindBlastPicks[static_cast<std::size_t>(position)] = state.rng.pickFrom(attack.params.targetsHit);
    }
}

// N. Cross misses a random handful of party members and another of monsters.
void nCross(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    BattlerSet& hit = attack.params.targetsHit;
    hit.characters.bits = static_cast<std::uint8_t>(hit.characters.bits & ~state.rng.next());
    hit.monsters.bits = static_cast<std::uint8_t>(hit.monsters.bits & ~state.rng.next());
}

// Flare Star: the power times the level of one target drawn at random, over the number of targets.
void flareStar(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.scratch.damageModification = false;
    const BattlerSet targets = attack.params.targets;
    const std::optional<BattlerSlot> drawn = state.rng.pickFrom(targets);
    const int level = drawn ? state.at(*drawn).level : 0;
    attack.damage = divide(attack.record.power * level, targets.count());
}

// A Launcher's eight hits, or a Super Ball's one to four, are made from the attacker after the hit
// check is passed over (resolveReflectedAttack()).
constexpr int kLauncherHits = 8;
constexpr std::uint8_t kSuperBallExtraHitBits = 0x03;
// Super Ball's hits are drawn with the attack animation at Bio Blast's index.
constexpr AttackId kSuperBallAnimation = AttackId::BIO_BLAST_TOOL;

void launcher(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    state.extraHits = kLauncherHits;
    attack.params.reflectedOff = BattlerSet::of(attacker);
}

void superBall(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    attack.attack = kSuperBallAnimation;
    state.extraHits = (state.rng.next() & kSuperBallExtraHitBits) + 1;
    attack.params.reflectedOff = BattlerSet::of(attacker);
}

// Suplex picks one target it can throw; with none of them throwable it picks among them all.
void suplex(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    attack.castingSwirlSuppressed = true;
    BattlerSet throwable = attack.params.targets;
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::monster(position);
        if (state.at(slot).monsterFlags.has(MonsterBattleFlag::CANT_SUPLEX)) {
            throwable.remove(slot);
        }
    }
    if (throwable.empty()) {
        throwable = attack.params.targets;
    }
    const std::optional<BattlerSlot> drawn = state.rng.pickFrom(throwable);
    attack.targets = drawn ? BattlerSet::of(*drawn) : BattlerSet{};
    initParams(attack, attacker);
}

// The first battle message of the eight "…-element ineffectual" ones, Fire's.
constexpr std::uint8_t kEveryElement = 0xFF;

// Forcefield nullifies one element it has not nullified yet, drawn at random, for the rest of the
// battle. With every element nullified it hits no one.
void forcefield(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    const auto open = static_cast<std::uint8_t>(kEveryElement & ~state.elementsNullifiedByForcefield.bits);
    if (open == 0) {
        hitsNoOne(attack);
        return;
    }
    const std::uint8_t element = pickBit(state.rng, open);
    state.elementsNullifiedByForcefield.bits = static_cast<std::uint8_t>(state.elementsNullifiedByForcefield.bits | element);
    attack.scratch.message = static_cast<std::uint8_t>(kMessageElementIneffectual + std::countr_zero(element));
}

// Quadra Slam and Quadra Slice make four attacks at random targets.
constexpr int kQuadraMoreAttacks = 3;

void quadra(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.scratch.attacksLeft = kQuadraMoreAttacks;
    attack.rules.randomTarget = true;
    attack.effect = {};
}

// Bababreath blows away a fallen party member: the lowest one dead, petrified or a zombie, still on
// the field and not away from it. With none, it goes on as aimed while two or more party members
// fight, and hits no one otherwise.
const StatusSet kBlownAway = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE);
constexpr int kBababreathAllies = 2;

void bababreath(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    std::optional<BattlerSlot> fallen;
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        const Battler& battler = state.at(slot);
        if (battler.present && battler.status.hasAny(kBlownAway) && !state.awayFromTheField.has(slot)) {
            fallen = slot;
        }
    }
    if (fallen) {
        attack.targets = BattlerSet::of(*fallen);
        initParams(attack, attacker);
        return;
    }
    if (state.allyCount < kBababreathAllies) {
        hitsNoOne(attack);
    }
}

// Empowerer turns on and off whether the attack works on MP. On, it is not shown; off, it is made
// once more at a quarter of the power.
constexpr int kEmpowererPowerShift = 2;

void empowerer(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    AttackFlags1& flags = attack.record.flags1;
    if (flags.has(AttackFlag1::AFFECT_MP)) {
        flags.clear(AttackFlag1::AFFECT_MP);
        ++attack.scratch.attacksLeft;
        attack.record.power = static_cast<std::uint8_t>(attack.record.power >> kEmpowererPowerShift);
        return;
    }
    flags.set(AttackFlag1::AFFECT_MP);
    attack.command = kNothingShown;
}

void revenge(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    attack.scratch.damageModification = false;
    const Battler& battler = state.at(attacker);
    attack.damage = battler.maxHp - battler.hp;
}

void halveDamage(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.damage >>= 1;
}

void quarterDamage(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.damage >>= 2;
}

// Quick gives the caster two turns in a row — unless someone is already taking Quick's turns, when
// the caster is shown "Miss".
constexpr std::uint8_t kQuickTurns = 2;

void quick(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    if (state.quickTurnsLeft) {
        attack.scratch.missed.add(attacker);
        return;
    }
    state.quickTarget = attacker;
    state.quickTurnsLeft = kQuickTurns;
}

// Discard hits no one but the battler the attacker holds in Seize, and that one cannot dodge it.
void discard(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    hitsNoOne(attack);
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    const std::optional<BattlerSlot> held = state.at(attacker).seizing;
    if (!held) {
        return;
    }
    attack.targets = BattlerSet::of(*held);
    initParams(attack, attacker);
}

// --- the items -----------------------------------------------------------------------------------

// Magicite summons an esper drawn at random, Odin and Raiden aside.
void magicite(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.scratch.followUpSpell = chooseRandomEsper(state);
    ++attack.scratch.attacksLeft;
}

// A Smoke Bomb does nothing where the party cannot run.
void smokeBomb(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    if (!state.cannotRun) {
        return;
    }
    hitsNoOne(attack);
    attack.effect = {};
    attack.scratch.message = kMessageCantRunAway;
}

// An Elixir restores HP first; its effect on each target restores the MP.
void elixir(BattleState&, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    attack.record.flags1.clear(AttackFlag1::AFFECT_MP);
}

// --- the commands --------------------------------------------------------------------------------

// Possess misses on two draws in five.
constexpr std::uint8_t kPossessTakesBelow = 150;

void possess(BattleState& state, AttackInFlight& attack, BattlerSlot, BattleScriptBuilder&) {
    if (state.rng.next() >= kPossessTakesBelow) {
        hitsNoOne(attack);
    }
}

// GP Rain throws 30 gil per level of the thrower: a party member's from the party's gil, a monster's
// from its own gold. Twice what was thrown is the damage, shared between the targets.
constexpr std::uint32_t kGpRainGilPerLevel = 30;
constexpr int kGpRainDamagePerGil = 2;

void gpRain(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder&) {
    Battler& battler = state.at(attacker);
    const std::uint32_t amount = battler.level * kGpRainGilPerLevel;
    std::uint32_t thrown = 0;
    if (attacker.isCharacter()) {
        thrown = takeGil(state.party, amount);
    } else if (battler.gold != 0) {
        thrown = std::min<std::uint32_t>(amount, battler.gold);
        battler.gold = static_cast<std::uint16_t>(battler.gold - thrown);
    }
    if (thrown == 0) {
        hitsNoOne(attack);
        attack.scratch.message = kMessageNoMoney;
        return;
    }
    attack.damage = divide(static_cast<int>(thrown) * kGpRainDamagePerGil, attack.scratch.targetCount);
}

// --- the table -----------------------------------------------------------------------------------

struct AttackerEffectRow {
    std::uint8_t effect;
    AttackerHandler handler;
};

constexpr std::uint8_t indexOf(WeaponSpecialEffect effect) {
    return SpecialEffect::of(effect).index();
}
constexpr std::uint8_t indexOf(AttackSpecialEffect effect) {
    return SpecialEffect::of(effect).index();
}
constexpr std::uint8_t indexOf(ItemUseEffect effect) {
    return SpecialEffect::of(effect).index();
}
constexpr std::uint8_t indexOf(CommandEffect effect) {
    return SpecialEffect::of(effect).index();
}
constexpr std::uint8_t indexOf(UncarriedAttackerEffect effect) {
    return static_cast<std::uint8_t>(effect);
}

// Every effect that acts on the attacker, by its index in the effect space. Sonic Dive's effect
// shares its index with Palidor's, which acts on the target.
constexpr std::array kAttackerEffectRows{
    AttackerEffectRow{indexOf(WeaponSpecialEffect::THIEFKNIFE), thiefknifeSteals},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::ATMA_WEAPON), atmaWeapon},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::DRAINER), drainerDrains},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::SOUL_SABRE), soulSabreDrainsMp},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::MP_CRITICAL), mpCritical},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::DICE), diceRoll},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::VALIANTKNIFE), valiantKnife},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::TEMPEST), tempestCastsWindSlash},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::HEAL_ROD), healRod},
    AttackerEffectRow{indexOf(WeaponSpecialEffect::OGRE_NIX), ogreNixBreaks},
    AttackerEffectRow{indexOf(UncarriedAttackerEffect::MoreMpForCritical), moreMpCritical},
    AttackerEffectRow{indexOf(AttackSpecialEffect::GOLEM), golemGuards},
    AttackerEffectRow{indexOf(AttackSpecialEffect::PALIDOR), sonicDive},
    AttackerEffectRow{indexOf(AttackSpecialEffect::MANTRA), mantra},
    AttackerEffectRow{indexOf(AttackSpecialEffect::SPIRALER), spiraler},
    AttackerEffectRow{indexOf(AttackSpecialEffect::WARP), warp},
    AttackerEffectRow{indexOf(AttackSpecialEffect::EXPLODER), exploder},
    AttackerEffectRow{indexOf(AttackSpecialEffect::BLOW_FISH), blowFish},
    AttackerEffectRow{indexOf(AttackSpecialEffect::PEARL_WIND), pearlWind},
    AttackerEffectRow{indexOf(AttackSpecialEffect::REFLECT_LORE), reflectLore},
    AttackerEffectRow{indexOf(AttackSpecialEffect::PEARL_LORE), pearlLore},
    AttackerEffectRow{indexOf(AttackSpecialEffect::STEP_MINE), stepMine},
    AttackerEffectRow{indexOf(AttackSpecialEffect::ESCAPE), escape},
    AttackerEffectRow{indexOf(AttackSpecialEffect::MIND_BLAST), mindBlast},
    AttackerEffectRow{indexOf(AttackSpecialEffect::N_CROSS), nCross},
    AttackerEffectRow{indexOf(AttackSpecialEffect::FLARE_STAR), flareStar},
    AttackerEffectRow{indexOf(AttackSpecialEffect::LAUNCHER), launcher},
    AttackerEffectRow{indexOf(AttackSpecialEffect::SUPLEX), suplex},
    AttackerEffectRow{indexOf(AttackSpecialEffect::FORCEFIELD), forcefield},
    AttackerEffectRow{indexOf(AttackSpecialEffect::QUADRA_SLAM_SLICE), quadra},
    AttackerEffectRow{indexOf(AttackSpecialEffect::BABABREATH), bababreath},
    AttackerEffectRow{indexOf(AttackSpecialEffect::EMPOWERER), empowerer},
    AttackerEffectRow{indexOf(AttackSpecialEffect::REVENGE), revenge},
    AttackerEffectRow{indexOf(UncarriedAttackerEffect::HalveDamage), halveDamage},
    AttackerEffectRow{indexOf(UncarriedAttackerEffect::QuarterDamage), quarterDamage},
    AttackerEffectRow{indexOf(AttackSpecialEffect::QUICK), quick},
    AttackerEffectRow{indexOf(AttackSpecialEffect::DISCARD), discard},
    AttackerEffectRow{indexOf(ItemUseEffect::MAGICITE), magicite},
    AttackerEffectRow{indexOf(ItemUseEffect::SUPER_BALL), superBall},
    AttackerEffectRow{indexOf(ItemUseEffect::SMOKE_BOMB), smokeBomb},
    AttackerEffectRow{indexOf(ItemUseEffect::ELIXIR), elixir},
    AttackerEffectRow{indexOf(ItemUseEffect::WARP_STONE), warp},
    AttackerEffectRow{indexOf(CommandEffect::Possess), possess},
    AttackerEffectRow{indexOf(CommandEffect::GpRain), gpRain},
};

constexpr std::array<AttackerHandler, kEffectCount> attackerEffectTable() {
    std::array<AttackerHandler, kEffectCount> table{};
    table.fill(noEffect);
    for (const AttackerEffectRow& row : kAttackerEffectRows) {
        table[row.effect] = row.handler;
    }
    return table;
}

constexpr std::array<AttackerHandler, kEffectCount> kAttackerEffects = attackerEffectTable();

static_assert(kAttackerEffectRows.size() == 44, "the attacker effect table names 44 effects");

}  // namespace

void attackerEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattleScriptBuilder& script) {
    kAttackerEffects[attack.effect.index()](state, attack, attacker, script);
}

void hideAndDie(BattleState& state, BattlerSlot slot, bool waitsToAnswer) {
    Battler& battler = state.at(slot);
    battler.statusesToSet.set(StatusId::HIDE);
    if (!battler.retaliationPending) {
        battler.lastAttacker = slot;
        battler.retaliationPending = waitsToAnswer;
    }
    battler.statusesToSet.set(StatusId::DEAD);
}

std::uint32_t takeGil(game::Party& party, std::uint32_t amount) {
    if (party.gil >= amount) {
        party.gil -= amount;
        return amount;
    }
    const std::uint32_t taken = party.gil;
    party.gil = 0;
    return taken;
}

}  // namespace ostinato::battle
