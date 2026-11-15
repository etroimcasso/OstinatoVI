// The special effects on each target on the oracle (docs/contracts/battle_effects.md, "The effects on
// each target"): the routine is where the US cartridges hold it and is refused where it is not, the
// frame's and the bounce's calls return where the trace expects them, a scenario with no effect reads
// back as written, and over 500 drawn scenarios the port leaves every cell as the cartridge
// (tools/damage-trace/src/oracle/damage_oracle.h) does.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

#include "battle/target_effects.h"
#include "oracle/damage_oracle.h"
#include "target_effects_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::DamageOracle;
using trace::TargetEffectResult;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

constexpr std::uint32_t kDoTargetEffect = 0xC2387E;

constexpr int kSweepDraws = 500;

TEST_F(TargetEffectTest, AnImageWithoutDoTargetEffectIsRefused) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(kDoTargetEffect)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

// The attack frame's `jsr DoTargetEffect` (battle_main.asm:8357) returns to $C2:345F, the bounce's
// (:8435) to $C2:34FC: the three bytes before each are the call.
TEST_F(TargetEffectTest, TheFramesAndTheBouncesCallsReturnWhereTheTraceExpects) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const std::vector<std::uint8_t> toDoTargetEffect{0x20, 0x7E, 0x38};
        for (const std::uint32_t returnsTo : {0xC2345Fu, 0xC234FCu}) {
            const std::vector<std::uint8_t> call =
                oracle.machine().read(retropp::MemoryRegion{.at = returnsTo - 3, .size = 3});
            EXPECT_EQ(call, toDoTargetEffect) << std::hex << returnsTo;
        }
    });
}

// With no special effect the routine changes nothing: every cell reads back as written.
TEST_F(TargetEffectTest, AScenarioWithNoEffectReadsBackAsWritten) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    trace::TargetScenario drawn = trace::drawTargetEffectScenario(rng);
    drawn.scenario.attack.effect = {};
    const TargetEffectResult unchanged = trace::targetEffectResult(trace::runTargetEffect(drawn.scenario, drawn.target));
    TargetEffectResult guest;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        guest = oracle.targetEffect(drawn.scenario, drawn.target);
    });
    EXPECT_EQ(trace::differences(unchanged, guest), "");
}

TEST_F(TargetEffectTest, EveryDrawnScenarioLeavesTheCellsAsTheCartridgeDoes) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        for (int draw = 0; draw < kSweepDraws; ++draw) {
            const trace::TargetScenario drawn = trace::drawTargetEffectScenario(rng);
            const TargetEffectResult port =
                trace::targetEffectResult(trace::runTargetEffect(drawn.scenario, drawn.target));
            const TargetEffectResult guest = oracle.targetEffect(drawn.scenario, drawn.target);
            EXPECT_EQ(trace::differences(port, guest), "")
                << "draw " << draw << ", effect " << int{drawn.scenario.attack.effect.index()} << ", attacker "
                << drawn.scenario.attacker.index() << ", target " << drawn.target.index();
        }
    });
}

}  // namespace
}  // namespace ostinato::test
