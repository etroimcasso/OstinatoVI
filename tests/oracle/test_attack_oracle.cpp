// The attack frame on the oracle: ExecSelfAttack and ExecAttack are where the US cartridges hold them
// and an image without either is refused; the frame reads no cell a scenario leaves unwritten; over
// drawn battles the port's whole attack (battle::runAttack, battle::runSelfAttack) leaves every cell
// as the cartridge's own frame does; and the routines the frame calls, traced while it runs, replay
// against the port with no difference.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "attack_oracle_cases.h"
#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::AttackResult;
using trace::DamageOracle;
using trace::DamageScenario;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

constexpr std::uint32_t kExecSelfAttack = 0xC23167;
constexpr std::uint32_t kExecAttack = 0xC2317B;

TEST_F(AttackOracleTest, AnImageWithoutExecAttackIsRefused) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(kExecAttack)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

TEST_F(AttackOracleTest, AnImageWithoutExecSelfAttackIsRefused) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(kExecSelfAttack)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

// Every cell the frame read came from the scenario or from the frame itself, but the cells left as
// the machine has them.
std::string readsUnwritten(DamageOracle& oracle) {
    std::string text;
    for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
        if (!trace::leftAsTheMachineHasIt(read.at)) {
            char line[24];
            std::snprintf(line, sizeof line, "$%02X:%04X ", read.at >> 16 & 0xFF, read.at & 0xFFFF);
            text += line;
        }
    }
    return text;
}

TEST_F(AttackOracleTest, TheAttackFrameReadsNothingTheScenarioDoesNotWrite) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    withStartedOracle(AttackOracleTest::image(), [&](DamageOracle& oracle) {
        for (int draw = 0; draw < 50; ++draw) {
            (void)oracle.attack(trace::drawAttackScenario(rng));
            EXPECT_EQ(readsUnwritten(oracle), "") << "draw " << draw;
        }
    });
}

TEST_F(AttackOracleTest, TheSelfAttackFrameReadsNothingTheScenarioDoesNotWrite) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    withStartedOracle(AttackOracleTest::image(), [&](DamageOracle& oracle) {
        for (int draw = 0; draw < 50; ++draw) {
            (void)oracle.selfAttack(trace::drawSelfAttackScenario(rng));
            EXPECT_EQ(readsUnwritten(oracle), "") << "draw " << draw;
        }
    });
}

// The routines the frame calls, traced while it runs, are closed at the frame's own return sites
// and replay against the port with no difference.
TEST_F(AttackOracleTest, TheFramesInnerCallsReplayAsTheCartridgeWorkedThemOut) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    int calls = 0;
    withStartedOracle(AttackOracleTest::image(), [&](DamageOracle& oracle) {
        oracle.trace(true);
        for (int draw = 0; draw < 20; ++draw) {
            (void)oracle.attack(trace::drawAttackScenario(rng));
            std::stringstream log;
            for (const std::string& line : oracle.takeTrace()) {
                log << line << '\n';
            }
            for (const trace::TracedCall& call : trace::parseTrace(log)) {
                ++calls;
                EXPECT_EQ(oracle.replay(call), "") << "draw " << draw << ", " << call.routine;
            }
        }
        oracle.trace(false);
    });
    EXPECT_GE(calls, 100);
}

// What a sweep reached, read from the port's side.
struct Reached {
    int differing = 0;
    int named = 0;
    int shortOfMp = 0;
    int fizzled = 0;
    int runic = 0;
    int critical = 0;
    int bounced = 0;
    int twoAttacks = 0;
    int anchored = 0;
    int lore = 0;
    int fought = 0;
    int shieldHand = 0;
    int offering = 0;
    int captured = 0;
    int emptyHand = 0;
    std::string first;
};

