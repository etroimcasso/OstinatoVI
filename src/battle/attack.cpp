#include "battle/attack.h"

#include <cstddef>
#include <utility>

#include "battle/commands.h"
#include "battle/targeting.h"
#include "data/attack_properties.h"
#include "ostinato/attack_flags.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::battle {

namespace {

// A battler with Runic up draws in a spell unless it is in one of these.
const StatusSet kCannotAbsorb = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::SLEEP, StatusId::STOP,
                                              StatusId::FROZEN, StatusId::HIDE);

// Strago learns a lore he sees unless he is in one of these.
const StatusSet kCannotLearnLore =
    StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::BLIND, StatusId::ZOMBIE, StatusId::SLEEP,
                  StatusId::CONFUSE, StatusId::BERSERK, StatusId::STOP, StatusId::FROZEN, StatusId::RAGE,
                  StatusId::HIDE);

// A target in one of these cannot evade.
const StatusSet kCannotEvade = StatusSet::of(StatusId::PETRIFY, StatusId::SLEEP, StatusId::STOP, StatusId::FROZEN);

// A party member in one of these shows no block.
const StatusSet kShowsNoBlock = StatusSet::of(StatusId::ZOMBIE, StatusId::MAGITEK, StatusId::INVISIBLE);

// One in 32 blows is a critical hit: a draw below this.
constexpr std::uint8_t kCriticalBelow = 8;

// A weapon's spell is cast, and an Image is used up, one time in four: a draw below this.
constexpr std::uint8_t kOneInFourBelow = 0x40;

// The hit check rolls a percentage, and the stamina save a number below this.
constexpr std::uint8_t kPercent = 100;
constexpr std::uint8_t kStaminaSaveRange = 0x80;

// An attack with this hit rate always reaches a target that can evade.
constexpr std::uint8_t kAlwaysHits = 0xFF;

// A critical hit doubles the damage; a morphed attacker's attack too.
constexpr int kDoubled = 2;

std::uint8_t raw(AttackId id) {
    return static_cast<std::uint8_t>(id);
}

// The animation the attack in flight is shown with.
AttackAnimation animationOf(const AttackInFlight& attack) {
    AttackAnimation animation{.attack = attack.attack,
                              .offHand = attack.offHand,
                              .secondary = attack.secondary,
                              .chainSawKill = attack.chainSawKill,
                              .showsAttack = attack.showsAttack,
                              .newBackground = attack.newBackground};
    const auto command = static_cast<std::uint8_t>(attack.command);
    if (command >= static_cast<std::uint8_t>(SpecialAnimation::DanceStumble)) {
        animation.what = static_cast<SpecialAnimation>(command);
    } else {
        animation.what = attack.command;
    }
    if (attack.command == BattleCommandId::ITEM || attack.command == BattleCommandId::THROW ||
        attack.command == BattleCommandId::TOOLS) {
        animation.item = static_cast<ItemId>(raw(attack.queuedAttack));
    }
    return animation;
}

std::optional<BattlerSlot> stragoInBattle(const BattleState& state) {
    for (int position = 0; position < kPartySlots; ++position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (state.at(slot).actor == CharacterPropId::STRAGO) {
            return slot;
        }
    }
    return std::nullopt;
}

