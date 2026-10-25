#include "battle/actions.h"

#include <algorithm>
#include <utility>
#include <variant>

#include "data/attack_properties.h"
#include "data/battle_commands.h"
#include "data/esper_properties.h"

namespace ostinato::battle {

namespace {

constexpr int kLongestAdvanceWait = 254;
constexpr int kUnsetAdvanceWait = 255;

bool isEsperSummon(AttackId attack) {
    return attack >= AttackId::RAMUH && static_cast<std::size_t>(attack) - static_cast<std::size_t>(AttackId::RAMUH) <
                                            kEsperProperties.size();
}

bool costsMp(BattleCommandId command) {
    return command == BattleCommandId::MAGIC || command == BattleCommandId::X_MAGIC ||
           command == BattleCommandId::LORE || command == BattleCommandId::SUMMON;
}

// Every new action takes Mimic's free-of-charge mark, whether or not it is a spell.
void priceAction(BattleState& state, BattlerSlot slot, Action& action) {
    const bool mimicking = std::exchange(state.mimicking, false);
    if (auto* command = std::get_if<CommandAction>(&action)) {
        command->mpCost = mimicking ? 0 : commandMpCost(state, slot, *command);
    }
}

}  // namespace

std::uint8_t commandMpCost(const BattleState& state, BattlerSlot slot, const CommandAction& command) {
    if (!costsMp(command.command)) {
        return 0;
    }
    if (slot.isMonster()) {
        return getAttackProperties(command.attack).mpCost;
    }
    // A party member pays what the spell costs them; a summon costs what their esper's does. A
    // spell not in their list costs nothing.
    const SpellList& spells = state.at(slot).spells;
    if (isEsperSummon(command.attack)) {
        return spells.esper ? spells.summon.mpCost : 0;
    }
    const SpellListEntry* known = spells.find(command.attack);
    return known == nullptr ? 0 : known->mpCost;
}

void addAction(BattleState& state, BattlerSlot slot, Action action) {
    priceAction(state, slot, action);
    state.at(slot).actions.push_back(std::move(action));
}

void addCounterAction(BattleState& state, BattlerSlot slot, Action action) {
    priceAction(state, slot, action);
    Battler& battler = state.at(slot);
    if (battler.counterActions.empty()) {
        state.counterQueue.push(slot);
    }
    battler.counterActions.push_back(std::move(action));
}

void addActionOrCounter(BattleState& state, BattlerSlot slot, Action action) {
    if (state.counterattackRunning) {
        addCounterAction(state, slot, std::move(action));
    } else {
        addAction(state, slot, std::move(action));
    }
}

void addImmediateAction(BattleState& state, Action action) {
    state.mimicking = false;
    state.immediateActions.push_back(std::move(action));
}

void addAdvanceWait(BattleState& state, ActionExecutor& executor, BattlerSlot slot, CommandAction& command) {
    Battler& battler = state.at(slot);
    const int wait = battler.advanceWait.value_or(kUnsetAdvanceWait) + commandAdvanceWait(command.command);
    battler.advanceWait = static_cast<std::uint8_t>(std::min(wait, kLongestAdvanceWait));
    executor.aim(state, slot, command);
}

void addAutomaticAction(BattleState& state, ActionExecutor& executor, BattlerSlot slot, CommandAction command) {
    addAdvanceWait(state, executor, slot, command);
    addAction(state, slot, command);
}

void removeAction(BattleState& state, BattlerSlot slot) {
    auto& actions = state.at(slot).actions;
    if (!actions.empty()) {
        actions.pop_front();
    }
}

void removeCounterAction(BattleState& state, BattlerSlot slot) {
    auto& actions = state.at(slot).counterActions;
    if (!actions.empty()) {
        actions.pop_front();
    }
}

void removeAllActions(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    battler.actions.clear();
    state.advanceWaitQueue.erase(slot);
    if (battler.gauge.isFull()) {
        battler.gauge.setNearlyFull();
        battler.inBattleMenu = false;
        battler.atbStopped = false;
        battler.actionQueued = false;
    }
    if (slot.isCharacter()) {
        addImmediateAction(state, ResetBattlePose{.character = slot});
    }
}

void joinAdvanceWaitLine(BattleState& state, BattlerSlot slot) {
    state.advanceWaitQueue.push(slot);
    state.at(slot).skipAdvanceWait = false;
}

void openMenuOrJoinLine(BattleState& state, BattlerSlot slot) {
    if (slot.isMonster()) {
        joinAdvanceWaitLine(state, slot);
        return;
    }
    Battler& battler = state.at(slot);
    if (!battler.mayActAfterJump || state.menusDisabled) {
        return;
    }
    battler.inBattleMenu = true;
    const bool controlling = language(state.version) == Language::EN ? battler.triggers.usesControlMenu
                                                                      : battler.status.has(StatusId::CHANT);
    state.requests.push_back(OpenBattleMenu{.character = slot, .controllingMonster = controlling});
}

void startPartyEscape(BattleState& state) {
    if (!state.partyRunning || state.partyJustEscaped.empty() || state.colosseum) {
        return;
    }
    queuePartyEscape(state);
}

void queuePartyEscape(BattleState& state) {
    if (std::exchange(state.runActionQueued, true)) {
        return;
    }
    addImmediateAction(state, PartyEscape{});
}

void condemnedCountRunsOut(BattleState& state, BattlerSlot slot) {
    addImmediateAction(state, SpellWithoutCaster{.spell = AttackId::DOOM, .target = slot});
}

}  // namespace ostinato::battle
