// An attack's statuses on one target (docs/contracts/battle_status.md, "An attack's statuses on one
// target"): what the attack sets and clears by its mode, the two rules that take Invisible and
// Frozen off, Vanish not applying, Life on the undead, the statuses added to what is already
// waiting, and the miss for a target immune to all of it. Each case is worked out by hand from the
// original and also run on the cartridge (tools/damage-trace/damage_oracle.h); the port must match
// both.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/status.h"
#include "damage_oracle.h"
#include "data/attack_properties.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/element_set.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::kAllStatuses;
using battle::BattlerSlot;
using battle::StatusChange;
using trace::DamageOracle;
using trace::DamageScenario;
using trace::StatusEffectResult;

constexpr AttackFlags2 kSetMode{};
constexpr AttackFlags2 kRemoveMode = AttackFlags2::of(AttackFlag2::REMOVE_STATUS);
constexpr AttackFlags2 kToggleMode = AttackFlags2::of(AttackFlag2::TOGGLE_STATUS);
constexpr AttackFlags2 kBothModes = AttackFlags2::of(AttackFlag2::REMOVE_STATUS, AttackFlag2::TOGGLE_STATUS);

constexpr BattlerSlot kCaster = BattlerSlot::character(0);
constexpr BattlerSlot kTarget = BattlerSlot::monster(1);

class StatusEffectTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // The attack's statuses on `target` by the port, checked against the cartridge's own answer.
    static StatusEffectResult checked(const DamageScenario& scenario, BattlerSlot target = kTarget) {
        const StatusEffectResult port = trace::portStatusEffect(scenario, target);
        StatusEffectResult guest;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            guest = oracle.statusEffect(scenario, target);
        });
        EXPECT_EQ(trace::differences(port, guest), "");
        return port;
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* StatusEffectTest::image_ = nullptr;

// A scenario with a spell that puts `statuses` on, or takes them off by `flags2`, and a target that
// can be given anything and has `current`.
DamageScenario spellOn(const StatusSet& current, const StatusSet& statuses, AttackFlags2 flags2 = kSetMode) {
    DamageScenario scenario;
    scenario.attacker = kCaster;
    scenario.attack.record.statuses = statuses;
    scenario.attack.record.flags2 = flags2;
    scenario.state.at(kTarget).present = true;
    scenario.state.at(kTarget).status = current;
    return scenario;
}

// --- what the attack sets and clears ------------------------------------------------------------

TEST(StatusModification, SetModeSetsOnlyWhatTheTargetLacks) {
    const StatusChange change =
        battle::statusModification(StatusSet::of(StatusId::POISON), StatusSet::of(StatusId::POISON, StatusId::BLIND),
                                   kSetMode, false, false);
    EXPECT_EQ(change.toSet.bytes, StatusSet::of(StatusId::BLIND).bytes);
    EXPECT_EQ(change.toClear.bytes, StatusSet{}.bytes);
}

TEST(StatusModification, SetModeOnATargetThatHasThemAllSetsNothing) {
    const StatusSet both = StatusSet::of(StatusId::POISON, StatusId::SLOW);
    const StatusChange change = battle::statusModification(both, both, kSetMode, true, false);
    EXPECT_EQ(change.toSet.bytes, StatusSet{}.bytes);
    EXPECT_EQ(change.toClear.bytes, StatusSet{}.bytes);
}

TEST(StatusModification, RemoveModeClearsOnlyWhatTheTargetHas) {
    const StatusChange change =
        battle::statusModification(StatusSet::of(StatusId::POISON, StatusId::STOP),
                                   StatusSet::of(StatusId::POISON, StatusId::BLIND, StatusId::STOP), kRemoveMode, true,
                                   false);
    EXPECT_EQ(change.toSet.bytes, StatusSet{}.bytes);
    EXPECT_EQ(change.toClear.bytes, StatusSet::of(StatusId::POISON, StatusId::STOP).bytes);
}