// The name the attack is shown under, once per command.
void showAttackName(BattleState& state, BattleScriptBuilder& script, AttackInFlight& attack, BattlerSlot attacker) {
    if (attack.nameShown) {
        return;
    }
    attack.nameShown = true;
    if (!attack.scratch.nameKind) {
        return;
    }
    switch (*attack.scratch.nameKind) {
    case AttackNameKind::Attack:
        script.add(AttackNameShown{.attack = attack.attack});
        break;
    case AttackNameKind::Item:
        script.add(ItemNameShown{.item = static_cast<ItemId>(raw(attack.queuedAttack))});
        break;
    case AttackNameKind::Esper:
        script.add(EsperNameShown{.esper = static_cast<EsperId>(raw(attack.attack))});
        break;
    case AttackNameKind::Bushido:
        script.add(BushidoNameShown{.technique = attack.attack});
        break;
    case AttackNameKind::Command:
        script.add(CommandNameShown{.command = attack.command});
        break;
    case AttackNameKind::MonsterSpecialAttack: {
        const Battler& battler = state.at(attacker);
        script.add(MonsterSpecialAttackNameShown{.monster = battler.monster.value_or(battler.rage.value_or(MonsterId{}))});
        break;
    }
    case AttackNameKind::JokerDoom:
        attack.scratch.aimExactly = true;
        attack.record.traits.clear(AttackTrait::INSTANT_DEATH);
        attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
        script.add(AttackNameShown{.attack = kJokerDoomName});
        break;
    case AttackNameKind::Blitz:
        script.add(AttackNameShown{.attack = attack.queuedAttack});
        break;
    }
}

// A weapon's spell cast after the hit replaces the next attack: aimed afresh, free, never a
// critical hit. Cast after the hit itself, it goes after one of the hit's targets.
void startFollowUpSpell(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    if (!attack.scratch.followUpSpell) {
        return;
    }
    attack.attack = *attack.scratch.followUpSpell;
    attack.command = commandForAttack(attack.attack);
    initTargeting(attack, attack.command, attack.attack);
    initAttacker(state, attack, attacker);
    attack.record.mpCost = 0;
    attack.scratch.followUpSpell.reset();
    attack.canBeCritical = false;
    if (attack.followUpSameTargets) {
        attack.record.flags1.clear(AttackFlag1::LEARNABLE_LORE);
        attack.record.flags1.clear(AttackFlag1::ENABLE_RUNIC);
        attack.rules.mayTargetTheFallen = true;
        attack.rules.keepTargetsThatAreGone = true;
        attack.targeting = Targeting::of(TargetFlags::ENEMY);
        if (attack.record.flags2.has(AttackFlag2::STAMINA_DEFENSE)) {
            attack.scratch.quietMisses = true;
        } else {
            attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
        }
    } else {
        attack.scratch.randomizeTargets = true;
        attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    }
    attack.record.flags1.set(AttackFlag1::IGNORE_REFLECT);
}

// A spell that Runic may draw in goes to every battler with Runic up instead, restoring their MP
// by its cost shared between them. Runic ends for each of them, awake or not.
void absorbWithRunic(BattleState& state, AttackInFlight& attack) {
    if (!attack.record.flags1.has(AttackFlag1::ENABLE_RUNIC)) {
        return;
    }
    BattlerSet absorbers;
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        Battler& battler = state.at(slot);
        if (!battler.present || (!battler.triggers.runic && !battler.triggers.monsterRunic)) {
            continue;
        }
        battler.triggers.runic = false;
        if (!battler.status.hasAny(kCannotAbsorb)) {
            absorbers.add(slot);
        }
    }
    if (absorbers.empty()) {
        return;
    }
    rememberTargets(attack);
    attack.scratch.randomizeTargets = true;
    attack.targets = absorbers;
    attack.record.statuses = {};
    attack.record.flags1 = AttackFlags1::of(AttackFlag1::AFFECT_MP, AttackFlag1::IGNORE_REFLECT);
    attack.record.flags2 = AttackFlags2::of(AttackFlag2::UNDODGEABLE, AttackFlag2::RESTORE_HP_MP);
    attack.record.traits = AttackTraitSet::of(AttackTrait::IGNORE_DEFENSE, AttackTrait::NO_DAMAGE_SPLIT);
    attack.record.power = static_cast<std::uint8_t>(attack.record.mpCost / absorbers.count());
    attack.attackerLevel = 0;
    attack.attackerStat = 0;
    attack.scratch.damageModification = false;
    attack.runicAbsorbing = true;
    attack.rules = AimingRules{.keepTargetsThatAreGone = true};
    attack.record.misc = {};
    attack.effect = {};
}

