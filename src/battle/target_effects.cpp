#include "battle/target_effects.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "battle/actions.h"
#include "battle/attacker_effects.h"
#include "battle/links.h"
#include "data/formations.h"
#include "data/metamorph.h"
#include "data/monster_attacks.h"
#include "ostinato/attack_effects.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/game_version.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_flags.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

namespace {

using TargetHandler = bool (*)(BattleState&, AttackInFlight&, BattlerSlot attacker, BattlerSlot target,
                               BattleScriptBuilder&);

// The effect space runs from 0 (no effect) to the last command effect.
constexpr std::size_t kEffectCount = static_cast<std::size_t>(CommandEffect::AirAnchor) + 1;

// The console's divide: a divisor of 0 gives 65,535.
constexpr int kDivideByZero = 0xFFFF;

int divide(int dividend, int divisor) {
    return divisor == 0 ? kDivideByZero : dividend / divisor;
}

// One of the set bits of `bits`, each equally likely, counting from the lowest. No bits set draws
// nothing and gives 0.
std::uint16_t pickBit(BattleRng& rng, std::uint16_t bits) {
    const int count = std::popcount(bits);
    if (count == 0) {
        return 0;
    }
    int remaining = rng.below(static_cast<std::uint8_t>(count));
    for (unsigned bit = 0; bit < 16; ++bit) {
        const auto mask = static_cast<std::uint16_t>(1u << bit);
        if ((bits & mask) == 0) {
            continue;
        }
        if (remaining-- == 0) {
            return mask;
        }
    }
    return 0;
}

// The two words of a status set as the original holds them: statuses 1-2 and statuses 3-4.
std::uint16_t firstWord(const StatusSet& set) {
    return static_cast<std::uint16_t>(set.bytes[0] | set.bytes[1] << 8);
}
std::uint16_t secondWord(const StatusSet& set) {
    return static_cast<std::uint16_t>(set.bytes[2] | set.bytes[3] << 8);
}

void addToFirstWord(StatusSet& set, std::uint16_t bits) {
    set.bytes[0] = static_cast<std::uint8_t>(set.bytes[0] | (bits & 0xFF));
    set.bytes[1] = static_cast<std::uint8_t>(set.bytes[1] | bits >> 8);
}
void addToSecondWord(StatusSet& set, std::uint16_t bits) {
    set.bytes[2] = static_cast<std::uint8_t>(set.bytes[2] | (bits & 0xFF));
    set.bytes[3] = static_cast<std::uint8_t>(set.bytes[3] | bits >> 8);
}

// The statuses `set` and `other` both have.
StatusSet shared(const StatusSet& set, const StatusSet& other) {
    return set.without(set.without(other));
}

std::uint8_t raw(ItemId id) {
    return static_cast<std::uint8_t>(id);
}

bool noEffect(BattleState&, AttackInFlight&, BattlerSlot, BattlerSlot, BattleScriptBuilder&) {
    return false;
}

// The effect missed the target: it is shown "Miss" and the attack does nothing more to it.
bool misses(AttackInFlight& attack, BattlerSlot target) {
    attack.scratch.missed.add(target);
    attack.params.targetsHit.remove(target);
    return true;
}

// `slot` hides and dies once the attack's statuses take effect.
void setToHideAndDie(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    battler.statusesToSet.set(StatusId::HIDE);
    battler.statusesToSet.set(StatusId::DEAD);
}

// The target leaves the battle once the action is over: a party member has left the battle, a
// monster has fallen.
void departs(BattleState& state, BattlerSlot target) {
    state.leavingAfterAction.add(target);
    if (target.isCharacter()) {
        state.partyLeft.add(target);
    } else {
        state.monstersFallen.add(target);
    }
}

// Sketch's and Control's chance: the attacker's level over the target's, with the target's taken
// down to two thirds by the relic. A quotient of one level or more succeeds without a draw; below
// one, a draw under the quotient's share of 256 does.
constexpr int kRelicLevelShare = 170;  // out of 256
constexpr int kLevelCheckScale = 256;

bool levelCheck(BattleState& state, BattlerSlot attacker, BattlerSlot target, bool relic) {
    int divisor = state.at(target).level;
    if (relic) {
        divisor = divisor * kRelicLevelShare / kLevelCheckScale;
    }
    const int quotient = divide(state.at(attacker).level * kLevelCheckScale, divisor);
    if (quotient >= kLevelCheckScale) {
        return true;
    }
    return state.rng.next() < quotient;
}

// One status drawn from the attack's: statuses 1-2 or 3-4 in proportion to how many each holds, then
// one of that word's. It goes on the target with the attack's other statuses.
void giveOneStatus(BattleState& state, const AttackInFlight& attack, BattlerSlot target) {
    const std::uint16_t first = firstWord(attack.record.statuses);
    const std::uint16_t second = secondWord(attack.record.statuses);
    const int firstCount = std::popcount(first);
    const int total = firstCount + std::popcount(second);
    const bool fromSecond = state.rng.below(static_cast<std::uint8_t>(total)) >= firstCount;
    const std::uint16_t status = pickBit(state.rng, fromSecond ? second : first);
    StatusSet& toSet = state.at(target).statusesToSet;
    if (fromSecond) {
        addToSecondWord(toSet, status);
    } else {
        addToFirstWord(toSet, status);
    }
}

// --- the weapons ---------------------------------------------------------------------------------

// The instant kill draws one time in four, then the target's stamina saves it on a draw of 0 to 127
// at or below its stamina.
constexpr std::uint8_t kInstantKillBelow = 0x40;
constexpr std::uint8_t kStaminaSaveBits = 0x7F;
// The kill is shown as a spell casting the animation at this attack's index: the Scimitar's and
// Zantetsuken's, and the X of the Striker, Wing Edge and Trump.
constexpr AttackId kScimitarAnimation = AttackId::BATTLE;
constexpr AttackId kXAnimation = AttackId::FLASH_TOOL;

// The instant kill of the Scimitar, Zantetsuken, Striker, Wing Edge and Trump. A target protected
// from instant death is never killed. The X weapons kill the undead without a draw. A US cartridge
// never kills during the later landings of a Dragon Horn's jump.
void instantKill(BattleState& state, AttackInFlight& attack, BattlerSlot target, BattleScriptBuilder& script,
                 bool undeadCertain, AttackId animation) {
    Battler& battler = state.at(target);
    if (battler.instantDeathProtected) {
        return;
    }
    if (!(undeadCertain && battler.undead)) {
        if (state.rng.next() >= kInstantKillBelow) {
            return;
        }
        if (battler.stamina >= (state.rng.next() & kStaminaSaveBits)) {
            return;
        }
    }
    if (language(state.version) == Language::EN && attack.scratch.attacksLeft != 0 &&
        attack.command == BattleCommandId::JUMP) {
        return;
    }
    attack.params.targetsHit.add(target);
    attack.scratch.earlierTargets.remove(target);
    attack.params.castingSwirl = true;
    battler.statusesToSet.set(StatusId::DEAD);
    attack.scratch.hideWithoutTargets = true;
    attack.record.power = 0;
    attack.command = BattleCommandId::MAGIC;
    attack.attack = animation;
    if (animation == kScimitarAnimation && target.isMonster()) {
        battler.statusesToSet.set(StatusId::HIDE);
    }
    script.addAnimation(animationOf(attack));
    script.addParams(attack.params);
}

bool scimitarKills(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target,
                   BattleScriptBuilder& script) {
    instantKill(state, attack, target, script, false, kScimitarAnimation);
    return false;
}

bool doomsTheUndead(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                    BattleScriptBuilder& script);

// The X weapons: the instant kill, then Doom's rule on the undead whether or not the kill came.
bool xKills(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
            BattleScriptBuilder& script) {
    instantKill(state, attack, target, script, true, kXAnimation);
    return doomsTheUndead(state, attack, attacker, target, script);
}

// The Man Eater strikes a human for two more steps of the multiplier.
constexpr int kManEaterSteps = 2;

bool manEater(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    if (state.at(target).human) {
        attack.multiplier += kManEaterSteps;
    }
    return false;
}

// The Sniper and Hawkeye strike half again on a coin; a Fight at a floating target strikes three more
// steps and is shown as the weapon thrown.
constexpr int kSniperFloatSteps = 3;

bool sniper(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder& script) {
    if (!state.rng.coin()) {
        return false;
    }
    ++attack.multiplier;
    if (!state.at(target).status.has(StatusId::FLOAT) || attack.command != BattleCommandId::FIGHT) {
        return false;
    }
    attack.multiplier += kSniperFloatSteps;
    attack.command = BattleCommandId::THROW;
    // A Fight's second value is the weapon's item plus one: the weapon is what is shown thrown.
    attack.attack = static_cast<AttackId>(static_cast<std::uint8_t>(attack.secondary - 1));
    AttackAnimation thrown = animationOf(attack);
    thrown.item = static_cast<ItemId>(static_cast<std::uint8_t>(attack.attack));
    script.replaceAnimation(thrown);
    return false;
}

// --- the attacks ---------------------------------------------------------------------------------

bool scan(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    if (state.at(target).monsterFlags.has(MonsterBattleFlag::CANT_SCAN)) {
        attack.scratch.message = kMessageCantProbe;
        return false;
    }
    addImmediateAction(state, ScanAction{.target = target});
    return false;
}

// Metamorph turns a monster into an item from its pack — one of four, drawn by two coins — at the odds
// its metamorph byte names. "Better off {item}!" takes the place of the attack's animation before
// the odds are drawn, and the animation is shown again after it.
bool metamorph(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
               BattleScriptBuilder& script) {
    if (target.isCharacter()) {
        return misses(attack, target);
    }
    const MetamorphInfo info = state.at(target).metamorph;
    const bool firstCoin = state.rng.coin();
    const bool secondCoin = state.rng.coin();
    const std::size_t draw = (firstCoin ? 2u : 0u) + (secondCoin ? 1u : 0u);
    const ItemId item = getMetamorphPack(info).items[draw];
    attack.scratch.messageItem = item;
    script.replaceAnimation(BattleMessage{.message = kMessageBetterOff, .firstNumber = raw(item)});
    script.addAnimation(animationOf(attack));
    if (state.rng.next() >= metamorphRate(info)) {
        return misses(attack, target);
    }
    state.at(attacker).itemInHand = item;
    if (attacker.isCharacter()) {
        state.holdingItem.add(attacker);
    }
    state.at(target).statusesToSet.set(StatusId::DEAD);
    return false;
}

// Palidor carries the target off: it leaves the field with the action, and a Jump with no targets
// goes to the front of its actions.
bool palidor(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    battler.pendingUpdates.leaveTheField = true;
    battler.actions.push_front(CommandAction{.command = BattleCommandId::JUMP});
    return false;
}

// Tapir wakes nothing but fills a sleeping target's HP and MP.
bool tapir(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    if (!battler.status.has(StatusId::SLEEP)) {
        return misses(attack, target);
    }
    battler.hp = battler.maxHp;
    battler.mp = battler.maxMp;
    return false;
}

// Exploder's user among its own targets is taken out of the targets hit here — and the frame puts
// every target the effect did not miss back, so the user takes its own damage. A monster's own bit is
// not in the byte this reads.
bool exploder(BattleState&, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    if (target == attacker && attacker.isCharacter()) {
        attack.params.targetsHit.remove(target);
    }
    return false;
}

// Reflect??? acts on its attacker only; its row in this table does nothing.
bool reflectLore(BattleState&, AttackInFlight&, BattlerSlot, BattlerSlot, BattleScriptBuilder&) {
    return false;
}

// Dischord halves the target's level, rounding up.
bool dischord(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    battler.level = static_cast<std::uint8_t>(static_cast<std::uint8_t>(battler.level + 1) / 2);
    return false;
}

// Pep Up: the caster gives up its HP and MP, hides and dies, and is gone once the action is over;
// the target's MP is filled.
bool pepUp(BattleState& state, AttackInFlight&, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    setToHideAndDie(state, attacker);
    state.leavingAfterAction.add(attacker);
    Battler& caster = state.at(attacker);
    caster.hp = 0;
    caster.mp = 0;
    Battler& battler = state.at(target);
    battler.mp = battler.maxMp;
    return false;
}

// Rippler swaps the statuses only one of the two has: each loses its own and gains the other's. The
// four waiting words are written over, not added to.
bool rippler(BattleState& state, AttackInFlight&, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    Battler& caster = state.at(attacker);
    Battler& battler = state.at(target);
    const StatusSet both = shared(caster.status, battler.status);
    const StatusSet casters = caster.status.without(both);
    const StatusSet targets = battler.status.without(both);
    battler.statusesToSet = casters;
    caster.statusesToClear = casters;
    caster.statusesToSet = targets;
    battler.statusesToClear = targets;
    return false;
}

// Stone strikes a target of the caster's own level for fourteen more steps of the multiplier.
constexpr int kStoneSteps = 14;

bool stone(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    if (state.at(attacker).level == state.at(target).level) {
        attack.multiplier += kStoneSteps;
    }
    return false;
}

bool noCounterattack(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot, BattleScriptBuilder&) {
    state.scratch.countersEnabled = false;
    return false;
}

bool missesFloating(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target,
                    BattleScriptBuilder&) {
    if (state.at(target).status.has(StatusId::FLOAT)) {
        return misses(attack, target);
    }
    return false;
}

// Wallchange: one element the target is weak to, drawn; every other nullified; one of those others
// absorbed too. Halved elements are left.
constexpr std::uint8_t kEveryElement = 0xFF;

bool wallchange(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    const auto weak = static_cast<std::uint8_t>(pickBit(state.rng, kEveryElement));
    battler.elementsWeak.bits = weak;
    const auto others = static_cast<std::uint8_t>(~weak);
    battler.elementsNullified.bits = others;
    battler.elementsAbsorbed.bits = static_cast<std::uint8_t>(pickBit(state.rng, others));
    return false;
}

// Escape, Sneeze, Smoke Bomb: the target leaves the battle once the action is over.
bool leavesTheBattle(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    departs(state, target);
    return false;
}

// Mind Blast: the target's waiting statuses are emptied, and it gets one of the attack's statuses for
// each party position whose pick named it, position 3 first.
bool mindBlast(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    battler.statusesToSet = {};
    battler.statusesToClear = {};
    for (int position = kPartySlots - 1; position >= 0; --position) {
        if (attack.scratch.mindBlastPicks[static_cast<std::size_t>(position)] == target) {
            giveOneStatus(state, attack, target);
        }
    }
    return false;
}

bool rPolarity(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    battler.backRow = !battler.backRow;
    return false;
}

// Love Token: the target takes the hits aimed at the caster.
bool loveToken(BattleState& state, AttackInFlight&, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    state.at(attacker).bodyguard = target;
    state.at(target).guarding = attacker;
    return false;
}

// Seize takes hold of one party member: not a monster, and not while the attacker already holds
// someone. The one held cannot act and its menu does not open.
bool seize(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    Battler& holder = state.at(attacker);
    if (holder.seizing || target.isMonster()) {
        return misses(attack, target);
    }
    holder.seizing = target;
    Battler& held = state.at(target);
    held.seizedBy = attacker;
    holder.aiVariable = static_cast<std::uint8_t>(holder.aiVariable | kAiVariableHoldingSeize);
    held.menuMayOpen = false;
    held.pendingUpdates.removeAllActions = true;
    return false;
}

// Targetting: the monster's next scripted attack goes to the target.
bool targetting(BattleState& state, AttackInFlight&, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    state.at(attacker).targetingTarget = target;
    return false;
}

bool suplex(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    if (state.at(target).monsterFlags.has(MonsterBattleFlag::CANT_SUPLEX)) {
        return misses(attack, target);
    }
    return false;
}

// Bababreath blows a party member away: taken from the party, and gone from the battle.
bool bababreath(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    if (target.isCharacter()) {
        state.takenFromTheParty.add(target);
    }
    departs(state, target);
    return false;
}

// Charm takes one battler at a time.
bool charm(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    Battler& charmer = state.at(attacker);
    if (charmer.charming) {
        return misses(attack, target);
    }
    charmer.charming = target;
    state.at(target).charmedBy = attacker;
    return false;
}

// Doom on the undead: a party member does not die and is brought to full HP; a monster hides, then
// fades out and comes back at full HP. The living are left as they are.
bool doomsTheUndead(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target,
                    BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    if (!battler.undead) {
        return false;
    }
    if (target.isCharacter()) {
        battler.statusesToSet.clear(StatusId::DEAD);
        battler.hp = battler.maxHp;
        return false;
    }
    battler.statusesToSet.set(StatusId::HIDE);
    attack.targets = BattlerSet::of(target);
    addImmediateAction(state, MonsterVisibilityAction{.mode = MonsterVisibilityMode::ReviveAtFullHp,
                                                      .monsters = BattlerSet::of(target),
                                                      .type = MonsterEntranceType::FADE_IN_CHECKERED});
    return false;
}

bool overcast(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    state.at(target).triggers.overcast = true;
    return false;
}

// Engulf swallows a party member: engulfed, and gone from the battle.
bool engulf(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    if (target.isCharacter()) {
        state.engulfed.add(target);
    }
    departs(state, target);
    return false;
}

// Zinger: the monster swallows the target and is off the field once the action is over.
bool zinger(BattleState& state, AttackInFlight&, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    state.heldByZinger = attacker;
    state.swallowedByZinger = target;
    if (attacker.isMonster()) {
        state.leavingAfterAction.add(attacker);
    }
    return false;
}

// Evil Toot: the target's waiting statuses are emptied and one of the attack's statuses goes on.
bool evilToot(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    battler.statusesToSet = {};
    battler.statusesToClear = {};
    giveOneStatus(state, attack, target);
    return false;
}

bool phantasm(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    state.at(target).triggers.phantasm = true;
    return false;
}

// Stunner's statuses take hold on a draw below its hit rate; on any other draw they fail.
bool stunner(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    if (state.rng.next() < attack.record.hitRate) {
        return false;
    }
    Battler& battler = state.at(target);
    battler.statusesToSet = battler.statusesToSet.without(attack.record.statuses);
    return false;
}

bool fallenOne(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    state.at(target).hp = 1;
    return false;
}

// Discard lets go of the battler held in Seize.
bool discard(BattleState& state, AttackInFlight&, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    Battler& holder = state.at(attacker);
    holder.aiVariable = static_cast<std::uint8_t>(holder.aiVariable & ~kAiVariableHoldingSeize);
    holder.seizing.reset();
    state.at(target).seizedBy.reset();
    return false;
}

// --- the items -----------------------------------------------------------------------------------

// A Super Ball's hit does 256 to 2,048, a draw's low three bits plus one, times 256 — drawn for each
// target.
constexpr std::uint8_t kSuperBallBits = 0x07;
constexpr int kSuperBallDamageStep = 256;

bool superBall(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot, BattleScriptBuilder&) {
    attack.damage = ((state.rng.next() & kSuperBallBits) + 1) * kSuperBallDamageStep;
    return false;
}

// An Elixir fills the MP; the castable spells are worked out again.
bool elixir(BattleState& state, AttackInFlight&, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    battler.pendingUpdates.enabledMagic = true;
    battler.mp = battler.maxMp;
    return false;
}

// --- the commands --------------------------------------------------------------------------------

// Possess: the possessor and the possessed both hide and die; a party member possessing is gone from
// the battle and from the party once it ends. The possessor is marked to answer the possessed.
bool possess(BattleState& state, AttackInFlight&, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    if (attacker.isCharacter()) {
        state.leavingAfterAction.add(attacker);
        state.takenFromTheParty.add(attacker);
    }
    setToHideAndDie(state, attacker);
    setToHideAndDie(state, target);
    markToRetaliate(state, target, attacker);
    return false;
}

// A failed Steal is remembered by the target as a Fight.
void stealFailed(Battler& target) {
    if (target.lastHit.command) {
        target.lastHit.command->with = BattleCommandId::FIGHT;
    }
}

// A party member's steal: the chance is 50 plus the thief's level less the target's, out of 100,
// doubled by a Sneak Ring; 128 or more is certain, below 0 fails. The rare item one time in eight, the
// common one otherwise; either way the target has nothing left to steal.
constexpr int kStealBonus = 50;
constexpr int kStealCertain = 128;
constexpr int kStealCertainReach = 0xFF;  // a level and the 50 past a byte
constexpr std::uint8_t kStealPercent = 100;
constexpr std::uint8_t kRareStealBelow = 0x20;

bool characterSteals(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target) {
    Battler& thief = state.at(attacker);
    Battler& victim = state.at(target);
    if (victim.rareSteal == ItemId::EMPTY && victim.commonSteal == ItemId::EMPTY) {
        stealFailed(victim);
        return false;
    }
    attack.scratch.message = kMessageCouldntSteal;
    const int chance = thief.level + kStealBonus - victim.level;
    const bool certain = thief.level + kStealBonus >= kStealCertainReach || chance >= kStealCertain;
    if (!certain) {
        if (chance < 0) {
            stealFailed(victim);
            return false;
        }
        const int odds = thief.relicEffects3.has(RelicEffect3::RAISE_STEAL_RATE) ? chance * 2 : chance;
        if (state.rng.below(kStealPercent) >= odds) {
            stealFailed(victim);
            return false;
        }
    }
    const ItemId item = state.rng.next() < kRareStealBelow ? victim.rareSteal : victim.commonSteal;
    if (item == ItemId::EMPTY) {
        stealFailed(victim);
        return false;
    }
    attack.scratch.messageItem = item;
    thief.itemInHand = item;
    state.holdingItem.add(attacker);
    victim.rareSteal = ItemId::EMPTY;
    victim.commonSteal = ItemId::EMPTY;
    attack.scratch.message = kMessageStole;
    return false;
}

// A monster's steal takes gil: it fails one time in four, and takes the monster's level times 256,
// plus 20, from the party's gil — where GP Rain beside it multiplies by 30.
constexpr std::uint8_t kMonsterStealFailsFrom = 0xC0;
constexpr std::uint32_t kMonsterStealLevelStep = 256;
constexpr std::uint32_t kMonsterStealExtra = 20;
constexpr std::uint32_t kGoldMax = 0xFFFF;

bool monsterSteals(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target) {
    Battler& thief = state.at(attacker);
    Battler& victim = state.at(target);
    attack.scratch.message = kMessageCouldntSteal;
    if (state.rng.next() >= kMonsterStealFailsFrom) {
        stealFailed(victim);
        return false;
    }
    attack.scratch.message = kMessageDoesntHaveAnything;
    const std::uint32_t taken = takeGil(state.party, thief.level * kMonsterStealLevelStep + kMonsterStealExtra);
    if (taken == 0) {
        stealFailed(victim);
        return false;
    }
    attack.scratch.messageAmount = taken;
    thief.gold = static_cast<std::uint16_t>(std::min(thief.gold + taken, kGoldMax));
    attack.scratch.message = kMessageGpStolen;
    return false;
}

// Steal never misses: a failed steal shows its message over the target.
bool steal(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    attack.scratch.message = kMessageDoesntHaveAnything;
    if (attacker.isMonster()) {
        return monsterSteals(state, attack, attacker, target);
    }
    return characterSteals(state, attack, attacker, target);
}

// A monster in any of these cannot be controlled.
const StatusSet kCannotBeControlled =
    StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::INVISIBLE, StatusId::ZOMBIE, StatusId::SLEEP,
                  StatusId::CONFUSE, StatusId::BERSERK, StatusId::RAGE, StatusId::HIDE, StatusId::MORPH);

