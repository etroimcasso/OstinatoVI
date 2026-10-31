// The damage oracle's own tests: the cartridge is started as far as its reset code, each routine is
// called in the mode and frame the battle program calls it in, the watches see what a routine
// writes, a scenario written for the guest reads back as written, and an image without the routines
// is refused.

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "assets/cartridge.h"
#include "battle/damage.h"
#include "damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "ostinato/item_effects.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::DamageOracle;
using trace::DamageScenario;
using trace::MultiplierResult;
using trace::WatchedWrite;

class DamageOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* DamageOracleTest::image_ = nullptr;

// The start ends at the reset code.
TEST_F(DamageOracleTest, TheMachineIsParkedAtTheResetCode) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        EXPECT_TRUE(oracle.atReset());
    });
}

// The magic formula at power 4, magic stat 0, level 1: four times the power and nothing from the
// product. The narrowest call there is, and the first proof a routine is called and returns.
TEST_F(DamageOracleTest, CalcMagicDmgReturnsFourTimesThePowerAtStatZero) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        EXPECT_EQ(oracle.magicDamage(4, 0, 1), 16);
    });
}

// 4 × 100 = 400 needs the routine's own 16-bit accumulator: the machine has to have left emulation
// mode, which only the game's reset code does.
TEST_F(DamageOracleTest, CalcMagicDmgWidensItsAccumulator) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        EXPECT_EQ(oracle.magicDamage(100, 0, 1), 400);
    });
}

// The product goes through the multiplication hardware: 10 × 64 × 10 / 32 = 200, on 40.
TEST_F(DamageOracleTest, CalcMagicDmgUsesTheMultiplier) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        EXPECT_EQ(oracle.magicDamage(10, 64, 10), 240);
    });
}

// The watch on the damage word sees the routine's own store of it, low byte then high byte.
TEST_F(DamageOracleTest, TheWatchSeesTheDamageWordWritten) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        ASSERT_EQ(oracle.magicDamage(100, 0, 1), 400);
        const std::vector<WatchedWrite>& writes = oracle.writes();
        ASSERT_GE(writes.size(), 2u);
        const WatchedWrite& low = writes[writes.size() - 2];
        const WatchedWrite& high = writes.back();
        EXPECT_EQ(low.at, DamageOracle::kDamage);
        EXPECT_EQ(low.value, 400 & 0xFF);
        EXPECT_EQ(high.at, DamageOracle::kDamage + 1);
        EXPECT_EQ(high.value, 400 >> 8);
    });
}

// The relic word sits at $3c44, past the 8 KB of work RAM every bank reaches: the bonus is only
// seen with the data bank at $7E.
TEST_F(DamageOracleTest, RelicDmgEffectReadsTheBattlersCellsInBank7E) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        DamageScenario scenario;
        scenario.attack.record.traits.set(AttackTrait::PHYSICAL);
        scenario.attacker = BattlerSlot::character(2);
        scenario.state.at(scenario.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_EQ(oracle.relicBonus(scenario, 400), 500);
    });
}

// The multiplier's steps sit in the direct page; they are read and used up, and the watch on them
// sees the routine zero them.
TEST_F(DamageOracleTest, ApplyDmgMultReadsAndClearsTheSteps) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        DamageScenario scenario;
        scenario.attack.damage = 100;
        scenario.attack.multiplier = 2;
        const MultiplierResult result = oracle.applyMultiplier(scenario);
        EXPECT_EQ(result.damage, 200);
        EXPECT_EQ(result.stepsLeft, 0);
        ASSERT_EQ(oracle.writes().size(), 1u);
        EXPECT_EQ(oracle.writes().front().at, DamageOracle::kMultiplier);
        EXPECT_EQ(oracle.writes().front().value, 0);
    });
}

// A scenario written for the guest reads back from the cells it was written to.
TEST_F(DamageOracleTest, AScenarioReadsBackFromItsCells) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        DamageScenario scenario;
        scenario.attack.record.power = 150;
        scenario.attack.attackerStat = 140;
        scenario.attack.attackerLevel = 99;
        scenario.attacker = BattlerSlot::monster(1);
        scenario.state.at(scenario.attacker).relicEffects3.set(RelicEffect3::RAISE_MAGIC_DAMAGE);
        (void)oracle.relicBonus(scenario, 1000);
        const auto bytes = [&](std::uint32_t at, std::uint32_t size) {
            return oracle.machine().read(retropp::MemoryRegion{.at = at, .size = size});
        };
        EXPECT_EQ(bytes(DamageOracle::kPower, 1)[0], 150);
        EXPECT_EQ(bytes(DamageOracle::kAttackerStat, 2), (std::vector<std::uint8_t>{140, 99}));
        // Monster 1 is battler 5: its relic word is the sixth, ten bytes in.
        const std::uint32_t relicWord = DamageOracle::kRelicEffects1And3 + 2 * scenario.attacker.index();
        EXPECT_EQ(bytes(relicWord, 2),
                  (std::vector<std::uint8_t>{0x00, static_cast<std::uint8_t>(RelicEffect3::RAISE_MAGIC_DAMAGE)}));
    });
}

// The same draws come out of the same seed, so a draw a report names can be run again alone.
TEST_F(DamageOracleTest, TheSweepsDrawsRepeat) {
    trace::runOnLoop([&] {
        std::mt19937 first{trace::kSweepSeed};
        std::mt19937 second{trace::kSweepSeed};
        for (int draw = 0; draw < 10; ++draw) {
            EXPECT_EQ(trace::describe(trace::drawScenario(first)), trace::describe(trace::drawScenario(second)));
        }
    });
}

// An image that is not this game is refused before anything is called.
TEST_F(DamageOracleTest, AnImageWithoutTheRoutinesIsRefused) {
    trace::runOnLoop([&] {
        std::vector<std::uint8_t> other = *image_;
        // The first byte of CalcDmg, in the image's own bytes: $C2:2B9D is offset $022B9D of a HiROM image.
        other[0x022B9D] ^= 0xFF;
        EXPECT_THROW(DamageOracle{assets::stripCopierHeader(other)}, std::runtime_error);
    });
}

}  // namespace
}  // namespace ostinato::test
