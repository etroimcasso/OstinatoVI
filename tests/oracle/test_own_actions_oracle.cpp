// The battle's own actions on the oracle: damage over time, a spell with no caster, status timers
// running out and Seize's drain are where the US cartridges hold them and an image without any of
// them is refused; they read no cell a scenario leaves unwritten; and over drawn battles the port's
// actions (CommandExecutor::execute()) leave every cell as the cartridge's own commands do, a
// script's SetStatus and ClearStatus among them.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ios>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "battle/action_queue.h"
#include "battle/battle_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_id.h"
#include "own_actions_oracle_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::AttackResult;
using trace::DamageOracle;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

// Damage over time, a spell with no caster, status timers running out, Seize's drain.
constexpr std::array<std::uint32_t, 4> kCommands{0xC2500B, 0xC24F5F, 0xC25161, 0xC251B2};

// Whether any attack in the script hit someone: a params block whose battlers hit are not none.
bool hitSomeone(const std::string& script) {
    const std::string hit = " hit ";
    for (std::size_t at = script.find(hit); at != std::string::npos; at = script.find(hit, at + hit.size())) {
        if (script[at + hit.size()] != '0') {
            return true;
        }
    }
    return false;
}

TEST_F(OwnActionsOracleTest, AnImageWithoutAnyOfTheFourCommandsIsRefused) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        for (const std::uint32_t at : kCommands) {
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(at)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << at;
        }
    });
}

// Every cell the commands read came from the scenario or from the commands themselves, but the cells
// left as the machine has them.
TEST_F(OwnActionsOracleTest, TheBattlesOwnActionsReadNothingTheScenarioDoesNotWrite) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    withStartedOracle(OwnActionsOracleTest::image(), [&](DamageOracle& oracle) {
        for (int draw = 0; draw < 50; ++draw) {
            const trace::OwnActionCase c = trace::drawOwnActionCase(rng);
            (void)oracle.ownAction(c.scenario, c.action);
            std::string text;
            for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
                if (!trace::leftAsTheMachineHasIt(read.at)) {
                    char line[24];
                    std::snprintf(line, sizeof line, "$%02X:%04X ", read.at >> 16 & 0xFF, read.at & 0xFFFF);
                    text += line;
                }
            }
            EXPECT_EQ(text, "") << "draw " << draw;
        }
    });
}

// What the sweep reached, read from the port's side.
struct Reached {
    int differing = 0;
    int poisoned = 0;
    int regenerated = 0;
    int sapped = 0;
    int held = 0;
    int noEnemies = 0;
    int doomStopsTheCount = 0;
    int spell = 0;
    int timers = 0;
    int woken = 0;
    int seized = 0;
    int scriptEffect = 0;
    std::string first;
};

TEST_F(OwnActionsOracleTest, FiveHundredDrawnActionsMatchTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    Reached reached;
    std::mt19937 rng{trace::kSweepSeed};
    withStartedOracle(OwnActionsOracleTest::image(), [&](DamageOracle& oracle) {
        for (int draw = 0; draw < 500; ++draw) {
            const trace::OwnActionCase c = trace::drawOwnActionCase(rng);
            const AttackResult port = trace::portOwnAction(c.scenario, c.action);
            const std::string difference = trace::differences(port, oracle.ownAction(c.scenario, c.action));
            if (!difference.empty() && ++reached.differing == 1) {
                reached.first = "draw " + std::to_string(draw) + " (" + trace::describe(c.scenario) + "):\n" + difference;
            }
            const bool attacked = hitSomeone(cell(port, "script"));
            const battle::BattleState& state = c.scenario.state;
            if (const auto* dot = std::get_if<battle::DamageOverTime>(&c.action)) {
                const battle::Battler& battler = state.at(c.scenario.attacker);
                reached.noEnemies += state.enemyCount == 0 ? 1 : 0;
                reached.held += state.enemyCount != 0 && battler.heldByStatus ? 1 : 0;
                if (attacked) {
                    reached.poisoned += dot->kind == battle::DamageOverTimeKind::Poison ? 1 : 0;
                    reached.regenerated += dot->kind == battle::DamageOverTimeKind::Regen ? 1 : 0;
                    reached.sapped += dot->kind == battle::DamageOverTimeKind::Sap ? 1 : 0;
                }
            } else if (const auto* spell = std::get_if<battle::SpellWithoutCaster>(&c.action)) {
                reached.doomStopsTheCount += spell->spell == AttackId::DOOM && state.enemyCount == 0 ? 1 : 0;
                reached.spell += attacked ? 1 : 0;
            } else if (std::holds_alternative<battle::StatusTimersExpired>(c.action)) {
                reached.timers += attacked ? 1 : 0;
                reached.woken += countOf(cell(port, "script"), "animation 2 120 ") > 0 ? 1 : 0;
            } else if (std::holds_alternative<battle::SeizeDrain>(c.action)) {
                reached.seized += attacked ? 1 : 0;
            } else {
                reached.scriptEffect += attacked ? 1 : 0;
            }
        }
    });
    EXPECT_EQ(reached.differing, 0) << reached.differing << " of 500 differ; the first:\n" << reached.first;
    EXPECT_GT(reached.poisoned, 0);
    EXPECT_GT(reached.regenerated, 0);
    EXPECT_GT(reached.sapped, 0);
    EXPECT_GT(reached.held, 0);
    EXPECT_GT(reached.noEnemies, 0);
    EXPECT_GT(reached.doomStopsTheCount, 0);
    EXPECT_GT(reached.spell, 0);
    EXPECT_GT(reached.timers, 0);
    EXPECT_GT(reached.woken, 0);
    EXPECT_GT(reached.seized, 0);
    EXPECT_GT(reached.scriptEffect, 0);
}

}  // namespace
}  // namespace ostinato::test