// Control takes hold of a monster that can be controlled and is not already held, on the level check
// (a Coronet helps). The controller chants while it holds the monster, which is drawn facing the
// other way, and its menu offers the monster's control attacks. A failed level check shows nothing.
bool control(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
             BattleScriptBuilder&) {
    Battler& monster = state.at(target);
    if (target.isCharacter() || monster.monsterFlags.has(MonsterBattleFlag::CANT_CONTROL) ||
        monster.status.hasAny(kCannotBeControlled) || (monster.controlledBy && !monster.controlEnding)) {
        attack.scratch.message = kMessageCantControl;
        return misses(attack, target);
    }
    Battler& controller = state.at(attacker);
    if (!levelCheck(state, attacker, target, controller.relicEffects3.has(RelicEffect3::RAISE_CONTROL_RATE))) {
        return misses(attack, target);
    }
    controller.controlling = target;
    controller.controlEnding = false;
    monster.controlledBy = attacker;
    monster.controlEnding = false;
    state.flippedByControl.add(target);
    if (language(state.version) == Language::EN) {
        controller.triggers.usesControlMenu = true;
    }
    // Chant goes onto the status itself, not through the statuses waiting to take effect.
    controller.status.set(StatusId::CHANT);
    monster.emptyGaugeAfterAction = true;
    controlMenu(state, attacker, target);
    return false;
}

