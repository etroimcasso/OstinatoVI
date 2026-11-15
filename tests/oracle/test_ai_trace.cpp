// The AI's routines followed by the damage trace (tools/damage-trace/src/oracle/damage_oracle.h): the
// cartridge's own calls of InitAI, ExecMonsterAction, ExecAIRetal and the three commands, traced as the
// oracle makes them and replayed against the port, and two recordings of the first fight in Narshe
// replayed the same way.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "ai_battle.h"
#include "ai_trace_cases.h"
#include "data/ai_scripts.h"
#include "oracle/damage_oracle.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::AiScenario;
using trace::TracedCall;

// ─── the cartridge's own calls, traced and replayed ─────────────────────────────────────────────

std::vector<TracedCall> parse(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    std::istringstream in{text};
    return trace::parseTrace(in);
}

// What a replay compared, so a replay that compares nothing shows.
struct Replayed {
    int calls = 0;
    int differing = 0;
    std::string first;

    void replay(trace::DamageOracle& oracle, const std::vector<std::string>& lines, const std::string& what,
                const char* routine) {
        for (const TracedCall& call : parse(lines)) {
            if (call.routine != routine) {
                continue;
            }
            ++calls;
            const std::string difference = oracle.replay(call);
            if (!difference.empty() && ++differing == 1) {
                first = what + ":\n" + difference;
                char text[32];
                for (const auto& [cell, value] : call.inputs) {
                    std::snprintf(text, sizeof text, " r%04X=%02X", cell & 0xFFFF, value);
                    first += text;
                }
                first += "\n";
                for (const auto& [cell, value] : call.writes) {
                    std::snprintf(text, sizeof text, " w%04X=%02X", cell & 0xFFFF, value);
                    first += text;
                }
            }
        }
    }
    std::string report() const {
        return std::to_string(differing) + " of " + std::to_string(calls) + " calls differ; the first:\n" + first;
    }
};

TEST(AiTraceReplay, EveryScriptReadiedReplaysAsTheCartridgeReadiedIt) {
    OSTINATO_SKIP_WHEN_BRIEF();
    Replayed replayed;
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            AiScenario s = aiBattle();
            s.state.at(kEmptyMonsterSlot) = battle::Battler{};
            s.state.at(kEmptyMonsterSlot).aiScript = script;
            oracle.trace(true);
            (void)oracle.readyAi(s, kEmptyMonsterSlot);
            oracle.trace(false);
            replayed.replay(oracle, oracle.takeTrace(), "script " + std::to_string(script), "InitAI");
        }
    });
    EXPECT_EQ(replayed.differing, 0) << replayed.report();
    EXPECT_EQ(replayed.calls, kAiScriptCount);
}

TEST(AiTraceReplay, EveryScriptsTurnsReplayAsTheCartridgeRanThem) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    Replayed replayed;
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            std::vector<battle::BattlerSlot> actors{battle::BattlerSlot::monster(script % battle::kMonsterSlots)};
            if (script >= kCharacterAiScripts) {
                actors.push_back(battle::BattlerSlot::character(script % battle::kPartySlots));
            }
            for (const battle::BattlerSlot actor : actors) {
                const AiScenario s = trace::drawAiRunScenario(rng, script, actor);
                oracle.trace(true);
                (void)oracle.aiTurn(s);
                (void)oracle.nextAiTurn(actor);
                (void)oracle.nextAiTurn(actor);
                oracle.trace(false);
                replayed.replay(oracle, oracle.takeTrace(),
                                "script " + std::to_string(script) + " on battler " + std::to_string(actor.index()) +
                                    " (" + trace::describe(s) + ")",
                                "ExecMonsterAction");
            }
        }
    });
    EXPECT_EQ(replayed.differing, 0) << replayed.report();
    EXPECT_EQ(replayed.calls, 3 * (384 + 128));
}

TEST(AiTraceReplay, EveryCounterattackPartOnEveryPathReplaysAsTheCartridgeRanIt) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    Replayed replayed;
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            if (!aiScript(script).hasCounter()) {
                continue;
            }
            for (int path = 0; path < trace::kCounterGuards; ++path) {
                const AiScenario s = trace::drawAiRunScenario(
                    rng, script, battle::BattlerSlot::monster(script % battle::kMonsterSlots),
                    static_cast<trace::CounterGuard>(path));
                oracle.trace(true);
                (void)oracle.aiCounter(s);
                oracle.trace(false);
                replayed.replay(oracle, oracle.takeTrace(),
                                "script " + std::to_string(script) + " on path " + std::to_string(path) + " (" +
                                    trace::describe(s) + ")",
                                "ExecAIRetal");
            }
        }
    });
    EXPECT_EQ(replayed.differing, 0) << replayed.report();
    EXPECT_EQ(replayed.calls, 210 * trace::kCounterGuards);
}