// A battler Air Anchor marked dies after its next attack.
void airAnchor(BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    Battler& battler = state.at(attacker);
    if (!battler.airAnchored) {
        return;
    }
    battler.airAnchored = false;
    attack.record.flags1.set(AttackFlag1::KILLS_ATTACKER);
}

// The attacker is set to die, and marked to answer itself (only an attack that affects MP leaves
// the mark ready).
void markToDie(BattleState& state, const AttackInFlight& attack, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (!battler.retaliationPending) {
        battler.lastAttacker = slot;
        battler.retaliationPending = attack.record.flags1.has(AttackFlag1::AFFECT_MP);
    }
    battler.statusesToSet.set(StatusId::DEAD);
}

// A weapon whose spell may be cast casts it one time in four, after the hit, at the hit's targets.
// Not between the landings of a Dragon Horn jump.
void weaponSpellcast(BattleState& state, AttackInFlight& attack) {
    if (attack.command == BattleCommandId::JUMP && attack.scratch.attacksLeft != 0) {
        return;
    }
    if (!attack.scratch.weaponSpellcast.randomOnAttack()) {
        return;
    }
    if (state.rng.next() >= kOneInFourBelow) {
        return;
    }
    attack.scratch.followUpSpell = attack.scratch.weaponSpellcast.spell();
    attack.followUpSameTargets = true;
    ++attack.scratch.attacksLeft;
}

// Strago learns a lore he sees used, if he does not know it yet.
void learnLore(BattleState& state, const AttackInFlight& attack) {
    if (!attack.record.flags1.has(AttackFlag1::LEARNABLE_LORE)) {
        return;
    }
    const std::optional<BattlerSlot> strago = stragoInBattle(state);
    if (!strago || state.at(*strago).status.hasAny(kCannotLearnLore)) {
        return;
    }
    const int lore = raw(attack.attack) - raw(AttackId::CONDEMNED);
    if (lore < 0 || lore >= static_cast<int>(game::kLores)) {
        return;
    }
    const auto index = static_cast<std::size_t>(lore);
    if (!state.party.loresKnown.test(index)) {
        state.loresLearned.set(index);
    }
}

// Mark the target to answer the attacker: always if it was not already waiting to answer
// someone, on a coin flip if it was.
void markToRetaliate(BattleState& state, BattlerSlot attacker, BattlerSlot target) {
    Battler& battler = state.at(target);
    if (!battler.retaliationPending || state.rng.coin()) {
        battler.lastAttacker = attacker;
        battler.retaliationPending = true;
    }
}

// What the attack's params say about who made it and whom it was aimed at.
void initParams(AttackInFlight& attack, BattlerSlot attacker) {
    AttackAnimationParams& params = attack.params;
    params.attackerIsMonster = attacker.isMonster();
    params.attacker = attacker;
    params.targets = attack.targets;
    params.targetsHit = attack.targets;
    params.noCharacterTargets = attack.targets.charactersOnly().empty();
    params.criticalFlash = false;
    params.dragonHorn = false;
    params.monsterAttack = false;
    if (params.noCharacterTargets != params.attackerIsMonster) {
        attack.rules.opposingSideOnly = true;
    }
    params.castingSwirl = !attack.castingSwirlSuppressed;
    attack.castingSwirlSuppressed = false;
    if (attack.scratch.attacksLeft == 0 && attack.scratch.dragonHorn) {
        params.dragonHorn = true;
        attack.rules.randomTarget = true;
        attack.rules.fallBackToEarlierTargets = true;
    }
}