// Leap: Gau hides and the battle ends with him leaping onto the Veldt — unless the formation forbids
// it or he is the only party member fighting.
constexpr int kLeapAllies = 2;

bool leap(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target, BattleScriptBuilder&) {
    if (getFormationAux(state.formation).leapDisabled() || state.allyCount < kLeapAllies) {
        attack.scratch.message = kMessageCantDive;
        return misses(attack, target);
    }
    state.at(attacker).statusesToSet.set(StatusId::HIDE);
    state.endOfBattleEvent = EndOfBattleEvent::GauLeapt;
    return false;
}

// Sketch catches a monster that can be sketched, on the level check (a Beret helps): the sketcher
// makes one of its two sketch attacks next — the second three draws in four. A failed check shows
// nothing.
constexpr std::uint8_t kSecondSketchFrom = 0x40;

bool sketch(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
            BattleScriptBuilder& script) {
    if (target.isCharacter()) {
        return misses(attack, target);
    }
    const Battler& monster = state.at(target);
    if (monster.monsterFlags.has(MonsterBattleFlag::CANT_SKETCH)) {
        attack.scratch.message = kMessageCantSketch;
        return misses(attack, target);
    }
    if (!levelCheck(state, attacker, target, state.at(attacker).relicEffects3.has(RelicEffect3::RAISE_SKETCH_RATE))) {
        return misses(attack, target);
    }
    attack.secondary = static_cast<std::uint8_t>(target.position());
    script.replaceAnimation(animationOf(attack));
    const std::size_t slot = state.rng.next() >= kSecondSketchFrom ? 1 : 0;
    attack.scratch.sketch = SketchDrawn{.monster = target};
    attack.scratch.followUpSpell = getMonsterSketch(monster.monster.value_or(MonsterId{})).attacks[slot];
    return false;
}

