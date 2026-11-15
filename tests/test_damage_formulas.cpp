// The damage formulas: a spell's and a blow's base damage, the relic bonus and the multiplier. Each
// case is worked out three ways — the port, by hand from the original, and the cartridge's own
// routine through the damage oracle where the run asks it (tests/with_cartridge.h) — and all three
// must agree. The seeded sweeps, the port against the cartridge alone, are in
// tests/oracle/test_damage_formulas.cpp.
//
// Authority: docs/contracts/battle_damage.md.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include "battle/attack.h"
#include "battle/damage.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_flags.h"
#include "ostinato/item_effects.h"
#include "damage_formulas_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::DamageScenario;
using trace::MultiplierResult;

// The three-way check: the port's answer, the value worked out by hand, and the cartridge's where the
// run asks it (an empty optional where it does not).
#define EXPECT_THREE_WAYS(port, hand, guest)                                                     \
    do {                                                                                        \
        const int port_ = (port);                                                               \
        const int hand_ = (hand);                                                               \
        const std::optional<int> guest_ = (guest);                                              \
        EXPECT_EQ(port_, hand_) << "the port disagrees with the hand trace";                    \
        if (guest_) {                                                                           \
            EXPECT_EQ(*guest_, hand_) << "the cartridge disagrees with the hand trace";          \
            EXPECT_EQ(port_, *guest_) << "the port disagrees with the cartridge";                \
        }                                                                                       \
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

// The cartridge's answers, each empty where the run does not ask it.
std::optional<int> cartridgeMagicDamage(const Cartridge& cartridge, std::uint8_t power, std::uint8_t magicStat,
                                        std::uint8_t level) {
    return cartridge ? std::optional<int>{cartridge.oracle().magicDamage(power, magicStat, level)} : std::nullopt;
}

std::optional<int> cartridgeCalcDamage(const Cartridge& cartridge, const DamageScenario& s) {
    return cartridge ? std::optional<int>{cartridge.oracle().calcDamage(s)} : std::nullopt;
}

std::optional<int> cartridgeRelicBonus(const Cartridge& cartridge, const DamageScenario& s, std::uint16_t damage) {
    return cartridge ? std::optional<int>{cartridge.oracle().relicBonus(s, damage)} : std::nullopt;
}

// The cartridge's base damage: its damage routine, then its relic bonus on what that gave.
std::optional<int> cartridgeBaseDamage(const Cartridge& cartridge, const DamageScenario& s) {
    if (!cartridge) {
        return std::nullopt;
    }
    return cartridge.oracle().relicBonus(s, cartridge.oracle().calcDamage(s));
}

std::optional<MultiplierResult> cartridgeMultiplier(const Cartridge& cartridge, const DamageScenario& s) {
    return cartridge ? std::optional<MultiplierResult>{cartridge.oracle().applyMultiplier(s)} : std::nullopt;
}

// The damage of the cartridge's multiplier, where it was asked.
std::optional<int> damageOf(const std::optional<MultiplierResult>& guest) {
    return guest ? std::optional<int>{guest->damage} : std::nullopt;
}

std::optional<int> cartridgeMultipliedDamage(const Cartridge& cartridge, const DamageScenario& s) {
    return damageOf(cartridgeMultiplier(cartridge, s));
}

// The cartridge's multiplier left `steps` steps, where it was asked.
::testing::AssertionResult theCartridgeLeaves(const std::optional<MultiplierResult>& guest, int steps) {
    if (!guest || guest->stepsLeft == steps) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "the cartridge left " << guest->stepsLeft << " steps";
}

// The cartridge's last call wrote none of the cells the oracle watches, where it was asked.
::testing::AssertionResult theCartridgeWroteNothing(const Cartridge& cartridge) {
    if (!cartridge || cartridge.oracle().writes().empty()) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "the cartridge wrote " << cartridge.oracle().writes().size() << " cells";
}

// ─── magicDamage ─────────────────────────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, MagicDamage_FourTimesThePowerAtLevelOneWithNoStat) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        EXPECT_THREE_WAYS(battle::magicDamage(4, 0, 1), 16, cartridgeMagicDamage(cartridge, 4, 0, 1));
    });
}

