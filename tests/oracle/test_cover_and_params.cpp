// The cover and the attack's animation set up on the oracle (docs/contracts/battle_commands.md, "The
// attack frame" and "Cover"): both routines are where the US cartridges hold them and an image without
// either is refused; then 500 drawn scenarios through each routine, the port against the cartridge's
// own routine (tools/damage-trace/src/oracle/damage_oracle.h), the draws reaching both bodyguards and
// both exclusions.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

#include "battle/attack.h"
#include "battle/targeting.h"
#include "cover_and_params_cases.h"
#include "oracle/damage_oracle.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using trace::DamageOracle;
using trace::DamageScenario;

constexpr std::uint32_t kInitGfxParams = 0xC257C2;
constexpr std::uint32_t kCoverEffect = 0xC2123B;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

// --- where the routines are ----------------------------------------------------------------------

TEST_F(CoverAndParamsTest, BothRoutinesAreWhereTheUsCartridgesHoldThem) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const std::vector<std::uint8_t> initGfxParams =
            oracle.machine().read(retropp::MemoryRegion{.at = kInitGfxParams, .size = 4});
        const std::vector<std::uint8_t> coverEffect =
            oracle.machine().read(retropp::MemoryRegion{.at = kCoverEffect, .size = 4});
        EXPECT_EQ(initGfxParams, (std::vector<std::uint8_t>{0x08, 0xE2, 0x30, 0x64}));  // php; sep #$30; stz $a0
        EXPECT_EQ(coverEffect, (std::vector<std::uint8_t>{0xDA, 0x08, 0xA5, 0xB2}));    // phx; php; lda $b2
    });
}

TEST_F(CoverAndParamsTest, AnImageWithoutEitherRoutineIsRefused) {
    OSTINATO_SKIP_WHEN_BRIEF();
    for (const std::uint32_t routine : {kInitGfxParams, kCoverEffect}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(routine)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << routine;
        });
    }
}

// --- the drawn scenarios -------------------------------------------------------------------------

constexpr int kSweepDraws = 500;

TEST_F(CoverAndParamsTest, EveryDrawnScenarioSetsUpTheParamsAsTheCartridgeDoes) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    int dragonHornLandings = 0;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const DamageScenario scenario = trace::drawAttackerEffectScenario(rng);
            if (scenario.attack.scratch.dragonHorn && scenario.attack.scratch.attacksLeft != 0) {
                ++dragonHornLandings;
            }
            EXPECT_EQ(trace::differences(trace::portInitParams(scenario), oracle.initParams(scenario)), "")
                << "draw " << draw;
        }
    });
    EXPECT_GT(dragonHornLandings, 0);
}

// Whether the cover would have gone otherwise for the battler at `slot` with its link cleared:
// the draw reached the rule that keeps a battler with that link out.
bool linkMattered(const DamageScenario& scenario, BattlerSlot slot, bool held) {
    DamageScenario as = scenario;
    battle::coverTarget(as.state, as.attack, as.attacker);
    DamageScenario freed = scenario;
    if (held) {
        freed.state.at(slot).seizedBy.reset();
    } else {
        freed.state.at(slot).controlledBy.reset();
    }
    battle::coverTarget(freed.state, freed.attack, freed.attacker);
    return !(as.attack.params.steppingIn == freed.attack.params.steppingIn);
}

// The target a blow at `targets` covers: the highest-numbered one.
std::optional<BattlerSlot> coveredTarget(BattlerSet targets) {
    for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
        if (targets.has(BattlerSlot::fromIndex(index))) {
            return BattlerSlot::fromIndex(index);
        }
    }
    return std::nullopt;
}

// Over 500 drawn scenarios the cover leaves the targets, the covered ally and the battlers stepping
// in as the cartridge's routine does, and the draws reach both bodyguards and both exclusions.
TEST_F(CoverAndParamsTest, EveryDrawnScenarioIsCoveredAsTheCartridgeCoversIt) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    int loveTokens = 0;
    int trueKnights = 0;
    int controlledKeptOut = 0;
    int heldKeptOut = 0;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const DamageScenario scenario = trace::drawCoverScenario(rng);
            EXPECT_EQ(trace::differences(trace::portCoverTarget(scenario), oracle.coverEffect(scenario)), "")
                << "draw " << draw;

            DamageScenario after = scenario;
            battle::coverTarget(after.state, after.attack, after.attacker);
            if (after.attack.params.coveredAlly) {
                const auto& bodyguard = scenario.state.at(*after.attack.params.coveredAlly).bodyguard;
                const bool byLoveToken = bodyguard && after.attack.targets == BattlerSet::of(*bodyguard);
                ++(byLoveToken ? loveTokens : trueKnights);
            }
            const std::optional<BattlerSlot> covered = coveredTarget(scenario.attack.targets);
            for (int index = 0; index < battle::kBattlerSlots; ++index) {
                const BattlerSlot slot = BattlerSlot::fromIndex(index);
                if (slot == covered) {
                    continue;
                }
                const battle::Battler& battler = scenario.state.at(slot);
                if (battler.controlledBy && !battler.controlEnding && linkMattered(scenario, slot, false)) {
                    ++controlledKeptOut;
                }
                if (battler.seizedBy && linkMattered(scenario, slot, true)) {
                    ++heldKeptOut;
                }
            }
        }
    });
    EXPECT_GT(loveTokens, 0);
    EXPECT_GT(trueKnights, 0);
    EXPECT_GT(controlledKeptOut, 0);
    EXPECT_GT(heldKeptOut, 0);
    std::cout << "love tokens " << loveTokens << ", true knights " << trueKnights << ", controlled kept out "
              << controlledKeptOut << ", held kept out " << heldKeptOut << '\n';
}

}  // namespace
}  // namespace ostinato::test
