// An attack's statuses on one target over drawn attacks (docs/contracts/battle_status.md): every bit of
// the attack's record and of the target drawn at random, the port against the cartridge
// (tools/damage-trace/src/oracle/damage_oracle.h).
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <random>
#include <string>

#include "oracle/damage_oracle.h"
#include "status_effect_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::DamageOracle;

// Every bit of the attack's record and of the target drawn at random: the port and the cartridge
// agree on every draw.
TEST_F(StatusEffectTest, AgreesWithTheCartridgeOverDrawnAttacks) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        std::mt19937 rng{trace::kSweepSeed};
        for (int draw = 0; draw < 500; ++draw) {
            const trace::TargetScenario drawn = trace::drawStatusEffectScenario(rng);
            const std::string apart = trace::differences(trace::portStatusEffect(drawn.scenario, drawn.target),
                                                         oracle.statusEffect(drawn.scenario, drawn.target));
            ASSERT_EQ(apart, "") << "draw " << draw;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