TEST_F(DamageFormulaTest, MagicDamage_ThePowerAloneAtLevelZero) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        EXPECT_THREE_WAYS(battle::magicDamage(50, 100, 0), 50, cartridgeMagicDamage(cartridge, 50, 100, 0));
    });
}

TEST_F(DamageFormulaTest, MagicDamage_TheProductIsDividedByThirtyTwo) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // 40 + 10 × 64 × 10 / 32 = 40 + 200.
        EXPECT_THREE_WAYS(battle::magicDamage(10, 64, 10), 240, cartridgeMagicDamage(cartridge, 10, 64, 10));
    });
}

TEST_F(DamageFormulaTest, MagicDamage_TheDividedProductKeepsSixteenBits) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // 255 × 255 × 255 / 32 = 518,167, whose low sixteen bits are 59,415; plus 1,020.
        EXPECT_THREE_WAYS(battle::magicDamage(255, 255, 255), 60435, cartridgeMagicDamage(cartridge, 255, 255, 255));
    });
}

TEST_F(DamageFormulaTest, MagicDamage_WrapsPastSixtyFiveThousand) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // Ultima (150) with a magic stat of 140 at level 99: 600 + 2,079,000 / 32 = 65,568, which wraps
        // to 32 (docs/Bugs.md).
        EXPECT_THREE_WAYS(battle::magicDamage(150, 140, 99), 32, cartridgeMagicDamage(cartridge, 150, 140, 99));
        // One point of stat less stays under the wrap.
        EXPECT_THREE_WAYS(battle::magicDamage(150, 139, 99), 65104, cartridgeMagicDamage(cartridge, 150, 139, 99));
    });
}

// ─── physicalDamage ──────────────────────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, PhysicalDamage_APartyMembersBlow) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // (100 + 50) × 10 × 10 / 256 = 58; then (200 + 58) / 2 + 58.
        const DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        EXPECT_THREE_WAYS(portBlow(s), 187, cartridgeCalcDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AMonstersBlowCountsItsPowerFourTimes) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // (400 + 50) × 10 × 10 / 256 = 175, and no party-member step.
        const DamageScenario s = blow(BattlerSlot::monster(0), 100, 50, 10);
        EXPECT_THREE_WAYS(portBlow(s), 175, cartridgeCalcDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_TheVigorCarriesIntoTheHighByte) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // 200 + 100 = 300: (300 × 10 × 10) / 256 = 117; (400 + 117) / 2 + 117. A blow that lost the carry
        // would start from 44 and come to 225.
        const DamageScenario s = blow(BattlerSlot::character(1), 200, 100, 10);
        EXPECT_THREE_WAYS(portBlow(s), 375, cartridgeCalcDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AWeaponInBothHandsCountsSevenQuartersOfItsPower) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // 100 → ((50 + 100) / 2 = 75) + 100 = 175; (175 + 50) × 100 / 256 = 87; (200 + 87) / 2 + 87.
        DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        s.attack.twoHanded = true;
        EXPECT_THREE_WAYS(portBlow(s), 230, cartridgeCalcDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AnOfferingHalvesIt) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        EXPECT_THREE_WAYS(portBlow(s), 93, cartridgeCalcDamage(cartridge, s));
    });
}

