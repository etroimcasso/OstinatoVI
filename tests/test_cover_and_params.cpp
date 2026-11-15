// The cover and the attack's animation set up, worked out by hand (docs/contracts/battle_commands.md,
// "The attack frame" and "Cover"): a Dragon Horn landing with another to come is marked for the jump's
// animation and aims the next landing at random, and the last landing is not; a bodyguard who is
// controlled or held in Seize does not step in, while one controlling a monster or holding someone
// does, and an ally held in Seize is not covered. Every case is worked out by hand from the original
// and also run on the cartridge's own routine (tools/damage-trace/src/oracle/damage_oracle.h) where the
// run asks it (tests/with_cartridge.h); the port must match both. Both routines on the oracle and the
// drawn scenarios are in tests/oracle/test_cover_and_params.cpp.

#include <gtest/gtest.h>

#include "battle/attack.h"
#include "battle/targeting.h"
#include "cover_and_params_cases.h"
#include "oracle/damage_oracle.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using battle::AttackInFlight;
using battle::BattlerSet;
using battle::BattlerSlot;
using trace::DamageScenario;

constexpr BattlerSlot kTerra = BattlerSlot::character(0);
constexpr BattlerSlot kLocke = BattlerSlot::character(1);
constexpr BattlerSlot kEdgar = BattlerSlot::character(3);
constexpr BattlerSlot kGuard = BattlerSlot::monster(0);
constexpr BattlerSlot kSoldier = BattlerSlot::monster(1);

// The port's animation set up over `scenario`, checked against the cartridge's own answer where the
// run asks it.
AttackInFlight checkedParams(const DamageScenario& scenario) {
    withCartridge(CoverAndParamsTest::image(), [&](const Cartridge& cartridge) {
        if (!cartridge) {
            return;
        }
        EXPECT_EQ(trace::differences(trace::portInitParams(scenario), cartridge.oracle().initParams(scenario)), "");
    });
    DamageScenario after = scenario;
    battle::initParams(after.attack, after.attacker);
    return after.attack;
}

// The port's cover over `scenario`, checked against the cartridge's own answer where the run asks it.
AttackInFlight checkedCover(const DamageScenario& scenario) {
    withCartridge(CoverAndParamsTest::image(), [&](const Cartridge& cartridge) {
        if (!cartridge) {
            return;
        }
        EXPECT_EQ(trace::differences(trace::portCoverTarget(scenario), cartridge.oracle().coverEffect(scenario)), "");
    });
    DamageScenario after = scenario;
    battle::coverTarget(after.state, after.attack, after.attacker);
    return after.attack;
}

// --- the attack's animation set up ---------------------------------------------------------------

// Locke jumps at a monster with `attacksLeft` landings still to come after this one.
DamageScenario jumpAt(int attacksLeft, bool dragonHorn) {
    DamageScenario s;
    s.attacker = kLocke;
    s.attack.command = BattleCommandId::JUMP;
    s.attack.targets = BattlerSet::of(kGuard);
    s.attack.scratch.attacksLeft = attacksLeft;
    s.attack.scratch.dragonHorn = dragonHorn;
    for (const BattlerSlot slot : {kLocke, kGuard}) {
        s.state.at(slot).present = true;
        s.state.at(slot).hp = 500;
        s.state.at(slot).maxHp = 500;
    }
    return s;
}

TEST_F(CoverAndParamsTest, ADragonHornLandingWithTwoMoreToComeAimsTheNextAtRandom) {
    const AttackInFlight attack = checkedParams(jumpAt(2, true));
    EXPECT_TRUE(attack.params.dragonHorn);
    EXPECT_TRUE(attack.rules.randomTarget);
    EXPECT_TRUE(attack.rules.fallBackToEarlierTargets);
}

TEST_F(CoverAndParamsTest, ADragonHornLandingWithOneMoreToComeAimsTheNextAtRandom) {
    const AttackInFlight attack = checkedParams(jumpAt(1, true));
    EXPECT_TRUE(attack.params.dragonHorn);
    EXPECT_TRUE(attack.rules.randomTarget);
    EXPECT_TRUE(attack.rules.fallBackToEarlierTargets);
}

TEST_F(CoverAndParamsTest, TheLastDragonHornLandingIsNotMarked) {
    const AttackInFlight attack = checkedParams(jumpAt(0, true));
    EXPECT_FALSE(attack.params.dragonHorn);
    EXPECT_FALSE(attack.rules.randomTarget);
    EXPECT_FALSE(attack.rules.fallBackToEarlierTargets);
}

