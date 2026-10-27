// Shared helpers for the battle core tests: a small battle built by hand, an executor that
// records what it was asked to do, and drivers for the two clocks.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "battle/action_executor.h"
#include "battle/attack.h"
#include "battle/battle_core.h"
#include "battle/battle_rng.h"
#include "battle/battle_state.h"
#include "data/monster_sprites.h"
#include "data/rng_table.h"
#include "data/text_corpus.h"
#include "ostinato/text_class.h"

namespace ostinato::test {

// A text corpus whose monster names are all blank, for battles that load monsters without the
// cartridge.
inline const TextCorpus& blankMonsterNames() {
    static const TextCorpus corpus = [] {
        std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
        buffers[static_cast<std::size_t>(TextClass::MONSTER_NAME)].assign(3840, 0xFF);
        return TextCorpus{std::move(buffers)};
    }();
    return corpus;
}

// No cartridge sprites: every monster but the Ghost Train is placed by its formation position alone.
inline const MonsterSprites& noMonsterSprites() {
    static const MonsterSprites sprites;
    return sprites;
}

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

// makeBattle(), with every present battler on the field to be aimed at.
inline battle::BattleState makeAimableBattle() {
    battle::BattleState state = makeBattle();
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        if (state.at(slot).present) {
            state.targetableOrPresent.add(slot);
        }
    }
    return state;
}

// Put the battle's random numbers where each of the next draws satisfies its test, in order.
inline void rngWhere(battle::BattleState& state, std::initializer_list<std::function<bool(std::uint8_t)>> draws) {
    for (int position = 0; position < 256; ++position) {
        bool fits = true;
        int i = 1;
        for (const auto& draw : draws) {
            if (!draw(rngByte(static_cast<std::uint8_t>(position + i)))) {
                fits = false;
                break;
            }
            ++i;
        }
        if (fits) {
            state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
            return;
        }
    }
    ADD_FAILURE() << "no run of the random table fits";
}

// Draw tests for rngWhere().
inline bool heads(std::uint8_t r) { return (r & 1) != 0; }
inline bool tails(std::uint8_t r) { return (r & 1) == 0; }
inline std::function<bool(std::uint8_t)> below(int n) {
    return [n](std::uint8_t r) { return r < n; };
}
inline std::function<bool(std::uint8_t)> atLeast(int n) {
    return [n](std::uint8_t r) { return r >= n; };
}
inline bool any(std::uint8_t) { return true; }

// Attack hooks that record what they were called with.
struct RecordingHooks : battle::AttackHooks {
    std::vector<battle::BattlerSlot> damaged;
    std::vector<battle::BattlerSlot> effects;
    std::vector<int> multipliers;            // attack.multiplier at each scaleDamage()
    std::vector<int> damageAtScale;          // attack.damage at each scaleDamage()
    std::vector<battle::AimingRules> rulesAtBaseDamage;
    std::vector<battle::SpecialEffect> effectsAtBaseDamage;
    int baseDamageValue = 0;
    std::function<bool(battle::BattlerSlot)> missesEffect;

    void baseDamage(battle::BattleState&, battle::AttackInFlight& attack, battle::BattlerSlot) override {
        rulesAtBaseDamage.push_back(attack.rules);
        effectsAtBaseDamage.push_back(attack.effect);
        attack.damage = baseDamageValue;
    }
    void scaleDamage(battle::BattleState&, battle::AttackInFlight& attack) override {
        multipliers.push_back(attack.multiplier);
        damageAtScale.push_back(attack.damage);
    }
    bool targetEffect(battle::BattleState&, battle::AttackInFlight&, battle::BattlerSlot,
                      battle::BattlerSlot target) override {
        effects.push_back(target);
        return missesEffect && missesEffect(target);
    }
    void targetDamage(battle::BattleState&, battle::AttackInFlight& attack, battle::BattlerSlot,
                      battle::BattlerSlot target) override {
        damaged.push_back(target);
        multipliersAtTargetDamage.push_back(attack.multiplier);
    }
    std::vector<int> multipliersAtTargetDamage;
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
