// Every battler's waiting statuses taking effect over drawn battles (docs/contracts/battle_status.md):
// every bit of every battler's statuses, pending statuses, immunities and flags drawn at random, the
// port against the cartridge (tools/damage-trace/src/oracle/damage_oracle.h).
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <random>
#include <string>

#include "oracle/damage_oracle.h"
#include "status_changes_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::DamageOracle;
using trace::DamageScenario;

// Every bit of every battler's statuses, pending statuses, immunities and flags drawn at random: the
// port and the cartridge agree on every draw.
TEST_F(StatusChangesTest, AgreesWithTheCartridgeOverDrawnBattles) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        std::mt19937 rng{trace::kSweepSeed};
        for (int draw = 0; draw < 500; ++draw) {
            const DamageScenario scenario = trace::drawStatusChangesScenario(rng);
            const std::string apart =
                trace::differences(trace::portStatusChanges(scenario), oracle.statusChanges(scenario));
            ASSERT_EQ(apart, "") << "draw " << draw;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