TEST(StatusModification, ToggleModeSetsWhatItLacksAndClearsWhatItHas) {
    const StatusChange change = battle::statusModification(
        StatusSet::of(StatusId::IMP), StatusSet::of(StatusId::IMP, StatusId::FLOAT), kToggleMode, true, false);
    EXPECT_EQ(change.toSet.bytes, StatusSet::of(StatusId::FLOAT).bytes);
    EXPECT_EQ(change.toClear.bytes, StatusSet::of(StatusId::IMP).bytes);
}

// No attack carries both bits; the port treats one that did as a toggle.
TEST(StatusModification, BothModeBitsActAsAToggle) {
    const StatusChange change = battle::statusModification(
        StatusSet::of(StatusId::IMP), StatusSet::of(StatusId::IMP, StatusId::FLOAT), kBothModes, true, false);
    EXPECT_EQ(change.toSet.bytes, StatusSet::of(StatusId::FLOAT).bytes);
    EXPECT_EQ(change.toClear.bytes, StatusSet::of(StatusId::IMP).bytes);
}

TEST(StatusModification, NoAttackInTheGameCarriesBothModeBits) {
    for (const AttackPropertiesEntry& entry : attackPropertiesEn()) {
        EXPECT_FALSE(entry.record.flags2.has(AttackFlag2::REMOVE_STATUS) &&
                     entry.record.flags2.has(AttackFlag2::TOGGLE_STATUS))
            << static_cast<int>(entry.id);
    }
}

TEST(StatusModification, ASpellTakesInvisibleOff) {
    const StatusChange change =
        battle::statusModification(StatusSet::of(StatusId::INVISIBLE), StatusSet{}, kSetMode, false, false);
    EXPECT_EQ(change.toClear.bytes, StatusSet::of(StatusId::INVISIBLE).bytes);
}

TEST(StatusModification, ABlowLeavesInvisibleOn) {
    const StatusChange change =
        battle::statusModification(StatusSet::of(StatusId::INVISIBLE), StatusSet{}, kSetMode, true, false);
    EXPECT_EQ(change.toClear.bytes, StatusSet{}.bytes);
}

// A spell whose own statuses name Invisible does not take it off by the spell rule: Vanish cast on a
// vanished target leaves it as it is.
TEST(StatusModification, ASpellNamingInvisibleLeavesItOn) {
    const StatusChange change = battle::statusModification(
        StatusSet::of(StatusId::INVISIBLE), StatusSet::of(StatusId::INVISIBLE), kSetMode, false, false);
    EXPECT_EQ(change.toSet.bytes, StatusSet{}.bytes);
    EXPECT_EQ(change.toClear.bytes, StatusSet{}.bytes);
}

TEST(StatusModification, FireThawsAFrozenTarget) {
    const StatusChange change =
        battle::statusModification(StatusSet::of(StatusId::FROZEN), StatusSet{}, kSetMode, true, true);
    EXPECT_EQ(change.toClear.bytes, StatusSet::of(StatusId::FROZEN).bytes);
}

TEST(StatusModification, FireNamingFrozenDoesNotThaw) {
    const StatusChange change = battle::statusModification(
        StatusSet::of(StatusId::FROZEN), StatusSet::of(StatusId::FROZEN), kSetMode, true, true);
    EXPECT_EQ(change.toClear.bytes, StatusSet{}.bytes);
}

TEST(StatusModification, WithoutFireFrozenStays) {
    const StatusChange change =
        battle::statusModification(StatusSet::of(StatusId::FROZEN), StatusSet{}, kSetMode, true, false);
    EXPECT_EQ(change.toClear.bytes, StatusSet{}.bytes);
}

TEST(CalcStatus, WhatIsSetGoesOnAndWhatIsClearedComesOff) {
    const StatusSet result =
        battle::calcStatus(StatusSet::of(StatusId::POISON, StatusId::SLOW), StatusSet::of(StatusId::HASTE),
                           StatusSet::of(StatusId::SLOW));
    EXPECT_EQ(result.bytes, StatusSet::of(StatusId::POISON, StatusId::HASTE).bytes);
}