// A monster's special attack takes its special-attack byte: no damage, no dodging, a drain, Reflect
// removed, extra damage or a status.
void applyMonsterSpecialAttack(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    const Battler& battler = state.at(attacker);
    attack.canBeCritical = false;
    attack.params.monsterAttack = true;
    attack.secondary = static_cast<std::uint8_t>(battler.specialAttackAnimation);
    const MonsterSpecialAttack special = battler.specialAttack;
    if (special.noDamage()) {
        attack.record.power = 0;
        attack.record.misc.set(AttackMiscFlag::MISS_IF_STATUS_IMMUNE);
    }
    if (special.cantDodge()) {
        attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    }
    const std::uint8_t effect = special.effectClass();
    constexpr std::uint8_t kFirstBoost = MonsterSpecialAttack::damageBoost(0).packed;
    constexpr std::uint8_t kDrainHp = MonsterSpecialAttack::drainHp().packed;
    constexpr std::uint8_t kDrainMp = MonsterSpecialAttack::drainMp().packed;
    if (effect == kDrainHp || effect == kDrainMp) {
        attack.record.flags2.set(AttackFlag2::DRAIN);
        if (effect == kDrainMp) {
            attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        }
    } else if (effect > kDrainMp) {
        attack.record.flags2.set(AttackFlag2::REMOVE_STATUS);
        attack.record.statuses.set(StatusId::REFLECT);
    } else if (effect >= kFirstBoost) {
        // The band's step count, plus the one the compare carries into the add.
        attack.multiplier += (effect - kFirstBoost) + 1;
    } else {
        attack.record.statuses.set(static_cast<StatusId>(effect));
    }
}

// A missed attack: no critical hit and no weapon spell for the rest of it, and the target is
// shown "Miss" unless misses are quiet.
HitResult missed(AttackInFlight& attack, BattlerSlot target, BlockShown block = BlockShown::None) {
    attack.canBeCritical = false;
    attack.scratch.weaponSpellcast = {};
    if (!attack.scratch.quietMisses) {
        attack.scratch.missed.add(target);
    }
    return HitResult{.hit = false, .block = block};
}

// One of the block animations the party member's equipment can show, or the Golem or
// Interceptor stepping in. Picked at random among them.
BlockShown blockShown(BattleState& state, AttackInFlight& attack, BattlerSlot target, BlockShown stepIn) {
    const Battler& battler = state.at(target);
    const bool physical = attack.record.traits.has(AttackTrait::PHYSICAL);
    // One bit per BlockShown value, value n at bit n - 1.
    unsigned choices = (physical ? battler.physicalBlockGraphics : battler.magicBlockGraphics).bits;
    if (stepIn != BlockShown::None) {
        choices |= 1u << (static_cast<unsigned>(stepIn) - 1);
    }
    if (choices == 0) {
        return BlockShown::None;
    }
    constexpr unsigned kChoiceBits = 8;
    int count = 0;
    for (unsigned bit = 0; bit < kChoiceBits; ++bit) {
        count += static_cast<int>((choices >> bit) & 1u);
    }
    int pick = state.rng.below(static_cast<std::uint8_t>(count));
    for (unsigned bit = 0; bit < kChoiceBits; ++bit) {
        if (((choices >> bit) & 1u) == 0) {
            continue;
        }
        if (pick-- != 0) {
            continue;
        }
        const auto shown = static_cast<BlockShown>(bit + 1);
        if (shown == BlockShown::Interceptor) {
            attack.scratch.interceptorBlocked = target;
        }
        if (shown == BlockShown::Golem) {
            attack.scratch.golemBlocked = target;
        }
        return shown;
    }
    return BlockShown::None;
}

// The miss a party member can block: the block animation shows, and the Golem or Interceptor
// stepping in turns the miss into a hit that carries no statuses.
HitResult missedOrBlocked(BattleState& state, AttackInFlight& attack, BattlerSlot target, BlockShown stepIn) {
    const Battler& battler = state.at(target);
    if (battler.status.hasAny(kShowsNoBlock) || target.isMonster()) {
        return missed(attack, target);
    }
    const BlockShown block = blockShown(state, attack, target, stepIn);
    if (block == BlockShown::Golem || block == BlockShown::Interceptor) {
        attack.record.statuses = {};
        return HitResult{.hit = true, .block = block};
    }
    return missed(attack, target, block);
}