TEST_F(CoverAndParamsTest, WithoutTheDragonHornNoLandingIsMarked) {
    const AttackInFlight attack = checkedParams(jumpAt(2, false));
    EXPECT_FALSE(attack.params.dragonHorn);
    EXPECT_FALSE(attack.rules.randomTarget);
    EXPECT_FALSE(attack.rules.fallBackToEarlierTargets);
}

// A monster's spell on a party member: it keeps to the other side, and a suppressed swirl is not
// shown and is suppressed no longer.
TEST_F(CoverAndParamsTest, TheOtherSideRuleAndTheSwirlAreSetUpWithTheParams) {
    DamageScenario s = jumpAt(0, false);
    s.attacker = kGuard;
    s.attack.command = BattleCommandId::MAGIC;
    s.attack.targets = BattlerSet::of(kLocke);
    s.attack.castingSwirlSuppressed = true;
    const AttackInFlight attack = checkedParams(s);
    EXPECT_TRUE(attack.params.attackerIsMonster);
    EXPECT_FALSE(attack.params.noCharacterTargets);
    EXPECT_TRUE(attack.rules.opposingSideOnly);
    EXPECT_FALSE(attack.params.castingSwirl);
    EXPECT_FALSE(attack.castingSwirlSuppressed);
}

// --- the cover -----------------------------------------------------------------------------------

// The guard's blow at Terra, Near Fatal, with Locke wearing the True Knight's relic beside her.
DamageScenario blowAtTerra() {
    DamageScenario s;
    s.attacker = kGuard;
    s.attack.canBeCritical = true;
    s.attack.targets = BattlerSet::of(kTerra);
    for (const BattlerSlot slot : {kTerra, kLocke, kEdgar, kGuard}) {
        s.state.at(slot).present = true;
        s.state.at(slot).hp = 100;
        s.state.at(slot).maxHp = 400;
    }
    s.state.at(kTerra).status.set(StatusId::NEAR_FATAL);
    s.state.at(kLocke).relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
    return s;
}

void expectCovered(const AttackInFlight& attack, BattlerSlot bodyguard) {
    EXPECT_EQ(attack.targets, BattlerSet::of(bodyguard));
    EXPECT_EQ(attack.params.coveredAlly, kTerra);
}

void expectUncovered(const AttackInFlight& attack) {
    EXPECT_EQ(attack.targets, BattlerSet::of(kTerra));
    EXPECT_FALSE(attack.params.coveredAlly);
}

TEST_F(CoverAndParamsTest, AControlledBodyguardDoesNotStepIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).controlledBy = kGuard;
    s.state.at(kGuard).controlling = kLocke;
    expectUncovered(checkedCover(s));
}

TEST_F(CoverAndParamsTest, ABodyguardControllingAMonsterStepsIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).controlling = kSoldier;
    s.state.at(kSoldier).controlledBy = kLocke;
    expectCovered(checkedCover(s), kLocke);
}

TEST_F(CoverAndParamsTest, ABodyguardWhoseControlIsEndingStepsIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).controlledBy = kGuard;
    s.state.at(kLocke).controlEnding = true;
    s.state.at(kGuard).controlling = kLocke;
    s.state.at(kGuard).controlEnding = true;
    expectCovered(checkedCover(s), kLocke);
}

TEST_F(CoverAndParamsTest, ABodyguardHeldInSeizeDoesNotStepIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).seizedBy = kGuard;
    s.state.at(kGuard).seizing = kLocke;
    expectUncovered(checkedCover(s));
}

TEST_F(CoverAndParamsTest, ABodyguardHoldingSomeoneStepsIn) {
    DamageScenario s = blowAtTerra();
    s.state.at(kLocke).seizing = kEdgar;
    s.state.at(kEdgar).seizedBy = kLocke;
    expectCovered(checkedCover(s), kLocke);
}

TEST_F(CoverAndParamsTest, AnAllyHeldInSeizeIsNotCovered) {
    DamageScenario s = blowAtTerra();
    s.state.at(kTerra).seizedBy = kGuard;
    s.state.at(kGuard).seizing = kTerra;
    expectUncovered(checkedCover(s));
}

// A Love Token's bodyguard steps in whatever the ally's HP, before any True Knight is considered.
TEST_F(CoverAndParamsTest, ALoveTokenBodyguardCoversFirst) {
    DamageScenario s = blowAtTerra();
    s.state.at(kTerra).status = {};
    s.state.at(kTerra).bodyguard = kEdgar;
    s.state.at(kEdgar).guarding = kTerra;
    const AttackInFlight attack = checkedCover(s);
    expectCovered(attack, kEdgar);
    EXPECT_EQ(attack.params.steppingIn, BattlerSet::of(kEdgar));
}

}  // namespace
}  // namespace ostinato::test