TEST(CalcStatus, AStatusBothSetAndClearedEndsUpOff) {
    const StatusSet result =
        battle::calcStatus(StatusSet{}, StatusSet::of(StatusId::SLEEP), StatusSet::of(StatusId::SLEEP));
    EXPECT_EQ(result.bytes, StatusSet{}.bytes);
}

// --- on the battle, and on the cartridge -----------------------------------------------------------

TEST_F(StatusEffectTest, TheSetsGoIntoThePendingStatuses) {
    DamageScenario scenario = spellOn(StatusSet::of(StatusId::POISON), StatusSet::of(StatusId::POISON, StatusId::SLEEP));
    const StatusEffectResult result = checked(scenario);
    EXPECT_EQ(result.statusesToSet.bytes, StatusSet::of(StatusId::SLEEP).bytes);
    EXPECT_EQ(result.statusesToClear.bytes, StatusSet{}.bytes);
    EXPECT_TRUE(result.missed.empty());
}

TEST_F(StatusEffectTest, WhatIsAlreadyWaitingStays) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::SLOW));
    scenario.state.at(kTarget).statusesToSet = StatusSet::of(StatusId::DEAD);
    scenario.state.at(kTarget).statusesToClear = StatusSet::of(StatusId::SLEEP);
    const StatusEffectResult result = checked(scenario);
    EXPECT_EQ(result.statusesToSet.bytes, StatusSet::of(StatusId::DEAD, StatusId::SLOW).bytes);
    EXPECT_EQ(result.statusesToClear.bytes, StatusSet::of(StatusId::SLEEP).bytes);
}

TEST_F(StatusEffectTest, ARemoveSpellClearsWhatTheTargetHas) {
    DamageScenario scenario =
        spellOn(StatusSet::of(StatusId::POISON, StatusId::BLIND), StatusSet::of(StatusId::POISON), kRemoveMode);
    const StatusEffectResult result = checked(scenario);
    EXPECT_EQ(result.statusesToSet.bytes, StatusSet{}.bytes);
    EXPECT_EQ(result.statusesToClear.bytes, StatusSet::of(StatusId::POISON).bytes);
}

TEST_F(StatusEffectTest, ASpellTakesInvisibleOffItsTarget) {
    DamageScenario scenario = spellOn(StatusSet::of(StatusId::INVISIBLE), StatusSet{});
    EXPECT_EQ(checked(scenario).statusesToClear.bytes, StatusSet::of(StatusId::INVISIBLE).bytes);
}

TEST_F(StatusEffectTest, ABlowLeavesItsTargetInvisible) {
    DamageScenario scenario = spellOn(StatusSet::of(StatusId::INVISIBLE), StatusSet{});
    scenario.attack.record.traits.set(AttackTrait::PHYSICAL);
    EXPECT_EQ(checked(scenario).statusesToClear.bytes, StatusSet{}.bytes);
}

// With Vanish not applying, Invisible stays — whether the spell rule or the attack's own remove mode
// would have taken it off.
TEST_F(StatusEffectTest, WhenVanishDoesNotApplyInvisibleStays) {
    DamageScenario spell = spellOn(StatusSet::of(StatusId::INVISIBLE), StatusSet{});
    spell.attack.vanishApplies = false;
    EXPECT_EQ(checked(spell).statusesToClear.bytes, StatusSet{}.bytes);

    DamageScenario remove = spellOn(StatusSet::of(StatusId::INVISIBLE, StatusId::POISON),
                                    StatusSet::of(StatusId::INVISIBLE, StatusId::POISON), kRemoveMode);
    remove.attack.vanishApplies = false;
    EXPECT_EQ(checked(remove).statusesToClear.bytes, StatusSet::of(StatusId::POISON).bytes);
}