void sweep(DamageOracle& oracle, bool self, Reached& reached) {
    std::mt19937 rng{trace::kSweepSeed};
    for (int draw = 0; draw < 500; ++draw) {
        const DamageScenario s = self ? trace::drawSelfAttackScenario(rng) : trace::drawAttackScenario(rng);
        const trace::AttackRun run = self ? trace::portSelfAttack(s) : trace::portAttack(s);
        const AttackResult port = trace::attackResult(run);
        const std::string difference = trace::differences(port, self ? oracle.selfAttack(s) : oracle.attack(s));
        if (!difference.empty() && ++reached.differing == 1) {
            reached.first = "draw " + std::to_string(draw) + " (" + trace::describe(s) + "):\n" + difference;
        }
        const std::string& script = port.cells.at("script");
        reached.named += countOf(script, "name ") > 0 ? 1 : 0;
        reached.shortOfMp += countOf(script, "message 40;") > 0 ? 1 : 0;
        reached.fizzled += countOf(script, "pose reset") > 0 ? 1 : 0;
        reached.runic += countOf(script, "animation 37 ") > 0 ? 1 : 0;
        reached.critical += countOf(script, "critical 1") > 0 ? 1 : 0;
        reached.bounced += countOf(script, "bounced attack") + countOf(script, "scattered attack") > 0 ? 1 : 0;
        reached.twoAttacks += countOf(script, "animation") >= 2 ? 1 : 0;
        reached.anchored += s.state.at(s.attacker).airAnchored ? 1 : 0;
        reached.lore += run.after.state.loresLearned != s.state.loresLearned ? 1 : 0;
        if (const std::optional<BattleCommandId> fight = s.attack.scratch.fightCommand) {
            const battle::Battler& attacker = s.state.at(s.attacker);
            ++reached.fought;
            reached.captured += *fight == BattleCommandId::CAPTURE ? 1 : 0;
            reached.offering += attacker.relicEffects4.has(RelicEffect4::FIGHT_TO_X_FIGHT) ? 1 : 0;
            reached.emptyHand += attacker.shieldHand.battlePower == 0 ? 1 : 0;
            for (const battle::BattleScriptCommand& command : run.script.commands()) {
                const auto* animation = std::get_if<battle::AttackAnimation>(&command);
                if (animation != nullptr && animation->offHand) {
                    ++reached.shieldHand;
                    break;
                }
            }
        }
    }
}

void expectEveryPathReached(const Reached& reached) {
    EXPECT_EQ(reached.differing, 0) << reached.differing << " of 500 differ; the first:\n" << reached.first;
    EXPECT_GT(reached.named, 0);
    EXPECT_GT(reached.shortOfMp, 0);
    EXPECT_GT(reached.fizzled, 0);
    EXPECT_GT(reached.runic, 0);
    EXPECT_GT(reached.critical, 0);
    EXPECT_GT(reached.bounced, 0);
    EXPECT_GT(reached.twoAttacks, 0);
    EXPECT_GT(reached.anchored, 0);
    EXPECT_GT(reached.lore, 0);
}

// The paths only a command that loads a hand takes: a Fight, each hand swung, an Offering's eight
// swings, Capture's hit and a hand with no power.
void expectEveryHandPathReached(const Reached& reached) {
    EXPECT_GT(reached.fought, 0);
    EXPECT_GT(reached.shieldHand, 0);
    EXPECT_GT(reached.offering, 0);
    EXPECT_GT(reached.captured, 0);
    EXPECT_GT(reached.emptyHand, 0);
}

TEST_F(AttackOracleTest, FiveHundredDrawnAttacksMatchTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    Reached reached;
    withStartedOracle(AttackOracleTest::image(), [&](DamageOracle& oracle) { sweep(oracle, false, reached); });
    expectEveryPathReached(reached);
    expectEveryHandPathReached(reached);
}

TEST_F(AttackOracleTest, FiveHundredDrawnSelfAttacksMatchTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    Reached reached;
    withStartedOracle(AttackOracleTest::image(), [&](DamageOracle& oracle) { sweep(oracle, true, reached); });
    expectEveryPathReached(reached);
}

}  // namespace
}  // namespace ostinato::test
