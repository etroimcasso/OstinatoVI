#include "battle/system_commands.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "battle/battle_start.h"
#include "battle/commands.h"
#include "battle/targeting.h"
#include "ostinato/attack_flags.h"
#include "ostinato/element.h"
#include "ostinato/status_id.h"

namespace ostinato::battle {

namespace {

// Damage over time is the battler's max HP times its stamina, over 1024, plus 2, at most 255.
constexpr int kDamageOverTimeShift = 10;
constexpr int kDamageOverTimeBase = 2;
constexpr int kStatMax = 255;

// Poison's multiplier grows by two steps a beat, to fourteen.
constexpr int kPoisonMultiplierStep = 2;
constexpr int kPoisonMultiplierMax = 14;

// Seize drains as a level-16 attack of power 16.
constexpr std::uint8_t kSeizeLevel = 16;
constexpr std::uint8_t kSeizePower = 16;

constexpr int kElementBits = 8;

// The plain hit a battle's own action makes: no command's rules, aimed at enemies by default.
void startOwnAttack(AttackInFlight& attack) {
    plainCommandAttack(attack, BattleCommandId::NONE);
}

}  // namespace

void damageOverTime(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                    BattlerSlot slot, DamageOverTimeKind kind) {
    if (state.enemyCount == 0) {
        return;
    }
    Battler& battler = state.at(slot);
    battler.dotActionPending = false;
    if (battler.heldByStatus) {
        return;
    }
    startOwnAttack(attack);
    attack.vanishApplies = false;
    attack.ignoreDefenseStopsMultiplier = false;
    attack.command = kNothingShown;
    attack.record.traits =
        AttackTraitSet::of(AttackTrait::INVERT_ON_UNDEAD, AttackTrait::IGNORE_DEFENSE, AttackTrait::NO_DAMAGE_SPLIT);
    if (kind == DamageOverTimeKind::Regen) {
        attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
    } else {
        attack.record.flags2.clear(AttackFlag2::RESTORE_HP_MP);
    }
    if (kind == DamageOverTimeKind::Poison) {
        attack.extraMultiplier = battler.poisonMultiplier;
        battler.poisonMultiplier = std::min(kPoisonMultiplierMax, battler.poisonMultiplier + kPoisonMultiplierStep);
        attack.record.element = ElementSet::of(Element::POISON);
    }
    const int power = (battler.maxHp * battler.stamina) >> kDamageOverTimeShift;
    attack.record.power = static_cast<std::uint8_t>(std::min(kStatMax, power + kDamageOverTimeBase));
    runSelfAttack(state, hooks, script, attack, slot);
}

void spellWithoutCaster(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                        AttackId spell, BattlerSlot target) {
    if (spell == AttackId::DOOM) {
        state.at(target).pendingUpdates.stopCondemned = true;
        if (state.enemyCount == 0) {
            return;
        }
    }
    attack.attack = spell;
    attack.command = BattleCommandId::MAGIC;
    initTargeting(attack, BattleCommandId::MAGIC, spell);
    initAttacker(state, attack, target);
    attack.castingSwirlSuppressed = false;
    attack.record.flags1 = AttackFlags1::of(AttackFlag1::IGNORE_REFLECT);
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    attack.record.mpCost = 0;
    runSelfAttack(state, hooks, script, attack, target);
}

void statusTimersExpire(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                        const StatusTimersExpired& expired) {
    startOwnAttack(attack);
    attack.command = kNothingShown;
    attack.castingSwirlSuppressed = false;
    if (expired.stop) {
        attack.record.statuses.set(StatusId::STOP);
    }
    if (expired.reflect) {
        attack.record.statuses.set(StatusId::REFLECT);
    }
    if (expired.freeze) {
        attack.record.statuses.set(StatusId::FROZEN);
    }
    if (expired.sleep && state.at(expired.battler).status.has(StatusId::SLEEP)) {
        attack.record.statuses.set(StatusId::SLEEP);
        attack.command = BattleCommandId::MAGIC;
        attack.attack = AttackId::TAPIR;
    }
    attack.record.flags2.set(AttackFlag2::REMOVE_STATUS);
    runSelfAttack(state, hooks, script, attack, expired.battler);
}

void partyEscapes(BattleState& state, ActionExecutor& executor, BattleScriptBuilder& script) {
    if (!state.partyRunning) {
        return;
    }
    if (state.cannotRun) {
        script.add(BattleMessage{.message = kMessageCantRunAway});
        return;
    }
    if (state.partyJustEscaped.empty()) {
        return;
    }
    const BattlerSet gotAway = std::exchange(state.partyJustEscaped, {});
    BattlerSet escaping;
    BattlerSlot last = BattlerSlot::character(0);
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (!gotAway.has(slot)) {
            continue;
        }
        Battler& battler = state.at(slot);
        if (!battler.gauge.isFull() || battler.heldByStatus || battler.heldByControl || !battler.present ||
            battler.status.has(StatusId::ZOMBIE) || battler.status.has(StatusId::HIDE)) {
            continue;
        }
        escaping.add(slot);
        state.partyLeft.add(slot);
        state.leavingAfterAction.add(slot);
        battler.emptyGaugeAfterAction = true;
        battler.pendingUpdates.removeAllActions = true;
        executor.releaseLinks(state, slot);
        last = slot;
    }
    if (escaping.empty()) {
        return;
    }
    script.addAnimation(AttackAnimation{.what = SpecialAnimation::PartyRunsAway});
    script.addParams(AttackAnimationParams{.attacker = last, .targets = escaping, .targetsHit = escaping});
}

