// A battler's AI script wired into the battle (battle/command_executor.h, battle/battle_start.h,
// docs/contracts/battle_ai.md § A): CommandExecutor's turn, counterattack and setup run the script, a
// scripted battle's party member has its script readied as it is seated; and the AI's routines followed
// by the damage trace (tools/damage-trace/damage_oracle): the cartridge's own calls, traced and replayed
// against the port.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ai_battle.h"
#include "battle/ai.h"
#include "battle/automatic_actions.h"
#include "battle/battle_hooks.h"
#include "battle/battle_start.h"
#include "battle/command_executor.h"
#include "damage_oracle.h"
#include "data/ai_scripts.h"
#include "data/formations.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "ostinato/char_ai_id.h"
#include "ostinato/status_id.h"
#include "ostinato/text_class.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using trace::AiRunResult;
using trace::AiScenario;
using trace::TracedCall;

// The scripts a scripted battle gives a party member start here.
constexpr std::uint16_t kCharacterAiScripts = 256;
// The Veldt's setup gives Gau the 115th of them (party_setup's table, char_ai.asm).
constexpr std::uint16_t kGauVeldtScript = kCharacterAiScripts + 114;

// A monster-name table of 0xFF bytes and no other text: what loading a monster reads.
const TextCorpus& blankText() {
    static const TextCorpus corpus = [] {
        std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
        buffers[static_cast<std::size_t>(TextClass::MONSTER_NAME)].assign(3840, 0xFF);
        return TextCorpus{std::move(buffers)};
    }();
    return corpus;
}

const MonsterSprites& noSprites() {
    static const MonsterSprites sprites;
    return sprites;
}

// The actions a battler's lists hold, as the cartridge's lists would.
std::vector<std::string> listed(const std::deque<battle::Action>& actions) {
    std::vector<std::string> out;
    for (const battle::Action& action : actions) {
        out.push_back(trace::listedActionText(action));
    }
    return out;
}

AiScenario withScript(AiScenario s, battle::BattlerSlot slot, std::uint16_t script) {
    s.state.at(slot).aiScript = script;
    battle::readyAi(s.state, slot);
    return s;
}

// ─── the seams ──────────────────────────────────────────────────────────────────────────────────

TEST(AiSeams, AMonstersTurnReadsItsScript) {
    // ExecAction, :211-224: a monster that chooses for itself runs its script's turn part.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    const AiScenario s = withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::LOBO));
    battle::BattleState wired = s.state;
    battle::BattleState direct = s.state;
    executor.runAi(wired, kSelf);
    battle::runAiTurn(direct, executor, kSelf);
    ASSERT_FALSE(wired.at(kSelf).actions.empty());
    EXPECT_EQ(listed(wired.at(kSelf).actions), listed(direct.at(kSelf).actions));
    EXPECT_EQ(wired.rng.position(), direct.rng.position());
}

TEST(AiSeams, AConfusedMonsterPicksItsOwnAttackAndLeavesItsScriptAlone) {
    // :214-222: charmed, berserk, confused or in the colosseum, a monster uses one of its own
    // attacks (RandMonsterAction) instead of reading its script.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    AiScenario s = withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::LOBO));
    s.state.at(kSelf).status.set(StatusId::CONFUSE);
    battle::BattleState wired = s.state;
    battle::BattleState own = s.state;
    executor.runAi(wired, kSelf);
    battle::chooseMonsterAction(own, executor, kSelf);
    EXPECT_EQ(listed(wired.at(kSelf).actions), listed(own.at(kSelf).actions));
    EXPECT_EQ(wired.at(kSelf).turnResume.blocksSkipped, s.state.at(kSelf).turnResume.blocksSkipped);
    EXPECT_EQ(wired.rng.position(), own.rng.position());
}

TEST(AiSeams, AScriptedPartyMemberReadsItsScriptWhateverItsStatus) {
    // QueueAction, :509-512: a party member with a script runs it when its gauge fills; Confuse,
    // Berserk and Charm do not divert it.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    AiScenario s = withScript(aiBattle(), kTerra, kGauVeldtScript);
    s.state.at(kTerra).status.set(StatusId::CONFUSE);
    battle::BattleState wired = s.state;
    battle::BattleState direct = s.state;
    executor.runAi(wired, kTerra);
    battle::runAiTurn(direct, executor, kTerra);
    ASSERT_FALSE(wired.at(kTerra).actions.empty());
    EXPECT_EQ(listed(wired.at(kTerra).actions), listed(direct.at(kTerra).actions));
}

