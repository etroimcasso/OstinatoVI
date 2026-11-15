// The damage taken and shown over drawn scenarios (docs/contracts/battle_damage.md): 500 scenarios
// drawn at random, every cell the routine writes compared between the port and the cartridge's own
// routine through the damage oracle (tools/damage-trace/src/oracle/damage_oracle.h).
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <random>

#include "apply_damage_cases.h"
#include "battle/damage.h"
#include "oracle/damage_oracle.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::ApplyDamageResult;
using trace::DamageScenario;

constexpr int kSweepDraws = 500;

TEST_F(ApplyDamageTest, ApplyDamageSweep_EveryCellMatchesTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    withStartedOracle(ApplyDamageTest::image(), [](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            const DamageScenario drawn = trace::drawApplyScenario(rng);
            const ApplyDamageResult port = trace::portApplyDamage(drawn);
            const ApplyDamageResult guest = oracle.applyDamage(drawn);
            ASSERT_EQ(trace::differences(port, guest), "") << "draw " << index;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