// The stamina save: a target resists by its magic block, then by its stamina.
bool staminaSaves(BattleState& state, const AttackInFlight& attack, const Battler& target) {
    const int chance = (target.magicBlockFactor * attack.record.hitRate) >> 8;
    if (state.rng.below(kPercent) >= chance) {
        return true;
    }
    const int roll = state.rng.next() & (kStaminaSaveRange - 1);
    return target.stamina >= roll;
}

}  // namespace

AttackInFlight startAttack(const CommandAction& command) {
    AttackInFlight attack;
    attack.command = command.command;
    attack.queuedCommand = command.command;
    attack.attack = command.attack;
    attack.queuedAttack = command.attack;
    attack.targets = command.targets;
    attack.scratch.mpCost = command.mpCost;
    return attack;
}

void loadAttackRecord(AttackInFlight& attack, AttackId id) {
    attack.record = getAttackProperties(id);
    attack.effect = SpecialEffect::of(attack.record.specialEffect);
}

void rememberTargets(AttackInFlight& attack) {
    if (attack.scratch.randomizeTargets) {
        attack.targets = attack.scratch.currentTargets;
        return;
    }
    attack.scratch.currentTargets = attack.targets;
    attack.scratch.earlierTargets = attack.scratch.earlierTargets | attack.targets;
}

HitResult checkHit(BattleState& state, AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target) {
    (void)attacker;
    Battler& battler = state.at(target);
    const AttackProperties& record = attack.record;
    const bool physical = record.traits.has(AttackTrait::PHYSICAL);

    if (attack.vanishApplies && battler.status.has(StatusId::INVISIBLE)) {
        if (!record.flags2.has(AttackFlag2::LEVEL_DIVISIBLE)) {
            if (!physical) {
                return HitResult{.hit = true};
            }
            return missedOrBlocked(state, attack, target, BlockShown::None);
        }
        battler.statusesToClear.set(StatusId::INVISIBLE);
    }
    if (!record.flags1.has(AttackFlag1::IGNORE_REFLECT) && battler.status.has(StatusId::REFLECT)) {
        return HitResult{.hit = false, .reflected = true};
    }
    if (record.traits.has(AttackTrait::INSTANT_DEATH) && battler.instantDeathProtected) {
        return missedOrBlocked(state, attack, target, BlockShown::None);
    }
    if (record.traits.has(AttackTrait::RESURRECTION_TARGET) && battler.status.has(StatusId::DEAD) == battler.undead) {
        return missedOrBlocked(state, attack, target, BlockShown::None);
    }
    const bool plainBlow = attack.command == BattleCommandId::FIGHT || attack.command == BattleCommandId::CAPTURE;
    if (plainBlow && attack.effect.isNone() && attack.scratch.targetCount == 1 && target.isCharacter()) {
        if (battler.status.has(StatusId::INTERCEPTOR) && state.rng.coin()) {
            return missedOrBlocked(state, attack, target, BlockShown::Interceptor);
        }
        if (state.golemHp != 0) {
            return missedOrBlocked(state, attack, target, BlockShown::Golem);
        }
    }

    if (record.flags2.has(AttackFlag2::UNDODGEABLE)) {
        return HitResult{.hit = true};
    }
    if (record.flags2.has(AttackFlag2::LEVEL_DIVISIBLE)) {
        // A divide by zero leaves the whole level as the remainder.
        const int remainder = record.hitRate == 0 ? battler.level : battler.level % record.hitRate;
        if (remainder != 0) {
            return missed(attack, target);
        }
        return HitResult{.hit = true};
    }
    if (record.flags2.has(AttackFlag2::STAMINA_DEFENSE)) {
        if (staminaSaves(state, attack, battler)) {
            return missedOrBlocked(state, attack, target, BlockShown::None);
        }
        return HitResult{.hit = true};
    }

    if (battler.status.hasAny(kCannotEvade) || attack.scratch.hitInTheBack.has(target) ||
        record.hitRate == kAlwaysHits) {
        return HitResult{.hit = true};
    }
    if (physical) {
        if (battler.triggers.retort) {
            return HitResult{.hit = true};
        }
        if (battler.status.has(StatusId::IMAGE)) {
            if (state.rng.next() < kOneInFourBelow) {
                battler.statusesToClear.set(StatusId::IMAGE);
            }
            return missed(attack, target);
        }
    }
    // Physical attacks and spells alike are evaded by magic block: the evade rating is never read
    // here, and nothing about the attacker or the target's other statuses changes the chance.
    const int chance = (battler.magicBlockFactor * record.hitRate) >> 8;
    if (state.rng.below(kPercent) < chance) {
        return HitResult{.hit = true};
    }
    return missedOrBlocked(state, attack, target, BlockShown::None);
}

