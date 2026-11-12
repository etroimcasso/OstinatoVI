#include "battle/system_commands.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "battle/ai_targets.h"
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

std::uint8_t& variableOf(BattleState& state, BattlerSlot actor, AiVariable variable) {
    return variable.isOwn() ? state.at(actor).aiVariable : state.battleVariables[variable.index()];
}

// --- the effects a script asks for ------------------------------------------------------------

// What an effect works on.
struct AiEffectRun {
    BattleState& state;
    AttackHooks& hooks;
    BattleScriptBuilder& script;
    AttackInFlight& attack;
    BattlerSlot actor;
    const AiEffectAction& action;

    BattlerSet targets() { return aiTargets(state, actor, action.target, false); }
};

// The fill a gauge's rate takes for the effect that fills it: its high byte all set, so the next
// time update fills the gauge.
constexpr std::uint16_t kGaugeFillsNextUpdate = 0xFF00;
constexpr std::uint16_t kRateFraction = 0x00FF;

void resetOwnTimer(AiEffectRun& run) {
    run.state.at(run.actor).turnTimer = 0;
}

void makeInvincible(AiEffectRun& run) {
    run.state.invincible = run.state.invincible | run.targets();
}

void endBattle(AiEffectRun& run) {
    run.state.endOfBattleEvent = EndOfBattleEvent::EndNow;
}

// Gau is recruited, and the battler's character record joins the party being played at its slot.
void recruitGau(AiEffectRun& run) {
    run.state.gauRecruited = true;
    const Battler& battler = run.state.at(run.actor);
    if (!battler.record) {
        return;
    }
    game::PartyMembership& membership = run.state.party.membership[*battler.record];
    membership.party = run.state.party.currentParty;
    membership.slot = static_cast<std::uint8_t>(run.actor.position());
}

void resetBattleTimer(AiEffectRun& run) {
    run.state.battleCounter = 0;
}

void endInvincible(AiEffectRun& run) {
    run.state.invincible = run.state.invincible.without(run.targets());
}

void makeTargetable(AiEffectRun& run) {
    run.state.aiUntargetable = run.state.aiUntargetable.without(run.targets().monstersOnly());
}

void makeUntargetable(AiEffectRun& run) {
    run.state.aiUntargetable = run.state.aiUntargetable | run.targets().monstersOnly();
}

// The target only decides whether anything happens: it is the battler's own gauge that fills.
void fillGauge(AiEffectRun& run) {
    if (run.targets().empty()) {
        return;
    }
    Battler& battler = run.state.at(run.actor);
    battler.atbRate = static_cast<std::uint16_t>((battler.atbRate & kRateFraction) | kGaugeFillsNextUpdate);
}

void endBattleLearningRages(AiEffectRun& run) {
    run.state.endOfBattleEvent = EndOfBattleEvent::RagesLearned;
}

void nothing(AiEffectRun&) {}

// An attack by the battler on itself that puts the status on, or with the remove-status flag takes
// it off, showing nothing.
void ownStatusAttack(AiEffectRun& run, bool remove) {
    startOwnAttack(run.attack);
    run.attack.record.statuses.set(run.action.status);
    run.attack.command = kNothingShown;
    if (remove) {
        run.attack.record.flags2.set(AttackFlag2::REMOVE_STATUS);
    }
    runSelfAttack(run.state, run.hooks, run.script, run.attack, run.actor);
}

void setStatus(AiEffectRun& run) {
    ownStatusAttack(run, false);
}

void clearStatus(AiEffectRun& run) {
    ownStatusAttack(run, true);
}

// Hide goes onto the battler's statuses directly, not through the statuses taking effect.
void hideSelf(AiEffectRun& run) {
    run.state.at(run.actor).status.set(StatusId::HIDE);
}

struct AiEffectRule {
    AiEffect effect;
    void (*run)(AiEffectRun&);
};

// One row per effect, in the order the game numbers them.
constexpr std::array<AiEffectRule, 14> kAiEffectRules{{
    {AiEffect::ResetOwnTimer, resetOwnTimer},
    {AiEffect::MakeInvincible, makeInvincible},
    {AiEffect::EndBattle, endBattle},
    {AiEffect::RecruitGau, recruitGau},
    {AiEffect::ResetBattleTimer, resetBattleTimer},
    {AiEffect::EndInvincible, endInvincible},
    {AiEffect::MakeTargetable, makeTargetable},
    {AiEffect::MakeUntargetable, makeUntargetable},
    {AiEffect::FillGauge, fillGauge},
    {AiEffect::EndBattleLearningRages, endBattleLearningRages},
    {AiEffect::Nothing, nothing},
    {AiEffect::SetStatus, setStatus},
    {AiEffect::ClearStatus, clearStatus},
    {AiEffect::HideSelf, hideSelf},
}};

constexpr bool rulesInOrder() {
    for (std::size_t i = 0; i < kAiEffectRules.size(); ++i) {
        if (static_cast<std::size_t>(kAiEffectRules[i].effect) != i) {
            return false;
        }
    }
    return true;
}
static_assert(rulesInOrder(), "kAiEffectRules holds one row per effect, in the game's order");

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

void changeBattleVariable(BattleState& state, BattlerSlot actor, const BattleVariableAction& action) {
    std::uint8_t& variable = variableOf(state, actor, action.variable);
    switch (action.change) {
    case AiVariableChange::Set:
        variable = action.amount;
        break;
    case AiVariableChange::Add:
        variable = static_cast<std::uint8_t>(variable + action.amount);
        break;
    case AiVariableChange::Subtract:
        variable = static_cast<std::uint8_t>(variable - action.amount);
        break;
    }
}

void changeBattleSwitch(BattleState& state, BattlerSlot actor, const BattleSwitchAction& action) {
    const AiSwitch which = action.which;
    if (which.isBattleSwitch()) {
        const int number = which.number();
        switch (action.change) {
        case AiSwitchChange::Toggle:
            state.switches.set(number, !state.switches.isSet(number));
            break;
        case AiSwitchChange::Set:
            state.switches.set(number, true);
            break;
        case AiSwitchChange::Clear:
            state.switches.set(number, false);
            break;
        }
        return;
    }
    std::uint8_t& variable = variableOf(state, actor, which.variable());
    const auto bit = static_cast<std::uint8_t>(1u << which.bit());
    switch (action.change) {
    case AiSwitchChange::Toggle:
        variable = static_cast<std::uint8_t>(variable ^ bit);
        break;
    case AiSwitchChange::Set:
        variable = static_cast<std::uint8_t>(variable | bit);
        break;
    case AiSwitchChange::Clear:
        variable = static_cast<std::uint8_t>(variable & ~bit);
        break;
    }
}

void runAiEffect(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor, const AiEffectAction& action) {
    AiEffectRun run{.state = state, .hooks = hooks, .script = script, .attack = attack, .actor = actor, .action = action};
    kAiEffectRules[static_cast<std::size_t>(action.effect)].run(run);
}

}  // namespace ostinato::battle