// At level 251 the half plus the damage passes 65,535 and keeps 16 bits before the Offering halves
// it: 510 × 251 keeps 62,474, the blow is 61,253, and 30,881 + 61,253 keeps 26,598.
TEST_F(DamageFormulaTest, PhysicalDamage_AnOfferingHalvesTheSumAsItWraps) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::character(0), 255, 255, 251);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        EXPECT_THREE_WAYS(portBlow(s), 13299, cartridgeCalcDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AGenjiGloveWithOneWeaponTakesAQuarterOff) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // 187 − 46.
        DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::EQUIP_2_WEAPONS);
        EXPECT_THREE_WAYS(portBlow(s), 141, cartridgeCalcDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AnOfferingAndAGenjiGloveTogether) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // 93 − 23.
        DamageScenario s = blow(BattlerSlot::character(3), 100, 50, 10);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::EQUIP_2_WEAPONS);
        EXPECT_THREE_WAYS(portBlow(s), 70, cartridgeCalcDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_AMonsterIgnoresThePartyMembersRelicBits) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::monster(2), 100, 50, 10);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
        s.state.at(s.attacker).relicEffects4.set(RelicEffect4::EQUIP_2_WEAPONS);
        EXPECT_THREE_WAYS(portBlow(s), 175, cartridgeCalcDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, PhysicalDamage_TheFirstProductByTheLevelKeepsSixteenBits) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        // Power 255 in both hands is 446; with a vigor stat of 255, 701 × 99 = 69,399, kept as 3,863;
        // 3,863 × 99 / 256 = 1,493; (510 + 1,493) / 2 + 1,493. Without the cut it would be 40,506
        // (docs/Bugs.md).
        DamageScenario s = blow(BattlerSlot::character(0), 255, 255, 99);
        s.attack.twoHanded = true;
        EXPECT_THREE_WAYS(portBlow(s), 2494, cartridgeCalcDamage(cartridge, s));
        // One level less stays under it: 701 × 98 = 68,698, kept as 3,162; 3,162 × 98 / 256 = 1,210;
        // (510 + 1,210) / 2 + 1,210.
        s.attack.attackerLevel = 98;
        EXPECT_THREE_WAYS(portBlow(s), 2070, cartridgeCalcDamage(cartridge, s));
    });
}

// ─── relicBonus ──────────────────────────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, RelicBonus_ABlowGainsAQuarterFromRaiseFightDamage) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 500, cartridgeRelicBonus(cartridge, s, 400));
        EXPECT_THREE_WAYS(portBonus(s, 401), 501, cartridgeRelicBonus(cartridge, s, 401));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_ABlowGainsNothingFromTheMagicBits) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        s.state.at(s.attacker).relicEffects3.set(RelicEffect3::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 400, cartridgeRelicBonus(cartridge, s, 400));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_ASpellGainsHalfFromTwoItemsAndAQuarterFromOne) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attacker = BattlerSlot::character(2);
        s.state.at(s.attacker).relicEffects3.set(RelicEffect3::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 500, cartridgeRelicBonus(cartridge, s, 400));
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 600, cartridgeRelicBonus(cartridge, s, 400));
        EXPECT_THREE_WAYS(portBonus(s, 3), 4, cartridgeRelicBonus(cartridge, s, 3));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_ASpellGainsNothingFromRaiseFightDamage) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 400, cartridgeRelicBonus(cartridge, s, 400));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_AnAttackOnMpTakesTheMagicPathWhateverItsTrait) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 400, cartridgeRelicBonus(cartridge, s, 400));
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 600, cartridgeRelicBonus(cartridge, s, 400));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_AnAttackThatRestoresGainsNothing) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.attack.record.flags2.set(AttackFlag2::RESTORE_HP_MP);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_MAGIC_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 400), 400, cartridgeRelicBonus(cartridge, s, 400));
    });
}

TEST_F(DamageFormulaTest, RelicBonus_StopsAtSixtyFiveThousandFiveHundredAndThirtyFive) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::character(0), 0, 0, 0);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        EXPECT_THREE_WAYS(portBonus(s, 60000), 65535, cartridgeRelicBonus(cartridge, s, 60000));
        EXPECT_THREE_WAYS(portBonus(s, 52428), 65535, cartridgeRelicBonus(cartridge, s, 52428));
        EXPECT_THREE_WAYS(portBonus(s, 52427), 65533, cartridgeRelicBonus(cartridge, s, 52427));
    });
}

// ─── baseDamage ──────────────────────────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, BaseDamage_ABlowThenItsBonus) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s = blow(BattlerSlot::character(0), 100, 50, 10);
        s.state.at(s.attacker).relicEffects1.set(RelicEffect1::RAISE_FIGHT_DAMAGE);
        battle::baseDamage(s.state, s.attack, s.attacker);
        // 187, then 187 + 46.
        EXPECT_THREE_WAYS(s.attack.damage, 233, cartridgeBaseDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, BaseDamage_ASpellThenItsBonus) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attack.record.power = 10;
        s.attack.attackerStat = 64;
        s.attack.attackerLevel = 10;
        s.state.at(s.attacker).relicEffects3.set(RelicEffect3::RAISE_MAGIC_DAMAGE);
        battle::baseDamage(s.state, s.attack, s.attacker);
        // 240, then 240 + 60.
        EXPECT_THREE_WAYS(s.attack.damage, 300, cartridgeBaseDamage(cartridge, s));
    });
}