TEST_F(StatusEffectTest, FireThawsItsTarget) {
    DamageScenario scenario = spellOn(StatusSet::of(StatusId::FROZEN), StatusSet{});
    scenario.attack.record.element = ElementSet::of(Element::FIRE);
    EXPECT_EQ(checked(scenario).statusesToClear.bytes, StatusSet::of(StatusId::FROZEN).bytes);
}

// Life is a remove-status spell that inverts on the undead and names Dead: on an undead target it is
// a kill.
TEST_F(StatusEffectTest, LifeKillsAnUndeadTarget) {
    DamageScenario scenario;
    scenario.attacker = kCaster;
    scenario.attack.record = getAttackProperties(AttackId::LIFE);
    ASSERT_TRUE(scenario.attack.record.flags2.has(AttackFlag2::REMOVE_STATUS));
    ASSERT_TRUE(scenario.attack.record.traits.has(AttackTrait::INVERT_ON_UNDEAD));
    ASSERT_TRUE(scenario.attack.record.statuses.has(StatusId::DEAD));
    scenario.state.at(kTarget).undead = true;
    EXPECT_EQ(checked(scenario).statusesToSet.bytes, StatusSet::of(StatusId::DEAD).bytes);
}

TEST_F(StatusEffectTest, LifeDoesNotKillTheLiving) {
    DamageScenario scenario;
    scenario.attacker = kCaster;
    scenario.attack.record = getAttackProperties(AttackId::LIFE);
    EXPECT_EQ(checked(scenario).statusesToSet.bytes, StatusSet{}.bytes);
}

// The kill is Dead to set even for an undead target that cannot be given Dead: the immunity is
// applied when the statuses take effect.
TEST_F(StatusEffectTest, LifeOnTheUndeadSetsDeadEvenOnATargetImmuneToIt) {
    DamageScenario scenario;
    scenario.attacker = kCaster;
    scenario.attack.record = getAttackProperties(AttackId::LIFE);
    scenario.state.at(kTarget).undead = true;
    scenario.state.at(kTarget).inflictableStatuses = kAllStatuses.without(StatusSet::of(StatusId::DEAD));
    EXPECT_EQ(checked(scenario).statusesToSet.bytes, StatusSet::of(StatusId::DEAD).bytes);
}

// The undead rule needs the remove mode: in set mode an attack that names Zombie only makes the
// target one.
TEST_F(StatusEffectTest, OnTheUndeadASetModeAttackDoesNotKill) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::ZOMBIE));
    scenario.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
    scenario.state.at(kTarget).undead = true;
    EXPECT_EQ(checked(scenario).statusesToSet.bytes, StatusSet::of(StatusId::ZOMBIE).bytes);
}

TEST_F(StatusEffectTest, OnTheUndeadARemoveAttackNamingNeitherDeadNorZombieDoesNotKill) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::POISON), kRemoveMode);
    scenario.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
    scenario.state.at(kTarget).undead = true;
    EXPECT_EQ(checked(scenario).statusesToSet.bytes, StatusSet{}.bytes);
}

TEST_F(StatusEffectTest, OnTheUndeadARemoveAttackThatDoesNotInvertDoesNotKill) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::DEAD), kRemoveMode);
    scenario.state.at(kTarget).undead = true;
    EXPECT_EQ(checked(scenario).statusesToSet.bytes, StatusSet{}.bytes);
}

TEST_F(StatusEffectTest, OnTheUndeadAZombieNamingRemoveAttackKills) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::ZOMBIE), kRemoveMode);
    scenario.attack.record.traits.set(AttackTrait::INVERT_ON_UNDEAD);
    scenario.state.at(kTarget).undead = true;
    EXPECT_EQ(checked(scenario).statusesToSet.bytes, StatusSet::of(StatusId::DEAD).bytes);
}

