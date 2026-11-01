// The damage formulas: a spell's and a blow's base damage, the relic bonus and the multiplier. Each
// case is worked out three ways — the port, by hand from the original, and the cartridge's own
// routine through the damage oracle — and all three must agree. A seeded sweep then draws scenarios
// at random and checks the port against the cartridge alone.
//
// Authority: docs/contracts/battle_damage.md.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attack.h"
#include "battle/damage.h"
#include "damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "ostinato/item_effects.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::DamageScenario;
using trace::MultiplierResult;

// The cartridge, read once for the binary; every test starts its own oracle on it.
class DamageFormulaTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* DamageFormulaTest::image_ = nullptr;

// The three-way check: the port's answer, the value worked out by hand, and the cartridge's.
#define EXPECT_THREE_WAYS(port, hand, guest)                                                     \
    do {                                                                                        \
        const int port_ = (port);                                                               \
        const int hand_ = (hand);                                                               \
        const int guest_ = (guest);                                                             \
        EXPECT_EQ(port_, hand_) << "the port disagrees with the hand trace";                    \
        EXPECT_EQ(guest_, hand_) << "the cartridge disagrees with the hand trace";               \
        EXPECT_EQ(port_, guest_) << "the port disagrees with the cartridge";                     \
    } while (0)

// A blow by `attacker` with `power`, the attacker's vigor stat and level.
DamageScenario blow(BattlerSlot attacker, std::uint8_t power, std::uint8_t vigor, std::uint8_t level) {
    DamageScenario scenario;
    scenario.attacker = attacker;
    scenario.attack.record.traits.set(AttackTrait::PHYSICAL);
    scenario.attack.record.power = power;
    scenario.attack.attackerStat = vigor;
    scenario.attack.attackerLevel = level;
    return scenario;
}

std::uint16_t portBlow(const DamageScenario& s) {
    return battle::physicalDamage(s.state, s.attack, s.attacker);
}

std::uint16_t portBonus(const DamageScenario& s, std::uint16_t damage) {
    return battle::relicBonus(s.state, s.attack, s.attacker, damage);
}

// ─── magicDamage ─────────────────────────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, MagicDamage_FourTimesThePowerAtLevelOneWithNoStat) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        EXPECT_THREE_WAYS(battle::magicDamage(4, 0, 1), 16, oracle.magicDamage(4, 0, 1));
    });
}

TEST_F(DamageFormulaTest, MagicDamage_ThePowerAloneAtLevelZero) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        EXPECT_THREE_WAYS(battle::magicDamage(50, 100, 0), 50, oracle.magicDamage(50, 100, 0));
    });
}

TEST_F(DamageFormulaTest, MagicDamage_TheProductIsDividedByThirtyTwo) {
    trace::runOnLoop([&] {
        // 40 + 10 × 64 × 10 / 32 = 40 + 200.
        trace::DamageOracle oracle{image()};
        oracle.start();
        EXPECT_THREE_WAYS(battle::magicDamage(10, 64, 10), 240, oracle.magicDamage(10, 64, 10));
    });
}

TEST_F(DamageFormulaTest, MagicDamage_TheDividedProductKeepsSixteenBits) {
    trace::runOnLoop([&] {
        // 255 × 255 × 255 / 32 = 518,167, whose low sixteen bits are 59,415; plus 1,020.
        trace::DamageOracle oracle{image()};
        oracle.start();
        EXPECT_THREE_WAYS(battle::magicDamage(255, 255, 255), 60435, oracle.magicDamage(255, 255, 255));
    });
}

TEST_F(DamageFormulaTest, MagicDamage_WrapsPastSixtyFiveThousand) {
    trace::runOnLoop([&] {
        // Ultima (150) with a magic stat of 140 at level 99: 600 + 2,079,000 / 32 = 65,568, which wraps
        // to 32 (docs/Bugs.md).
        trace::DamageOracle oracle{image()};
        oracle.start();
        EXPECT_THREE_WAYS(battle::magicDamage(150, 140, 99), 32, oracle.magicDamage(150, 140, 99));
        // One point of stat less stays under the wrap.
        EXPECT_THREE_WAYS(battle::magicDamage(150, 139, 99), 65104, oracle.magicDamage(150, 139, 99));
    });
}