void seizeDrain(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                BattlerSlot seizer, BattlerSlot seized) {
    state.at(seizer).dotActionPending = false;
    startOwnAttack(attack);
    attack.attackerStat = 0;
    attack.attackerLevel = kSeizeLevel;
    attack.record.power = kSeizePower;
    attack.record.traits = AttackTraitSet::of(AttackTrait::INVERT_ON_UNDEAD, AttackTrait::IGNORE_DEFENSE);
    attack.record.flags1 = AttackFlags1::of(AttackFlag1::IGNORE_REFLECT);
    attack.record.flags2.set(AttackFlag2::DRAIN);
    attack.scratch.aimExactly = true;
    attack.drainCapped = false;
    attack.command = kNothingShown;
    attack.targets = BattlerSet::of(seized);
    runAttack(state, hooks, script, attack, seizer);
}

void formationChange(BattleState& state, ActionExecutor& executor, BattleScriptBuilder& script,
                     const FormationChange& change, const TextCorpus& text, const MonsterSprites& sprites) {
    changeFormation(state, executor, change.formation,
                    change.restoreMonsters ? MonsterHp::FromRecord : MonsterHp::KeptFromSlot, text, sprites);
    script.add(FormationChanges{.scrollBackground = change.scrollBackground,
                                .exit = change.exit,
                                .monsters = state.alive.monstersOnly()});
}

void setMonsterVisibility(BattleState& state, BattleScriptBuilder& script, const MonsterVisibilityAction& action) {
    BattlerSet hidden;
    BattlerSet revived;
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::monster(position);
        Battler& battler = state.at(slot);
        if (!action.monsters.has(slot) || !battler.monster) {
            continue;
        }
        auto hide = [&] {
            state.countedInBattle.remove(slot);
            state.monstersStanding.remove(slot);
            hidden.add(slot);
            battler.status.set(StatusId::HIDE);
        };
        auto revive = [&] {
            state.monstersFallen.remove(slot);
            state.monstersStanding.add(slot);
            revived.add(slot);
            state.joiningAfterAction.add(slot);
            state.countedInBattle.add(slot);
            state.battleMayNotEnd = false;
        };
        switch (action.mode) {
        case MonsterVisibilityMode::ReviveAtFullHp:
            battler.hp = battler.maxHp;
            revive();
            break;
        case MonsterVisibilityMode::ReviveAtCurrentHp:
            revive();
            break;
        case MonsterVisibilityMode::Hide:
            state.leavingAfterAction.add(slot);
            hide();
            break;
        case MonsterVisibilityMode::HideButTargetable:
            hide();
            break;
        case MonsterVisibilityMode::HideAndKeepBattleGoing:
            state.battleMayNotEnd = true;
            hide();
            break;
        case MonsterVisibilityMode::HideAndKill:
            hide();
            battler.status.set(StatusId::DEAD);
            break;
        }
    }
    script.add(MonstersEnterOrLeave{.type = action.type, .hidden = hidden, .revived = revived});
}

void scan(BattleState& state, BattlerSlot target) {
    const Battler& battler = state.at(target);
    auto show = [&](BattleMessage message) {
        state.requests.push_back(RunBattleScript{.commands = {message}});
    };
    show(BattleMessage{.message = kMessageLevel, .firstNumber = battler.level});
    show(BattleMessage{.message = kMessageHp, .firstNumber = battler.hp, .secondNumber = battler.maxHp});
    if (battler.maxMp != 0) {
        show(BattleMessage{.message = kMessageMp, .firstNumber = battler.mp, .secondNumber = battler.maxMp});
    }
    const unsigned canceled = battler.elementsHalved.bits | battler.elementsAbsorbed.bits |
                               battler.elementsNullified.bits;
    const unsigned weak = battler.elementsWeak.bits & ~canceled;
    for (int bit = 0; bit < kElementBits; ++bit) {
        if ((weak & (1u << bit)) != 0) {
            show(BattleMessage{.message = static_cast<std::uint8_t>(kMessageWeakAgainst + bit)});
        }
    }
}

}  // namespace ostinato::battle