// A target immune to everything the attack would change is missed, in both of the frame's sets; the
// attack's sets still go into its pending statuses.
TEST_F(StatusEffectTest, AnImmuneTargetIsMissed) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::POISON, StatusId::STOP));
    scenario.attack.record.misc.set(AttackMiscFlag::MISS_IF_STATUS_IMMUNE);
    scenario.state.at(kTarget).inflictableStatuses = kAllStatuses.without(StatusSet::of(StatusId::POISON, StatusId::STOP));
    const StatusEffectResult result = checked(scenario);
    EXPECT_EQ(result.missedByEffect, BattlerSet::of(kTarget));
    EXPECT_EQ(result.missed, BattlerSet::of(kTarget));
    EXPECT_EQ(result.statusesToSet.bytes, StatusSet::of(StatusId::POISON, StatusId::STOP).bytes);
}

// One status that can change, in either half of the statuses, is enough for a hit.
TEST_F(StatusEffectTest, OneStatusThatCanChangeInEitherHalfIsAHit) {
    for (const StatusId open : {StatusId::POISON, StatusId::STOP}) {
        DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::POISON, StatusId::STOP));
        scenario.attack.record.misc.set(AttackMiscFlag::MISS_IF_STATUS_IMMUNE);
        scenario.state.at(kTarget).inflictableStatuses = StatusSet::of(open);
        const StatusEffectResult result = checked(scenario);
        EXPECT_TRUE(result.missedByEffect.empty()) << static_cast<int>(open);
        EXPECT_TRUE(result.missed.empty()) << static_cast<int>(open);
    }
}

// The test is on what this attack changes, not on what was already waiting: a pending status the
// target can be given does not save it.
TEST_F(StatusEffectTest, TheMissReadsOnlyThisAttack) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::POISON));
    scenario.attack.record.misc.set(AttackMiscFlag::MISS_IF_STATUS_IMMUNE);
    scenario.state.at(kTarget).inflictableStatuses = StatusSet::of(StatusId::SLEEP);
    scenario.state.at(kTarget).statusesToSet = StatusSet::of(StatusId::SLEEP);
    EXPECT_EQ(checked(scenario).missedByEffect, BattlerSet::of(kTarget));
}

TEST_F(StatusEffectTest, WithoutTheFlagAnImmuneTargetIsHit) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::POISON));
    scenario.state.at(kTarget).inflictableStatuses = StatusSet{};
    EXPECT_TRUE(checked(scenario).missedByEffect.empty());
}

// A miss adds the target to the battlers already shown "Miss" and keeps them.
TEST_F(StatusEffectTest, TheMissAddsToThoseAlreadyMissed) {
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::POISON));
    scenario.attack.record.misc.set(AttackMiscFlag::MISS_IF_STATUS_IMMUNE);
    scenario.state.at(kTarget).inflictableStatuses = StatusSet{};
    scenario.attack.scratch.missed = BattlerSet::of(BattlerSlot::monster(3));
    EXPECT_EQ(checked(scenario).missed, BattlerSet::of(BattlerSlot::monster(3)) | BattlerSet::of(kTarget));
}

TEST_F(StatusEffectTest, AToggleOnAPartyMemberDoesBoth) {
    const BattlerSlot member = BattlerSlot::character(2);
    DamageScenario scenario = spellOn(StatusSet{}, StatusSet::of(StatusId::IMP, StatusId::FLOAT), kToggleMode);
    scenario.state.at(member).status = StatusSet::of(StatusId::IMP);
    const StatusEffectResult result = checked(scenario, member);
    EXPECT_EQ(result.statusesToSet.bytes, StatusSet::of(StatusId::FLOAT).bytes);
    EXPECT_EQ(result.statusesToClear.bytes, StatusSet::of(StatusId::IMP).bytes);
}

// Every bit of the attack's record and of the target drawn at random: the port and the cartridge
// agree on every draw.
TEST_F(StatusEffectTest, AgreesWithTheCartridgeOverDrawnAttacks) {
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