BattlerSet hitInTheBack(const BattleState& state, const AttackInFlight& attack, BattlerSlot attacker) {
    BattlerSet hit;
    if (!attack.record.traits.has(AttackTrait::PHYSICAL)) {
        return hit;
    }
    const bool attackerFacesRight = state.facingRight.has(attacker);
    if (attacker.isCharacter() && state.battleType == BattleType::Side) {
        for (int position = 0; position < kMonsterSlots; ++position) {
            const BattlerSlot slot = BattlerSlot::monster(position);
            if (state.facingRight.has(slot) == attackerFacesRight) {
                hit.add(slot);
            }
        }
    }
    if (attacker.isMonster() && state.battleType == BattleType::Pincer) {
        for (int position = 0; position < kPartySlots; ++position) {
            const BattlerSlot slot = BattlerSlot::character(position);
            if (state.facingRight.has(slot) == attackerFacesRight) {
                hit.add(slot);
            }
        }
    }
    return hit;
}

void recordLastHit(BattleState& state, const AttackInFlight& attack, BattlerSlot attacker, BattlerSlot target) {
    markToRetaliate(state, attacker, target);
    LastHits& last = state.at(target).lastHit;
    last.command = Hit<BattleCommandId>{.by = attacker, .with = attack.queuedCommand};
    if (attack.scratch.lastSpell) {
        last.spell = Hit<AttackId>{.by = attacker, .with = *attack.scratch.lastSpell};
    }
    if (attack.scratch.lastItem) {
        last.item = Hit<ItemId>{.by = attacker, .with = *attack.scratch.lastItem};
    }
    last.element = Hit<ElementSet>{.by = attacker, .with = attack.record.element};
}

void resolveAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                   BattlerSlot attacker) {
    attack.scratch.missed = {};
    attack.scratch.hitInTheBack = {};
    attack.params = {};
    chooseTargets(state, attack, attacker);
    attack.scratch.targetCount = attack.targets.count();
    coverTarget(state, attack, attacker);
    initParams(attack, attacker);
    rememberTargets(attack);

    Battler& battler = state.at(attacker);
    if (attack.scratch.mpCost != 0) {
        const std::uint8_t cost = attack.scratch.mpCost;
        attack.scratch.mpCost = 0;
        if (battler.mp < cost) {
            if (attacker.isMonster()) {
                attack.command = kNothingShown;
            }
            script.addAnimation(animationOf(attack));
            attack.params.targets = {};
            attack.params.targetsHit = {};
            attack.record.misc.clear(AttackMiscFlag::SHOW_ATTACK_MESSAGE);
            script.add(BattleMessage{.message = kMessageNotEnoughMp});
            script.addParams(attack.params);
            return;
        }
        battler.mp = static_cast<std::uint16_t>(battler.mp - cost);
        battler.pendingUpdates.enabledMagic = true;
    }

    if (attack.scratch.nameKind == AttackNameKind::MonsterSpecialAttack) {
        applyMonsterSpecialAttack(state, attack, attacker);
    }
    if (attack.runicAbsorbing) {
        attack.runicAbsorbing = false;
        AttackAnimation absorbs = animationOf(attack);
        absorbs.what = SpecialAnimation::RunicAbsorbsSpell;
        script.add(absorbs);
        script.addParams(attack.params);
        attack.params.castingSwirl = false;
    }
    if (battler.status.has(StatusId::MORPH)) {
        attack.multiplier += kDoubled;
    }
    const bool physical = attack.record.traits.has(AttackTrait::PHYSICAL);
    if (physical && battler.status.has(StatusId::BERSERK)) {
        ++attack.multiplier;
    }
    if (!attack.record.traits.has(AttackTrait::NO_DAMAGE_SPLIT) && attack.scratch.targetCount >= 2) {
        attack.damage /= 2;
    }
    if (!attack.rowIgnored && battler.backRow) {
        attack.damage /= 2;
    }
    attack.scratch.hitInTheBack = hitInTheBack(state, attack, attacker);
    hooks.attackerEffect(state, attack, attacker);
    if (attack.scratch.extraHits) {
        return;
    }

    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot target = BattlerSlot::fromIndex(index);
        if (!attack.params.targetsHit.has(target)) {
            continue;
        }
        const HitResult result = checkHit(state, attack, attacker, target);
        if (result.block != BlockShown::None) {
            attack.params.blocks[static_cast<std::size_t>(target.position())] = result.block;
        }
        if (result.reflected) {
            attack.params.reflectedOff.add(target);
        }
        if (!result.hit) {
            attack.params.targetsHit.remove(target);
        }
    }
    if (attack.scratch.quietMisses && attack.params.targetsHit.empty()) {
        attack.command = kNothingShown;
    }
    if (attack.scratch.hideWithoutTargets && attack.params.targets.empty()) {
        attack.command = kNothingShown;
    }

    bool critical = false;
    if (battler.impCritical && battler.status.has(StatusId::IMP)) {
        critical = true;
    } else if (attack.automaticCritical) {
        critical = true;
    } else if (attack.canBeCritical && attack.rules.opposingSideOnly) {
        critical = state.rng.next() < kCriticalBelow && attack.scratch.targetCount != 0;
    }
    if (critical) {
        attack.multiplier += kDoubled;
        attack.params.criticalFlash = true;
    }

    script.addAnimation(animationOf(attack));
    hooks.scaleDamage(state, attack);
    if (attack.record.flags1.has(AttackFlag1::KILLS_ATTACKER)) {
        markToDie(state, attack, attacker);
    }
    if (battler.controlledBy) {
        hooks.controlMenu(state, *battler.controlledBy, attacker);
    }

    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot target = BattlerSlot::fromIndex(index);
        if (!attack.params.targetsHit.has(target)) {
            continue;
        }
        attack.params.targetsHit.remove(target);
        if (attack.scratch.hitInTheBack.has(target)) {
            ++attack.multiplier;
        }
        recordLastHit(state, attack, attacker, target);
        if (state.heldByZinger == target) {
            continue;
        }
        attack.scratch.missedByEffect = {};
        hooks.statusEffect(state, attack, attacker, target);
        if (hooks.targetEffect(state, attack, attacker, target)) {
            attack.scratch.missedByEffect.add(target);
        }
        if (!attack.scratch.missedByEffect.empty()) {
            continue;
        }
        attack.params.targetsHit.add(target);
        hooks.targetDamage(state, attack, attacker, target);
    }
    hooks.applyDamage(state, attack, script);
    learnLore(state, attack);
    if (attack.params.targetsHit.empty()) {
        attack.record.misc.clear(AttackMiscFlag::SHOW_ATTACK_MESSAGE);
    }
    script.addParams(attack.params);
}