// ─── physicalDamage ──────────────────────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, PhysicalDamage_APartyMembersBlow) {
    trace::runOnLoop([&] {
        // (100 + 50) × 10 × 10 / 256 = 58; then (200 + 58) / 2 + 58.
        trace::DamageOracle oracle{image()};
        oracle.start();
        const DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        EXPECT_THREE_WAYS(portBlow(s), 187, oracle.calcDamage(s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AMonstersBlowCountsItsPowerFourTimes) {
    trace::runOnLoop([&] {
        // (400 + 50) × 10 × 10 / 256 = 175, and no party-member step.
        trace::DamageOracle oracle{image()};
        oracle.start();
        const DamageScenario s = blow(BattlerSlot::monster(0), 100, 50, 10);
        EXPECT_THREE_WAYS(portBlow(s), 175, oracle.calcDamage(s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_TheVigorCarriesIntoTheHighByte) {
    trace::runOnLoop([&] {
        // 200 + 100 = 300: (300 × 10 × 10) / 256 = 117; (400 + 117) / 2 + 117. A blow that lost the carry
        // would start from 44 and come to 225.
        trace::DamageOracle oracle{image()};
        oracle.start();
        const DamageScenario s = blow(BattlerSlot::character(1), 200, 100, 10);
        EXPECT_THREE_WAYS(portBlow(s), 375, oracle.calcDamage(s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AWeaponInBothHandsCountsSevenQuartersOfItsPower) {
    trace::runOnLoop([&] {
        // 100 → ((50 + 100) / 2 = 75) + 100 = 175; (175 + 50) × 100 / 256 = 87; (200 + 87) / 2 + 87.
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        s.attack.twoHanded = true;
        EXPECT_THREE_WAYS(portBlow(s), 230, oracle.calcDamage(s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AnOfferingHalvesIt) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        EXPECT_THREE_WAYS(portBlow(s), 93, oracle.calcDamage(s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AGenjiGloveWithOneWeaponTakesAQuarterOff) {
    trace::runOnLoop([&] {
        // 187 − 46.
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::EQUIP_2_WEAPONS);
        EXPECT_THREE_WAYS(portBlow(s), 141, oracle.calcDamage(s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AnOfferingAndAGenjiGloveTogether) {
    trace::runOnLoop([&] {
        // 93 − 23.
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(3), 100, 50, 10);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::EQUIP_2_WEAPONS);
        EXPECT_THREE_WAYS(portBlow(s), 70, oracle.calcDamage(s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AMonsterIgnoresThePartyMembersRelicBits) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::monster(2), 100, 50, 10);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::EQUIP_2_WEAPONS);
        EXPECT_THREE_WAYS(portBlow(s), 175, oracle.calcDamage(s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_TheFirstProductByTheLevelKeepsSixteenBits) {
    trace::runOnLoop([&] {
        // Power 255 in both hands is 446; with a vigor stat of 255, 701 × 99 = 69,399, kept as 3,863;
        // 3,863 × 99 / 256 = 1,493; (510 + 1,493) / 2 + 1,493. Without the cut it would be 40,506
        // (docs/Bugs.md).
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 255, 255, 99);
        s.attack.twoHanded = true;
        EXPECT_THREE_WAYS(portBlow(s), 2494, oracle.calcDamage(s));
        // One level less stays under it: 701 × 98 = 68,698, kept as 3,162; 3,162 × 98 / 256 = 1,210;
        // (510 + 1,210) / 2 + 1,210.
        s.attack.attackerLevel = 98;
        EXPECT_THREE_WAYS(portBlow(s), 2070, oracle.calcDamage(s));
    });
}

// ─── relicBonus ──────────────────────────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, RelicBonus_ABlowGainsAQuarterFromRaiseFightDamage) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 500, oracle.relicBonus(s, 400));
        EXPECT_THREE_WAYS(portBonus(s, 401), 501, oracle.relicBonus(s, 401));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_ABlowGainsNothingFromTheMagicBits) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        s.state.at(s.attacker).relicEffects3.set(RelicEffect3::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 400, oracle.relicBonus(s, 400));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_ASpellGainsHalfFromTwoItemsAndAQuarterFromOne) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attacker = BattlerSlot::character(2);
        s.state.at(s.attacker).relicEffects3.set(RelicEffect3::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 500, oracle.relicBonus(s, 400));
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 600, oracle.relicBonus(s, 400));
        EXPECT_THREE_WAYS(portBonus(s, 3), 4, oracle.relicBonus(s, 3));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_ASpellGainsNothingFromRaiseFightDamage) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 400, oracle.relicBonus(s, 400));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_AnAttackOnMpTakesTheMagicPathWhateverItsTrait) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 400, oracle.relicBonus(s, 400));
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 600, oracle.relicBonus(s, 400));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_AnAttackThatRestoresGainsNothing) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 400, oracle.relicBonus(s, 400));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_StopsAtSixtyFiveThousandFiveHundredAndThirtyFive) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 60000), 65535, oracle.relicBonus(s, 60000));
        EXPECT_THREE_WAYS(portBonus(s, 52428), 65535, oracle.relicBonus(s, 52428));
        EXPECT_THREE_WAYS(portBonus(s, 52427), 65533, oracle.relicBonus(s, 52427));
    });
}

// ─── baseDamage ──────────────────────────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, BaseDamage_ABlowThenItsBonus) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        battle::baseDamage(s.state, s.attack, s.attacker);
        // 187, then 187 + 46.
        EXPECT_THREE_WAYS(s.attack.damage, 233, oracle.relicBonus(s, oracle.calcDamage(s)));
    });
}