// Debilitator makes the target weak to one element it is not yet weak to and Forcefield has not
// nullified, drawn at random, and no longer halving, absorbing or nullifying it.
bool debilitator(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    const auto open =
        static_cast<std::uint8_t>(~(battler.elementsWeak.bits | state.elementsNullifiedByForcefield.bits));
    if (open == 0) {
        return misses(attack, target);
    }
    const auto element = static_cast<std::uint8_t>(pickBit(state.rng, open));
    attack.scratch.message = static_cast<std::uint8_t>(kMessageNowWeakAgainst + std::countr_zero(element));
    const auto others = static_cast<std::uint8_t>(~element);
    battler.elementsWeak.bits = static_cast<std::uint8_t>(battler.elementsWeak.bits | element);
    battler.elementsHalved.bits = static_cast<std::uint8_t>(battler.elementsHalved.bits & others);
    battler.elementsAbsorbed.bits = static_cast<std::uint8_t>(battler.elementsAbsorbed.bits & others);
    battler.elementsNullified.bits = static_cast<std::uint8_t>(battler.elementsNullified.bits & others);
    return false;
}

// Air Anchor marks a target that instant death can reach: its next action kills it. Counterattacks
// are off for this action.
bool airAnchor(BattleState& state, AttackInFlight& attack, BattlerSlot, BattlerSlot target, BattleScriptBuilder&) {
    Battler& battler = state.at(target);
    if (battler.instantDeathProtected) {
        return misses(attack, target);
    }
    attack.scratch.message = kMessageMoveAndYoureDust;
    battler.airAnchored = true;
    state.scratch.countersEnabled = false;
    return false;
}

