// Every monster AI script run against the cartridge's own routines (battle/ai.h,
// battle/system_commands.h, docs/contracts/battle_ai.md), run on the hosted cartridge over the same
// battle (tools/damage-trace/src/oracle/damage_oracle.h): every script readied into an empty slot,
// every script's turns and every counterattack part on every path through its guard over drawn
// battles, and the three commands only a script queues over drawn cases.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include "ai_battle.h"
#include "battle/ai.h"
#include "battle/battle_state.h"
#include "data/ai_scripts.h"
#include "oracle/damage_oracle.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::AiRunResult;
using trace::AiScenario;
using trace::AiScriptPlaces;

// The scripts a scripted battle gives a party member start here.
constexpr std::uint16_t kCharacterAiScripts = 256;

TEST(AiRunOracleTest, ReadyingEveryScriptIntoAnEmptySlot) {
    OSTINATO_SKIP_WHEN_BRIEF();
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            AiScenario s = aiBattle();
            s.state.at(kEmptyMonsterSlot) = battle::Battler{};
            s.state.at(kEmptyMonsterSlot).aiScript = script;
            const AiScriptPlaces guest = oracle.readyAi(s, kEmptyMonsterSlot);
            battle::readyAi(s.state, kEmptyMonsterSlot);
            ASSERT_EQ(trace::differences(trace::portScriptPlaces(s.state, kEmptyMonsterSlot), guest), "")
                << "script " << script;
        }
    });
}

// Where each step stands in the cartridge's script block, for a report.
std::string differenceIn(const std::string& differences, std::uint16_t script, int turn, const AiScenario& s) {
    return "script " + std::to_string(script) + " on battler " + std::to_string(s.actor.index()) + ", turn " +
           std::to_string(turn + 1) + " (" + trace::describe(s) + "):\n" + differences;
}

void emptyLists(battle::BattleState& state, battle::BattlerSlot actor) {
    state.at(actor).actions.clear();
    state.at(actor).counterActions.clear();
    state.counterQueue = {};
}

// What a sweep compared, so a sweep that compares nothing shows.
struct SweepCounts {
    int runs = 0;
    int differing = 0;
    int actions = 0;
    int resumed = 0;  // runs that stopped at a wait
    int draws = 0;    // random numbers drawn
    std::string first;

    void note(const std::string& difference) {
        ++runs;
        if (!difference.empty() && ++differing == 1) {
            first = difference;
        }
    }
    void count(const AiRunResult& result, std::uint8_t rngBefore) {
        actions += static_cast<int>(result.actions.size() + result.counterActions.size());
        if (result.script.turnSkipped != battle::kNoResume || result.script.counterSkipped != battle::kNoResume) {
            ++resumed;
        }
        draws += static_cast<std::uint8_t>(result.rngPosition - rngBefore);
    }
    std::string report() const {
        return std::to_string(differing) + " of " + std::to_string(runs) + " runs differ; the first:\n" + first;
    }
};

// Six turns of `script` for `actor`, port and cartridge each going on from its own last turn.
// Returns the first difference, or empty.
std::string sixTurns(trace::DamageOracle& oracle, const AiScenario& s, std::uint16_t script, SweepCounts& counts) {
    battle::BattleState port = s.state;
    for (int turn = 0; turn < 6; ++turn) {
        const std::uint8_t rngBefore = port.rng.position();
        const AiRunResult guest = turn == 0 ? oracle.aiTurn(s) : oracle.nextAiTurn(s.actor);
        const AiRunResult ours = trace::portAiTurn(port, s.actor);
        const std::string differences = trace::differences(ours, guest);
        if (!differences.empty()) {
            return differenceIn(differences, script, turn, s);
        }
        counts.count(guest, rngBefore);
        emptyLists(port, s.actor);
    }
    return "";
}

TEST(AiRunOracleTest, EveryScriptForSixTurnsMatchesTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    SweepCounts counts;
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            std::vector<battle::BattlerSlot> actors{battle::BattlerSlot::monster(script % battle::kMonsterSlots)};
            if (script >= kCharacterAiScripts) {
                actors.push_back(battle::BattlerSlot::character(script % battle::kPartySlots));
            }
            for (const battle::BattlerSlot actor : actors) {
                const AiScenario s = trace::drawAiRunScenario(rng, script, actor);
                counts.note(sixTurns(oracle, s, script, counts));
            }
        }
    });
    EXPECT_EQ(counts.differing, 0) << counts.report();
    // Every script on a monster slot, and the ones a scripted battle gives a party member on a party
    // slot too; the sweep queues actions, stops at waits and draws.
    EXPECT_EQ(counts.runs, 384 + 128);
    EXPECT_GE(counts.actions, 3000);
    EXPECT_GE(counts.resumed, 500);
    EXPECT_GE(counts.draws, 3000);
}

TEST(AiRunOracleTest, EveryCounterattackPartOnEveryPathThroughItsGuardMatchesTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    SweepCounts counts;
    int restricted = 0;
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            if (!aiScript(script).hasCounter()) {
                continue;
            }
            for (int path = 0; path < trace::kCounterGuards; ++path) {
                const auto guard = static_cast<trace::CounterGuard>(path);
                const AiScenario s =
                    trace::drawAiRunScenario(rng, script, battle::BattlerSlot::monster(script % battle::kMonsterSlots), guard);
                const AiRunResult guest = oracle.aiCounter(s);
                const AiRunResult port = trace::portAiCounter(s);
                const std::string difference = trace::differences(port, guest);
                counts.note(difference.empty() ? "" : differenceIn(difference, script, path, s));
                counts.count(guest, s.state.rng.position());
                if (guard == trace::CounterGuard::DiedUnattacked || guard == trace::CounterGuard::QuickForAnother ||
                    guard == trace::CounterGuard::HeldByAStatus || guard == trace::CounterGuard::Charmed) {
                    ++restricted;
                }
            }
        }
    });
    EXPECT_EQ(counts.differing, 0) << counts.report();
    EXPECT_EQ(counts.runs, 210 * trace::kCounterGuards);
    EXPECT_EQ(restricted, 210 * 4);
    EXPECT_GE(counts.actions, 300);
}

TEST(AiRunOracleTest, TheThreeCommandsOverFiveHundredDrawnCasesMatchTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    int differing = 0;
    int statusesSet = 0;
    int statusesCleared = 0;
    std::string first;
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (int i = 0; i < 500; ++i) {
            const trace::AiCommandCase c = trace::drawAiCommandCase(rng);
            if (const auto* effect = std::get_if<battle::AiEffectAction>(&c.action)) {
                statusesSet += effect->effect == AiEffect::SetStatus ? 1 : 0;
                statusesCleared += effect->effect == AiEffect::ClearStatus ? 1 : 0;
            }
            const std::string difference =
                trace::differences(trace::portAiCommand(c.scenario, c.action), oracle.aiCommand(c.scenario, c.action));
            if (!difference.empty() && ++differing == 1) {
                first = trace::listedActionText(c.action) + " by battler " + std::to_string(c.scenario.actor.index()) +
                        ":\n" + difference;
            }
        }
    });
    EXPECT_EQ(differing, 0) << differing << " of 500 differ; the first:\n" << first;
    EXPECT_GT(statusesSet, 0);
    EXPECT_GT(statusesCleared, 0);
}

}  // namespace
}  // namespace ostinato::test
