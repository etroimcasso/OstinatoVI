// The spell after the attack on the oracle (docs/contracts/battle_damage.md): a bounce aimed at the
// other side only over every random-number position, sent to the same battler by the port and the
// cartridge's own routine (tools/damage-trace/src/oracle/damage_oracle.h); and 500 scenarios drawn at
// random, every cell the routine writes compared between the two.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <random>

#include "battle/damage.h"
#include "oracle/damage_oracle.h"
#include "reflected_attack_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::DamageScenario;
using trace::ReflectedAttackResult;

TEST_F(ReflectedAttackTest, BounceAimedAtTheOtherSideOnly_GoesBackToTheCasterOnHalfTheDraws) {
    OSTINATO_SKIP_WHEN_BRIEF();
    withStartedOracle(ReflectedAttackTest::image(), [](trace::DamageOracle& oracle) {
        const Cartridge cartridge{oracle};
        // Bounced off the foe with the hero still standing, a spell aimed at the other side only goes
        // to the hero on tails and to either party member on heads. Over every random-number
        // position the port and the cartridge send it to the same battler, and both outcomes happen.
        int toTheCaster = 0;
        int toTheAlly = 0;
        for (int position = 0; position < 256; ++position) {
            DamageScenario s = bounce(kHero, battle::BattlerSet::of(kFoe));
            s.attack.rules.opposingSideOnly = true;
            s.state.rng = battle::BattleRng{static_cast<std::uint8_t>(position)};
            const Both both = reflect(cartridge, s);
            toTheCaster += both.guest->applied.hp[at(kHero)] < 5000 ? 1 : 0;
            toTheAlly += both.guest->applied.hp[at(kAlly)] < 5000 ? 1 : 0;
        }
        EXPECT_GT(toTheCaster, toTheAlly);
        EXPECT_GT(toTheAlly, 0);
        EXPECT_EQ(toTheCaster + toTheAlly, 256);
    });
}

constexpr int kSweepDraws = 500;

TEST_F(ReflectedAttackTest, ReflectedAttackSweep_EveryCellMatchesTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    withStartedOracle(ReflectedAttackTest::image(), [](trace::DamageOracle& oracle) {
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            const DamageScenario drawn = trace::drawReflectScenario(rng);
            const ReflectedAttackResult port = trace::portReflectedAttack(drawn);
            const ReflectedAttackResult guest = oracle.reflectedAttack(drawn);
            ASSERT_EQ(trace::differences(port, guest), "") << "draw " << index;
        }
    });
}

}  // namespace
}  // namespace ostinato::test
