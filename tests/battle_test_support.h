// Shared helpers for the battle core tests: a small battle built by hand, an executor that
// records what it was asked to do, and drivers for the two clocks.
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "battle/action_executor.h"
#include "battle/battle_core.h"
#include "battle/battle_state.h"

namespace ostinato::test {

// Two party members and two monsters, present and alive, speed 40, gauges empty.
inline battle::BattleState makeBattle() {
    battle::BattleState state;
    for (int position = 0; position < 2; ++position) {
        for (battle::BattlerSlot slot : {battle::BattlerSlot::character(position), battle::BattlerSlot::monster(position)}) {
            battle::Battler& battler = state.at(slot);
            battler.present = true;
            battler.hp = 100;
            battler.speed = 40;
            battler.gauge.empty();
            battle::recalcAtbRate(state, slot);
            state.alive.add(slot);
            if (slot.isMonster()) {
                state.monstersStanding.add(slot);
            }
        }
    }
    state.allyCount = 2;
    state.enemyCount = 2;
    state.partyPresentAtStart = state.alive.charactersOnly();
    return state;
}

// An executor that records every call and runs an optional hook for execute().
struct RecordingExecutor : battle::ActionExecutor {
    struct Executed {
        std::optional<battle::BattlerSlot> actor;
        battle::Action action;
    };
    std::vector<Executed> executed;
    std::vector<battle::BattlerSlot> aimed;
    std::vector<battle::BattlerSlot> automatic;
    std::vector<battle::BattlerSlot> ai;
    std::vector<battle::BattlerSlot> counterAi;
    std::function<void(battle::BattleState&, std::optional<battle::BattlerSlot>, const battle::Action&)> onExecute;
    std::function<void(battle::BattleState&, battle::BattlerSlot)> onAi;

    std::vector<battle::BattleScriptCommand> execute(battle::BattleState& state,
                                                     std::optional<battle::BattlerSlot> actor,
                                                     const battle::Action& action) override {
        executed.push_back({actor, action});
        if (onExecute) {
            onExecute(state, actor, action);
        }
        return {};
    }
    void aim(battle::BattleState&, battle::BattlerSlot actor, battle::CommandAction&) override {
        aimed.push_back(actor);
    }
    void chooseAutomaticAction(battle::BattleState&, battle::BattlerSlot actor) override {
        automatic.push_back(actor);
    }
    void runAi(battle::BattleState& state, battle::BattlerSlot actor) override {
        ai.push_back(actor);
        if (onAi) {
            onAi(state, actor);
        }
    }
    void runCounterAi(battle::BattleState&, battle::BattlerSlot actor) override {
        counterAi.push_back(actor);
    }
};

// Run exactly one time update: enough frames for the two-frame gate to open.
inline void runTimeUpdate(battle::BattleState& state, battle::ActionExecutor& executor) {
    state.framesSinceTimeUpdate = 1;
    battle::tickBattleTime(state, true, executor);
}

// Run the time update whose timer job belongs to `slot`.
inline void runTimerJobFor(battle::BattleState& state, battle::ActionExecutor& executor, battle::BattlerSlot slot) {
    state.counterPhase = static_cast<std::uint8_t>(slot.index());
    runTimeUpdate(state, executor);
}

// Step the loop as the host would: once, and again for each battle script it waits on, until it
// is back at the frame boundary. Returns how many scripts it waited on.
inline int stepUntilIdle(battle::BattleState& state, battle::ActionExecutor& executor) {
    int scripts = 0;
    battle::stepBattle(state, executor);
    while (state.waitingForScript && !state.outcome) {
        ++scripts;
        battle::stepBattle(state, executor);
    }
    return scripts;
}

}  // namespace ostinato::test