TEST_F(DamageFormulaTest, BaseDamage_ASpellThenItsBonus) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attack.record.power = 10;
        s.attack.attackerStat = 64;
        s.attack.attackerLevel = 10;
        s.state.at(s.attacker).relicEffects3.set(RelicEffect3::RAISE_MAGIC_DAMAGE);
        battle::baseDamage(s.state, s.attack, s.attacker);
        // 240, then 240 + 60.
        EXPECT_THREE_WAYS(s.attack.damage, 300, oracle.relicBonus(s, oracle.calcDamage(s)));
    });
}

// ─── applyMultiplier and scaleDamage ─────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, DamageMultiplier_EachStepAddsHalfTheDamage) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attack.damage = 100;
        for (int steps = 0; steps <= 3; ++steps) {
            s.attack.multiplier = steps;
            const trace::MultiplierResult guest = oracle.applyMultiplier(s);
            EXPECT_THREE_WAYS(battle::applyMultiplier(100, steps), 100 + 50 * steps, guest.damage);
            EXPECT_EQ(guest.stepsLeft, 0) << steps;
        }
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_HalfIsRoundedDownOnce) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attack.damage = 3;
        s.attack.multiplier = 3;
        EXPECT_THREE_WAYS(battle::applyMultiplier(3, 3), 6, oracle.applyMultiplier(s).damage);
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_StopsAtSixtyFiveThousandFiveHundredAndThirtyFive) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attack.damage = 40000;
        s.attack.multiplier = 1;
        EXPECT_THREE_WAYS(battle::applyMultiplier(40000, 1), 60000, oracle.applyMultiplier(s).damage);
        s.attack.multiplier = 2;
        EXPECT_THREE_WAYS(battle::applyMultiplier(40000, 2), 65535, oracle.applyMultiplier(s).damage);
        s.attack.multiplier = 3;
        EXPECT_THREE_WAYS(battle::applyMultiplier(40000, 3), 65535, oracle.applyMultiplier(s).damage);
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_NoStepsLeavesTheDamageAndWritesNothing) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attack.damage = 100;
        s.attack.multiplier = 0;
        battle::scaleDamage(s.attack);
        const trace::MultiplierResult guest = oracle.applyMultiplier(s);
        EXPECT_THREE_WAYS(s.attack.damage, 100, guest.damage);
        EXPECT_TRUE(oracle.writes().empty());
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_ScaleDamageUsesTheStepsUp) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attack.damage = 100;
        s.attack.multiplier = 2;
        const trace::MultiplierResult guest = oracle.applyMultiplier(s);
        battle::scaleDamage(s.attack);
        EXPECT_THREE_WAYS(s.attack.damage, 200, guest.damage);
        EXPECT_EQ(s.attack.multiplier, 0);
        EXPECT_EQ(guest.stepsLeft, 0);
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_AnAttackThatIgnoresDefenseIsNotMultipliedAndKeepsItsSteps) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attack.damage = 100;
        s.attack.multiplier = 2;
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        const trace::MultiplierResult guest = oracle.applyMultiplier(s);
        battle::scaleDamage(s.attack);
        EXPECT_THREE_WAYS(s.attack.damage, 100, guest.damage);
        EXPECT_EQ(s.attack.multiplier, 2);
        EXPECT_EQ(guest.stepsLeft, 2);
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_AThrowMultipliesAnAttackThatIgnoresDefense) {
    trace::runOnLoop([&] {
        trace::DamageOracle oracle{image()};
        oracle.start();
        DamageScenario s;
        s.attack.damage = 100;
        s.attack.multiplier = 2;
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        s.attack.ignoreDefenseStopsMultiplier = false;
        const trace::MultiplierResult guest = oracle.applyMultiplier(s);
        battle::scaleDamage(s.attack);
        EXPECT_THREE_WAYS(s.attack.damage, 200, guest.damage);
        EXPECT_EQ(s.attack.multiplier, 0);
        EXPECT_EQ(guest.stepsLeft, 0);
    });
}

// ─── the seeded sweep ────────────────────────────────────────────────────────────────────────────

constexpr int kSweepDraws = 2000;

std::string draw(int index, const DamageScenario& s) {
    return "draw " + std::to_string(index) + ": " + trace::describe(s);
}

TEST_F(DamageFormulaTest, DamageSweep_TheBaseDamageMatchesTheCartridge) {
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
