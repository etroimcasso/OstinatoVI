// The damage formulas over drawn scenarios (docs/contracts/battle_damage.md): the base damage, the
// relic bonus and the multiplier of 2,000 scenarios drawn at random, the port against the cartridge's
// own routines through the damage oracle (tools/damage-trace/src/oracle/damage_oracle.h).
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>

#include "battle/attack.h"
#include "battle/damage.h"
#include "damage_formulas_cases.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::DamageScenario;

constexpr int kSweepDraws = 2000;

std::string draw(int index, const DamageScenario& s) {
    return "draw " + std::to_string(index) + ": " + trace::describe(s);
}

TEST_F(DamageFormulaTest, DamageSweep_TheBaseDamageMatchesTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            const DamageScenario s = trace::drawScenario(rng);
            const std::uint16_t port =
                s.attack.record.traits.has(AttackTrait::PHYSICAL)
                    ? battle::physicalDamage(s.state, s.attack, s.attacker)
                    : battle::magicDamage(s.attack.record.power, s.attack.attackerStat, s.attack.attackerLevel);
            ASSERT_EQ(port, oracle.calcDamage(s)) << draw(index, s);
        }
    });
}

TEST_F(DamageFormulaTest, DamageSweep_TheRelicBonusMatchesTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            const DamageScenario s = trace::drawScenario(rng);
            const auto damage = static_cast<std::uint16_t>(s.attack.damage);
            ASSERT_EQ(battle::relicBonus(s.state, s.attack, s.attacker, damage), oracle.relicBonus(s, damage))
                << draw(index, s);
        }
    });
}

TEST_F(DamageFormulaTest, DamageSweep_TheMultiplierMatchesTheCartridge) {
    OSTINATO_SKIP_WHEN_BRIEF();
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        std::mt19937 rng{trace::kSweepSeed};
        for (int index = 0; index < kSweepDraws; ++index) {
            DamageScenario s = trace::drawScenario(rng);
            const trace::MultiplierResult guest = oracle.applyMultiplier(s);
            battle::scaleDamage(s.attack);
            ASSERT_EQ(s.attack.damage, guest.damage) << draw(index, s);
            ASSERT_EQ(s.attack.multiplier, guest.stepsLeft) << draw(index, s);
        }
    });
}

}  // namespace
}  // namespace ostinato::test
