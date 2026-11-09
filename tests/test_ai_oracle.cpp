// A monster AI script's targets and conditions on the oracle (docs/contracts/battle_ai.md): the
// target routine and the condition routines are where the US cartridges hold them, the cartridge's
// table of conditions points at the routines the original names, and an image without them is
// refused; then 500 drawn battles through each — every target a script can name, with and without
// the cut to the reachable, and every condition a script can hold — each run on the cartridge's own
// routine (tools/damage-trace/damage_oracle.h) and by the port, the two matching.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "damage_oracle.h"
#include "data/ai_scripts.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using trace::AiConditionCase;
using trace::AiConditionResult;
using trace::AiTargetsCase;
using trace::AiTargetsResult;
using trace::DamageOracle;

constexpr std::uint32_t kCheckAiTarget = 0xC21F25;
constexpr std::uint32_t kAiTargetTest = 0xC21D34;
constexpr std::uint32_t kAiTargetTestClear = 0xC21D4F;
constexpr std::uint32_t kAiConditionTable = 0xC21D55;
constexpr std::size_t kAiConditions = 29;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

class AiOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    template <typename Body>
    static void withOracle(Body body) {
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            body(oracle);
        });
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* AiOracleTest::image_ = nullptr;

// --- where the routines are ----------------------------------------------------------------------

TEST_F(AiOracleTest, TheTargetRoutineAndTheTargetTestAreWhereTheUsCartridgesHoldThem) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto bytes = [&](std::uint32_t at) {
            return oracle.machine().read(retropp::MemoryRegion{.at = at, .size = 4});
        };
        EXPECT_EQ(bytes(kCheckAiTarget), (std::vector<std::uint8_t>{0xDA, 0x5A, 0x48, 0x64}));  // phx; phy; pha; stz
        EXPECT_EQ(bytes(kAiTargetTest), (std::vector<std::uint8_t>{0xAD, 0x2E, 0x3A, 0x48}));   // lda $3a2e; pha
        // The test on the monster itself clears the same byte twice.
        EXPECT_EQ(bytes(kAiTargetTestClear), (std::vector<std::uint8_t>{0x64, 0xFC, 0x64, 0xFC}));  // stz $fc; stz $fc
    });
}

TEST_F(AiOracleTest, TheTableOfConditionsPointsAtTheTwentyNineRoutines) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const std::vector<std::uint8_t> table =
            oracle.machine().read(retropp::MemoryRegion{.at = kAiConditionTable, .size = kAiConditions * 2});
        const auto word = [&](std::size_t number) {
            return static_cast<std::uint16_t>(table[number * 2] | table[number * 2 + 1] << 8);
        };
        EXPECT_EQ(word(0x00), 0x1D2C);  // never
        EXPECT_EQ(word(0x0A), word(0x00));  // the second number that never holds is the same routine
        EXPECT_EQ(word(0x17), 0x1D34);  // the target test
        EXPECT_EQ(word(0x1C), 0x1D22);  // always
    });
}

TEST_F(AiOracleTest, AnImageWithoutTheRoutinesOrTheirTableIsRefused) {
    for (const std::uint32_t at : {kCheckAiTarget, kAiTargetTest, kAiTargetTestClear, kAiConditionTable + 2 * 9}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(at)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << at;
        });
    }
}

// --- the sweeps ----------------------------------------------------------------------------------

constexpr int kSweepDraws = 500;

TEST_F(AiOracleTest, EveryDrawnTargetIsTheCartridges) {
    std::mt19937 rng{trace::kSweepSeed};
    int found = 0;
    int empty = 0;
    int reachable = 0;
    int picked = 0;
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const AiTargetsCase c = trace::drawAiTargetsCase(rng);
            const AiTargetsResult port = trace::portAiTargets(c.scenario, c.target, c.reachableOnly);
            const AiTargetsResult guest = oracle.aiTargets(c.scenario, c.target, c.reachableOnly);
            EXPECT_EQ(trace::differences(port, guest), "") << "draw " << draw << ": " << trace::describe(c.scenario);
            found += port.any;
            empty += !port.any;
            reachable += c.reachableOnly;
            picked += port.rngPosition != c.scenario.state.rng.position();
        }
    });
    EXPECT_GT(found, 0);
    EXPECT_GT(empty, 0);
    EXPECT_GT(reachable, 0);
    EXPECT_GT(picked, 0);
}

TEST_F(AiOracleTest, EveryDrawnConditionIsTheCartridges) {
    std::mt19937 rng{trace::kSweepSeed};
    std::array<int, std::variant_size_v<AiCondition>> held{};
    std::array<int, std::variant_size_v<AiCondition>> failed{};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const AiConditionCase c = trace::drawAiConditionCase(rng);
            const AiConditionResult port = trace::portAiCondition(c.scenario, c.condition);
            const AiConditionResult guest = oracle.aiCondition(c.scenario, c.condition);
            EXPECT_EQ(trace::differences(port, guest), "")
                << "draw " << draw << " condition " << c.condition.index() << ": " << trace::describe(c.scenario);
            (port.holds ? held : failed)[c.condition.index()] += 1;
        }
    });
    // Every condition but the one that never holds and the one that always does is seen both ways.
    const std::size_t never = AiCondition{ai::Never{}}.index();
    const std::size_t always = AiCondition{ai::Always{}}.index();
    for (std::size_t condition = 0; condition < held.size(); ++condition) {
        if (condition == never) {
            EXPECT_EQ(held[condition], 0);
            EXPECT_GT(failed[condition], 0);
        } else if (condition == always) {
            EXPECT_GT(held[condition], 0);
            EXPECT_EQ(failed[condition], 0);
        } else {
            EXPECT_GT(held[condition], 0) << "condition " << condition;
            EXPECT_GT(failed[condition], 0) << "condition " << condition;
        }
    }
}

}  // namespace
}  // namespace ostinato::test