TEST(AiSeams, ACounterattackReadsTheCounterPartAimedAtTheBattlerItAnswers) {
    // ExecRetal, :12416-12425, and ExecAI, :4218-4223: the battler answered is where the run's aim
    // starts. Mag Roader's fourth script answers with Wild Cat's turn part: with more than one
    // monster standing it skips the Fire Ball block and attacks, aimed where the run is.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    AiScenario s = withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::MAG_ROADER_4));
    s.state.counterattackRunning = true;
    battle::BattleState wired = s.state;
    battle::BattleState direct = s.state;
    executor.runCounterAi(wired, kSelf, battle::BattlerSet::of(kTerra));
    battle::runAiCounter(direct, executor, kSelf, battle::BattlerSet::of(kTerra));
    EXPECT_TRUE(wired.counterAiRanThisStep.has(kSelf));
    ASSERT_EQ(wired.at(kSelf).counterActions.size(), 1u);
    const auto* answer = std::get_if<battle::CommandAction>(&wired.at(kSelf).counterActions.front());
    ASSERT_NE(answer, nullptr);
    EXPECT_EQ(answer->targets, battle::BattlerSet::of(kTerra));
    EXPECT_EQ(listed(wired.at(kSelf).counterActions), listed(direct.at(kSelf).counterActions));
    EXPECT_EQ(wired.rng.position(), direct.rng.position());
}

TEST(AiSeams, LoadingAMonsterReadiesItsScript) {
    // LoadMonsterProp, :7313: InitAI with the monster's index.
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    AiScenario s = aiBattle();
    s.state.at(kSelf).aiScript = static_cast<std::uint16_t>(MonsterId::REPO_MAN);
    executor.initAi(s.state, kSelf);
    const AiScript& script = aiScript(static_cast<std::uint16_t>(MonsterId::REPO_MAN));
    EXPECT_EQ(s.state.at(kSelf).turnScript.data(), script.turn.data());
    EXPECT_EQ(s.state.at(kSelf).counterScript.data(), script.counter.data());
    EXPECT_TRUE(s.state.at(kSelf).hasCounterScript);
}

TEST(AiSeams, AScriptedBattleSeatsAPartyMemberWithItsScriptReadied) {
    // InitBattle, :7835-7840: a scripted setup's party member has InitAI called as it is seated,
    // before the monsters are loaded. The Veldt's battle seats Gau with his script.
    FormationId veldt{};
    bool found = false;
    for (const FormationEntry& entry : formations()) {
        const FormationAux& aux = getFormationAux(entry.id);
        if (aux.characterAiEnabled() && static_cast<CharAiId>(aux.characterAiIndex()) == CharAiId::GAU_VELDT) {
            veldt = entry.id;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
    battle::BattleState party = battleWithParty({CharacterPropId::SABIN, CharacterPropId::CYAN});
    party.party.characters[7] = startingRecord(CharacterPropId::GAU);
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankText(), noSprites());
    const battle::BattleState state = battle::startBattle(battle::BattleStart{.formation = FormationRef::of(veldt)},
                                                          party.party, {}, blankText(), noSprites(), executor);
    const battle::BattlerSlot gau = battle::BattlerSlot::character(2);
    ASSERT_EQ(state.at(gau).actor, CharacterPropId::GAU);
    ASSERT_EQ(state.at(gau).aiScript, kGauVeldtScript);
    const AiScript& script = aiScript(kGauVeldtScript);
    EXPECT_EQ(state.at(gau).turnScript.data(), script.turn.data());
    EXPECT_EQ(state.at(gau).turnScript.size(), script.turn.size());
    EXPECT_EQ(state.at(gau).hasCounterScript, script.hasCounter());
}

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
    Replayed replayed;
    withAiOracle([&](trace::DamageOracle& oracle) {
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
    std::mt19937 rng{trace::kSweepSeed};
    Replayed replayed;
    withAiOracle([&](trace::DamageOracle& oracle) {
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
    std::mt19937 rng{trace::kSweepSeed};
    Replayed replayed;
    withAiOracle([&](trace::DamageOracle& oracle) {
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
    // SetStatus and ClearStatus make an attack: the trace follows them through the attack's own
    // calls, and the effect's call itself is not among the ones a trace holds.
    std::mt19937 rng{trace::kSweepSeed};
    Replayed replayed;
    int attacks = 0;
    int commands = 0;
    withAiOracle([&](trace::DamageOracle& oracle) {
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
    withAiOracle([&](trace::DamageOracle& oracle) {
        for (const TracedCall& call : calls) {
            if (call.routine == "InitAI" || call.routine == "ExecMonsterAction" || call.routine == "ExecAIRetal" ||
                call.routine.starts_with("Cmd_")) {
                EXPECT_EQ(oracle.replay(call), "") << name << " frame " << call.frame << ": " << call.routine;
            }
        }
    });
}

TEST(AiRecordings, TheGuardsTakeTheirTurnsAndOneAnswersAnAttackAsTheCartridgeDid) {
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