TEST(AiTraceReplay, TheThreeCommandsReplayAsTheCartridgeRanThem) {
    OSTINATO_SKIP_WHEN_BRIEF();
    // SetStatus and ClearStatus make an attack: the trace follows them through the attack's own
    // calls, and the effect's call itself is not among the ones a trace holds.
    std::mt19937 rng{trace::kSweepSeed};
    Replayed replayed;
    int attacks = 0;
    int commands = 0;
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (int i = 0; i < 500; ++i) {
            const trace::AiCommandCase c = trace::drawAiCommandCase(rng);
            const auto* effect = std::get_if<battle::AiEffectAction>(&c.action);
            const bool attack =
                effect != nullptr && (effect->effect == AiEffect::SetStatus || effect->effect == AiEffect::ClearStatus);
            oracle.trace(true);
            (void)oracle.aiCommand(c.scenario, c.action);
            oracle.trace(false);
            const std::vector<std::string> lines = oracle.takeTrace();
            int held = 0;
            for (const TracedCall& call : parse(lines)) {
                held += call.routine.starts_with("Cmd_") ? 1 : 0;
            }
            if (attack) {
                ++attacks;
                EXPECT_EQ(held, 0) << trace::listedActionText(c.action);
                continue;
            }
            ++commands;
            const std::string what = trace::listedActionText(c.action) + " by battler " +
                                     std::to_string(c.scenario.actor.index());
            for (const char* routine : {"Cmd_2e", "Cmd_2f", "Cmd_30"}) {
                replayed.replay(oracle, lines, what, routine);
            }
        }
    });
    EXPECT_EQ(replayed.differing, 0) << replayed.report();
    EXPECT_EQ(replayed.calls, commands);
    EXPECT_GT(attacks, 0);
}

// ─── recordings ─────────────────────────────────────────────────────────────────────────────────

// The calls of a recording under tests/fixtures/damage-trace/; a recording that cannot be read fails.
std::vector<TracedCall> recording(const char* name) {
    const std::filesystem::path path = std::filesystem::path{OSTINATO_DAMAGE_TRACE_RECORDINGS} / name;
    std::ifstream in{path};
    if (!in) {
        throw std::runtime_error("cannot read the recording " + path.string());
    }
    return trace::parseTrace(in);
}

std::vector<TracedCall> callsOf(const std::vector<TracedCall>& calls, const char* routine) {
    std::vector<TracedCall> of;
    std::copy_if(calls.begin(), calls.end(), std::back_inserter(of),
                 [&](const TracedCall& call) { return call.routine == routine; });
    return of;
}

// Where a counterattack line ends: a call that queues a counterattack writes it.
constexpr std::uint32_t kCounterLineEnd = 0x7E3A69;

// Every call of the AI's routines in `calls` replays with no difference.
void expectAiCallsReplay(const std::vector<TracedCall>& calls, const char* name) {
    withStartedOracle(aiCartridge(), [&](trace::DamageOracle& oracle) {
        for (const TracedCall& call : calls) {
            if (call.routine == "InitAI" || call.routine == "ExecMonsterAction" || call.routine == "ExecAIRetal" ||
                call.routine.starts_with("Cmd_")) {
                EXPECT_EQ(oracle.replay(call), "") << name << " frame " << call.frame << ": " << call.routine;
            }
        }
    });
}

TEST(AiRecordings, TheGuardsTakeTheirTurnsAndOneAnswersAnAttackAsTheCartridgeDid) {
    OSTINATO_SKIP_WHEN_BRIEF();
    // guards-counter.log: the first fight from first-fight.inputs's state, played with
    // hold-then-press-a.inputs, with Wild Cat's turn part written into one guard's counterattack place
    // (tools/damage-trace/README.md).
    const std::vector<TracedCall> calls = recording("guards-counter.log");
    EXPECT_EQ(callsOf(calls, "InitAI").size(), 2u);
    EXPECT_GE(callsOf(calls, "ExecMonsterAction").size(), 3u);
    const std::vector<TracedCall> counters = callsOf(calls, "ExecAIRetal");
    ASSERT_EQ(counters.size(), 1u);
    EXPECT_TRUE(counters[0].writes.contains(kCounterLineEnd));
    expectAiCallsReplay(calls, "guards-counter.log");
}

TEST(AiRecordings, APetrifiedGuardAnswersItsOwnDeathAsTheCartridgeDid) {
    OSTINATO_SKIP_WHEN_BRIEF();
    // petrified-guard.log: the same fight with Retainer's counterattack part (if it is dead, its
    // Special) written into one guard's counterattack place and Petrify into its statuses to set.
    // The guard answers once with its Special; a second call for it in the same step runs nothing.
    const std::vector<TracedCall> calls = recording("petrified-guard.log");
    const std::vector<TracedCall> counters = callsOf(calls, "ExecAIRetal");
    ASSERT_EQ(counters.size(), 2u);
    EXPECT_TRUE(counters[0].writes.contains(kCounterLineEnd));
    EXPECT_FALSE(counters[1].writes.contains(kCounterLineEnd));
    expectAiCallsReplay(calls, "petrified-guard.log");
}

}  // namespace
}  // namespace ostinato::test