namespace {

// A spell cast by a muted battler, or by an Imp who is not casting Imp, fizzles: the caster only
// goes back to standing.
bool fizzles(const AttackInFlight& attack, const Battler& caster) {
    if (attack.record.mpCost == 0) {
        return false;
    }
    if (caster.status.has(StatusId::SILENCE)) {
        return true;
    }
    return caster.status.has(StatusId::IMP) && attack.scratch.lastSpell != AttackId::IMP;
}

// One attack of the command, from loading the hand to the messages after it.
void attackOnce(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                BattlerSlot attacker) {
    attack.multiplier = attack.extraMultiplier;
    attack.scratch.golemBlocked.reset();
    attack.scratch.interceptorBlocked.reset();

    bool makesNoHit = false;
    if (!attack.scratch.followUpSpell && attack.scratch.fightCommand) {
        attack.command = *attack.scratch.fightCommand;
        initTargeting(attack, attack.command, attack.attack);
        const bool offHand = ((attack.scratch.attacksLeft + 1) & 1) != 0;
        loadWeaponAttack(state, attack, attacker, offHand);
        if (attack.record.power == 0) {
            makesNoHit = true;
        } else if (attack.command == BattleCommandId::CAPTURE) {
            attack.effect = SpecialEffect::of(CommandEffect::Steal);
        }
    }

    if (!makesNoHit) {
        startFollowUpSpell(state, attack, attacker);
        const bool firstPass = std::exchange(attack.firstPass, false);
        if (!firstPass || !attack.record.flags1.has(AttackFlag1::RETARGET_IF_INVALID)) {
            attack.rules.keepTargetsThatAreGone = true;
        }
        if (attack.targets.empty() && attack.emptyTargetsReaim) {
            attack.rules.keepTargetsThatAreGone = false;
        }
        if (attack.scratch.randomizeTargets) {
            attack.rules.randomTarget = true;
        }
        if (attack.hitsNoOne) {
            attack.rules.keepTargetsThatAreGone = true;
            attack.targets = {};
        }
        showAttackName(state, script, attack, attacker);
        const BattlerSlot maker = attack.scratch.sketcher.value_or(attacker);
        if (attack.queuedCommand == kRouletteCommand) {
            attack.targets = {};
            attack.rules = AimingRules{.keepTargetsThatAreGone = true};
        }

        if (fizzles(attack, state.at(maker))) {
            script.add(PoseReset{.battler = maker});
        } else {
            absorbWithRunic(state, attack);
            airAnchor(state, attack, maker);
            hooks.baseDamage(state, attack, maker);
            resolveAttack(state, hooks, script, attack, maker);
            if (!attack.record.traits.has(AttackTrait::PHYSICAL) && !attack.params.reflectedOff.empty()) {
                hooks.reflectedAttack(state, attack, maker, script);
            }
            attack.targets = attack.scratch.currentTargets;
            hooks.applyPendingStatuses(state);
            weaponSpellcast(state, attack);
            if (attack.scratch.message) {
                script.add(BattleMessage{.message = *attack.scratch.message});
                attack.scratch.message.reset();
            }
            if (attack.record.misc.has(AttackMiscFlag::SHOW_ATTACK_MESSAGE) && maker.isMonster()) {
                script.add(BattleMessage{.message = raw(attack.attack)});
            }
        }
    }

    attack.scratch.damageModification = true;
    attack.scratch.randomizeTargets = false;
    attack.scratch.quietMisses = false;
    if (attack.scratch.interceptorBlocked) {
        state.scratch.interceptorGuarding = attack.scratch.interceptorBlocked;
    }
}

}  // namespace

void runAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
               BattlerSlot attacker) {
    do {
        attackOnce(state, hooks, script, attack, attacker);
    } while (--attack.scratch.attacksLeft >= 0);
}

void runSelfAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                   BattlerSlot attacker) {
    attack.vanishApplies = false;
    attack.rules.mayTargetTheFallen = true;
    attack.rules.keepTargetsThatAreGone = true;
    attack.targets = BattlerSet::of(attacker);
    runAttack(state, hooks, script, attack, attacker);
}

}  // namespace ostinato::battle