// --- the table -----------------------------------------------------------------------------------

struct TargetEffectRow {
    std::uint8_t effect;
    TargetHandler handler;
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

// Every effect that acts on the targets hit, by its index in the effect space.
constexpr std::array kTargetEffectRows{
    TargetEffectRow{indexOf(WeaponSpecialEffect::INSTANT_KILL), xKills},
    TargetEffectRow{indexOf(WeaponSpecialEffect::MAN_EATER), manEater},
    TargetEffectRow{indexOf(WeaponSpecialEffect::SNIPER_HAWKEYE), sniper},
    TargetEffectRow{indexOf(WeaponSpecialEffect::SCIMITAR_ZANTETSUKEN), scimitarKills},
    TargetEffectRow{indexOf(AttackSpecialEffect::SCAN), scan},
    TargetEffectRow{indexOf(AttackSpecialEffect::METAMORPH), metamorph},
    TargetEffectRow{indexOf(AttackSpecialEffect::PALIDOR), palidor},
    TargetEffectRow{indexOf(AttackSpecialEffect::TAPIR), tapir},
    TargetEffectRow{indexOf(AttackSpecialEffect::EXPLODER), exploder},
    TargetEffectRow{indexOf(AttackSpecialEffect::REFLECT_LORE), reflectLore},
    TargetEffectRow{indexOf(AttackSpecialEffect::DISCHORD), dischord},
    TargetEffectRow{indexOf(AttackSpecialEffect::PEP_UP), pepUp},
    TargetEffectRow{indexOf(AttackSpecialEffect::RIPPLER), rippler},
    TargetEffectRow{indexOf(AttackSpecialEffect::STONE), stone},
    TargetEffectRow{indexOf(AttackSpecialEffect::DISABLE_COUNTERATTACK), noCounterattack},
    TargetEffectRow{indexOf(AttackSpecialEffect::MISSES_FLOATING_TARGETS), missesFloating},
    TargetEffectRow{indexOf(AttackSpecialEffect::WALLCHANGE), wallchange},
    TargetEffectRow{indexOf(AttackSpecialEffect::ESCAPE), leavesTheBattle},
    TargetEffectRow{indexOf(AttackSpecialEffect::MIND_BLAST), mindBlast},
    TargetEffectRow{indexOf(AttackSpecialEffect::R_POLARITY), rPolarity},
    TargetEffectRow{indexOf(AttackSpecialEffect::LOVE_TOKEN), loveToken},
    TargetEffectRow{indexOf(AttackSpecialEffect::SEIZE), seize},
    TargetEffectRow{indexOf(AttackSpecialEffect::TARGETTING), targetting},
    TargetEffectRow{indexOf(AttackSpecialEffect::SUPLEX), suplex},
    TargetEffectRow{indexOf(AttackSpecialEffect::BABABREATH), bababreath},
    TargetEffectRow{indexOf(AttackSpecialEffect::CHARM), charm},
    TargetEffectRow{indexOf(AttackSpecialEffect::DOOM), doomsTheUndead},
    TargetEffectRow{indexOf(AttackSpecialEffect::OVERCAST), overcast},
    TargetEffectRow{indexOf(AttackSpecialEffect::SNEEZE), leavesTheBattle},
    TargetEffectRow{indexOf(AttackSpecialEffect::ENGULF), engulf},
    TargetEffectRow{indexOf(AttackSpecialEffect::ZINGER), zinger},
    TargetEffectRow{indexOf(AttackSpecialEffect::EVIL_TOOT), evilToot},
    TargetEffectRow{indexOf(AttackSpecialEffect::PHANTASM), phantasm},
    TargetEffectRow{indexOf(AttackSpecialEffect::STUNNER), stunner},
    TargetEffectRow{indexOf(AttackSpecialEffect::FALLEN_ONE), fallenOne},
    TargetEffectRow{indexOf(AttackSpecialEffect::DISCARD), discard},
    TargetEffectRow{indexOf(ItemUseEffect::SUPER_BALL), superBall},
    TargetEffectRow{indexOf(ItemUseEffect::SMOKE_BOMB), leavesTheBattle},
    TargetEffectRow{indexOf(ItemUseEffect::ELIXIR), elixir},
    TargetEffectRow{indexOf(CommandEffect::Possess), possess},
    TargetEffectRow{indexOf(CommandEffect::Steal), steal},
    TargetEffectRow{indexOf(CommandEffect::Control), control},
    TargetEffectRow{indexOf(CommandEffect::Leap), leap},
    TargetEffectRow{indexOf(CommandEffect::Sketch), sketch},
    TargetEffectRow{indexOf(CommandEffect::Debilitator), debilitator},
    TargetEffectRow{indexOf(CommandEffect::AirAnchor), airAnchor},
};

constexpr std::array<TargetHandler, kEffectCount> targetEffectTable() {
    std::array<TargetHandler, kEffectCount> table{};
    table.fill(noEffect);
    for (const TargetEffectRow& row : kTargetEffectRows) {
        table[row.effect] = row.handler;
    }
    return table;
}

constexpr std::array<TargetHandler, kEffectCount> kTargetEffects = targetEffectTable();

static_assert(kTargetEffectRows.size() == 46, "the target effect table names 46 effects");

}  // namespace

bool targetEffect(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target,
                  BattleScriptBuilder& script) {
    return kTargetEffects[attack.effect.index()](state, attack, attacker, target, script);
}

}  // namespace ostinato::battle
