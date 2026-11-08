// The attacker-effect routine on the oracle: it is where the US cartridges hold it and is refused
// where it is not, the attack frame calls it where the oracle's call returns, a scenario's cells read
// back as they were written, a machine without a cartridge fails naming what it lacks, and over 500
// drawn scenarios the port's attacker effects leave every cell as the cartridge's own routine does.
// The trace's replay of both effect routines: the cartridge's own calls, traced, rebuilt from the cells
// each read and worked out by the port with no difference; a target-effect call without its target
// is refused.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attacker_effects.h"
#include "damage_oracle.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::AttackerEffectResult;
using trace::DamageOracle;
using trace::DamageScenario;

class EffectsOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* EffectsOracleTest::image_ = nullptr;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

constexpr std::uint32_t kDoAttackerEffect = 0xC23E7D;

TEST_F(EffectsOracleTest, AnImageWithoutDoAttackerEffectIsRefused) {
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(kDoAttackerEffect)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

// The attack frame's `jsr DoAttackerEffect` (battle_main.asm:8275) returns to $C2:33A9: the three
// bytes before it are the call.
TEST_F(EffectsOracleTest, TheFramesCallReturnsWhereTheOracleExpects) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const std::vector<std::uint8_t> call =
            oracle.machine().read(retropp::MemoryRegion{.at = 0xC233A9 - 3, .size = 3});
        const std::vector<std::uint8_t> toDoAttackerEffect{0x20, 0x7D, 0x3E};
        EXPECT_EQ(call, toDoAttackerEffect);
    });
}

// With no special effect the routine changes nothing: every cell reads back as written.
TEST_F(EffectsOracleTest, AScenarioWithNoEffectReadsBackAsWritten) {
    std::mt19937 rng{trace::kSweepSeed};
    DamageScenario scenario = trace::drawAttackerEffectScenario(rng);
    scenario.attack.effect = {};
    const AttackerEffectResult unchanged = trace::attackerEffectResult(trace::AttackerEffectRun{.after = scenario});
    AttackerEffectResult guest;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        guest = oracle.attackerEffect(scenario);
    });
    EXPECT_EQ(trace::differences(unchanged, guest), "");
}

// A machine that has not been given a cartridge fails, naming the variable that names one.
TEST(EffectsCartridge, AMissingCartridgeFailsNamingTheVariable) {
    constexpr const char* kVariable = "FF6_VANILLA_ROM";
    const char* was = std::getenv(kVariable);
    const std::optional<std::string> saved = was != nullptr ? std::optional<std::string>{was} : std::nullopt;
#ifdef _WIN32
    _putenv_s(kVariable, "");
#else
    unsetenv(kVariable);
#endif
    std::string message;
    try {
        (void)vanillaRom();
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
#ifdef _WIN32
    _putenv_s(kVariable, saved ? saved->c_str() : "");
#else
    if (saved) {
        setenv(kVariable, saved->c_str(), 1);
    }
#endif
    EXPECT_NE(message.find(kVariable), std::string::npos) << message;
}

constexpr int kSweepDraws = 500;

TEST_F(EffectsOracleTest, EveryDrawnScenarioLeavesTheCellsAsTheCartridgeDoes) {
    std::mt19937 rng{trace::kSweepSeed};
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const DamageScenario scenario = trace::drawAttackerEffectScenario(rng);
            const AttackerEffectResult port = trace::attackerEffectResult(trace::runAttackerEffect(scenario));
            const AttackerEffectResult guest = oracle.attackerEffect(scenario);
            const std::string differ = trace::differences(port, guest);
            EXPECT_EQ(differ, "") << "draw " << draw << ", effect " << int{scenario.attack.effect.index()}
                                  << ", attacker " << scenario.attacker.index();
        }
    });
}

// --- the trace's replay --------------------------------------------------------------------------

constexpr int kReplayDraws = 200;

// The cartridge's own calls of both effect routines, traced as the oracle makes them, replay against
// the port with no difference: the battle rebuilt from the cells each call read.
TEST_F(EffectsOracleTest, TheAttackerEffectsTracedCallsReplayAsTheCartridgeWorkedThemOut) {
    std::mt19937 rng{trace::kSweepSeed};
    int calls = 0;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kReplayDraws; ++draw) {
            const DamageScenario scenario = trace::drawAttackerEffectScenario(rng);
            oracle.trace(true);
            (void)oracle.attackerEffect(scenario);
            oracle.trace(false);
            std::string text;
            for (const std::string& line : oracle.takeTrace()) {
                text += line + "\n";
            }
            std::istringstream in{text};
            for (const trace::TracedCall& call : trace::parseTrace(in)) {
                EXPECT_EQ(call.routine, "DoAttackerEffect");
                EXPECT_EQ(oracle.replay(call), "") << "draw " << draw << ", effect " << int{scenario.attack.effect.index()};
                ++calls;
            }
        }
    });
    EXPECT_EQ(calls, kReplayDraws);
}

// The oracle calls the target effect with no statuses worked out before it, so its trace does not show
// the target: each call is given the one it was made for.
TEST_F(EffectsOracleTest, TheTargetEffectsTracedCallsReplayAsTheCartridgeWorkedThemOut) {
    std::mt19937 rng{trace::kSweepSeed};
    int calls = 0;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kReplayDraws; ++draw) {
            const trace::TargetScenario drawn = trace::drawTargetEffectScenario(rng);
            oracle.trace(true);
            (void)oracle.targetEffect(drawn.scenario, drawn.target);
            oracle.trace(false);
            std::string text;
            for (const std::string& line : oracle.takeTrace()) {
                text += line + "\n";
            }
            std::istringstream in{text};
            for (trace::TracedCall call : trace::parseTrace(in)) {
                EXPECT_EQ(call.routine, "DoTargetEffect");
                EXPECT_FALSE(call.target.has_value());
                call.target = drawn.target;
                EXPECT_EQ(oracle.replay(call), "")
                    << "draw " << draw << ", effect " << int{drawn.scenario.attack.effect.index()};
                ++calls;
            }
        }
    });
    EXPECT_EQ(calls, kReplayDraws);
}

// A target-effect call with no target in the trace cannot be rebuilt, and says why.
TEST_F(EffectsOracleTest, ATargetEffectCallWithNoTargetIsNotRebuilt) {
    std::mt19937 rng{trace::kSweepSeed};
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        const trace::TargetScenario drawn = trace::drawTargetEffectScenario(rng);
        oracle.trace(true);
        (void)oracle.targetEffect(drawn.scenario, drawn.target);
        oracle.trace(false);
        std::string text;
        for (const std::string& line : oracle.takeTrace()) {
            text += line + "\n";
        }
        std::istringstream in{text};
        const std::vector<trace::TracedCall> calls = trace::parseTrace(in);
        ASSERT_EQ(calls.size(), 1u);
        try {
            (void)oracle.replay(calls.front());
            ADD_FAILURE() << "a call with no target was rebuilt";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string{error.what()}.find("target"), std::string::npos) << error.what();
        }
    });
}

}  // namespace
}  // namespace ostinato::test
