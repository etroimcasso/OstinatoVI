// The damage to one target over drawn scenarios (docs/contracts/battle_damage.md): 500 scenarios drawn
// at random, every cell the pass writes compared between the port and the cartridge's own routine
// through the damage oracle (tools/damage-trace/src/oracle/damage_oracle.h).
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <random>

#include "battle/damage.h"
#include "oracle/damage_oracle.h"
#include "target_damage_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::TargetDamageResult;

constexpr int kSweepDraws = 500;

TEST_F(TargetDamageTest, TargetDamageSweep_EveryCellMatchesTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    withStartedOracle(TargetDamageTest::image(), [](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            const trace::TargetScenario drawn = trace::drawTargetScenario(rng);
            const TargetDamageResult port = trace::portTargetDamage(drawn.scenario, drawn.target);
            const TargetDamageResult guest = oracle.targetDamage(drawn.scenario, drawn.target);
            ASSERT_EQ(trace::differences(port, guest), "")
                << "draw " << index << ": " << trace::describe(drawn);
        }
    });
}

}  // namespace
}  // namespace ostinato::test