// ─── applyMultiplier and scaleDamage ─────────────────────────────────────────────────────────────

TEST_F(DamageFormulaTest, DamageMultiplier_EachStepAddsHalfTheDamage) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attack.damage = 100;
        for (int steps = 0; steps <= 3; ++steps) {
            s.attack.multiplier = steps;
            const std::optional<MultiplierResult> guest = cartridgeMultiplier(cartridge, s);
            EXPECT_THREE_WAYS(battle::applyMultiplier(100, steps), 100 + 50 * steps, damageOf(guest));
            EXPECT_TRUE(theCartridgeLeaves(guest, 0)) << steps;
        }
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_HalfIsRoundedDownOnce) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attack.damage = 3;
        s.attack.multiplier = 3;
        EXPECT_THREE_WAYS(battle::applyMultiplier(3, 3), 6, cartridgeMultipliedDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_StopsAtSixtyFiveThousandFiveHundredAndThirtyFive) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attack.damage = 40000;
        s.attack.multiplier = 1;
        EXPECT_THREE_WAYS(battle::applyMultiplier(40000, 1), 60000, cartridgeMultipliedDamage(cartridge, s));
        s.attack.multiplier = 2;
        EXPECT_THREE_WAYS(battle::applyMultiplier(40000, 2), 65535, cartridgeMultipliedDamage(cartridge, s));
        s.attack.multiplier = 3;
        EXPECT_THREE_WAYS(battle::applyMultiplier(40000, 3), 65535, cartridgeMultipliedDamage(cartridge, s));
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_NoStepsLeavesTheDamageAndWritesNothing) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attack.damage = 100;
        s.attack.multiplier = 0;
        battle::scaleDamage(s.attack);
        const std::optional<MultiplierResult> guest = cartridgeMultiplier(cartridge, s);
        EXPECT_THREE_WAYS(s.attack.damage, 100, damageOf(guest));
        EXPECT_TRUE(theCartridgeWroteNothing(cartridge));
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_ScaleDamageUsesTheStepsUp) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attack.damage = 100;
        s.attack.multiplier = 2;
        const std::optional<MultiplierResult> guest = cartridgeMultiplier(cartridge, s);
        battle::scaleDamage(s.attack);
        EXPECT_THREE_WAYS(s.attack.damage, 200, damageOf(guest));
        EXPECT_EQ(s.attack.multiplier, 0);
        EXPECT_TRUE(theCartridgeLeaves(guest, 0));
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_AnAttackThatIgnoresDefenseIsNotMultipliedAndKeepsItsSteps) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attack.damage = 100;
        s.attack.multiplier = 2;
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        const std::optional<MultiplierResult> guest = cartridgeMultiplier(cartridge, s);
        battle::scaleDamage(s.attack);
        EXPECT_THREE_WAYS(s.attack.damage, 100, damageOf(guest));
        EXPECT_EQ(s.attack.multiplier, 2);
        EXPECT_TRUE(theCartridgeLeaves(guest, 2));
    });
}

TEST_F(DamageFormulaTest, DamageMultiplier_AThrowMultipliesAnAttackThatIgnoresDefense) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        DamageScenario s;
        s.attack.damage = 100;
        s.attack.multiplier = 2;
        s.attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        s.attack.ignoreDefenseStopsMultiplier = false;
        const std::optional<MultiplierResult> guest = cartridgeMultiplier(cartridge, s);
        battle::scaleDamage(s.attack);
        EXPECT_THREE_WAYS(s.attack.damage, 200, damageOf(guest));
        EXPECT_EQ(s.attack.multiplier, 0);
        EXPECT_TRUE(theCartridgeLeaves(guest, 0));
    });
}

}  // namespace
}  // namespace ostinato::test
