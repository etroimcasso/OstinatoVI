// Every battler's waiting statuses taking effect (docs/contracts/battle_status.md, "The statuses
// taking effect" and "What each status does"): the immunity mask on what goes on and on what comes
// off, the three additions the mask does not touch, the statuses for a counterattack, every status's
// handler going on and coming off, the order the handlers run in, a Zombie's statuses, and the
// pending statuses emptied. Each case is worked out by hand from the original and also run on the
// cartridge (tools/damage-trace/damage_oracle.h); the port must match both.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/status.h"
#include "damage_oracle.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using battle::kAllStatuses;
using trace::DamageOracle;
using trace::DamageScenario;
using trace::StatusChangesResult;

constexpr BattlerSlot kMember = BattlerSlot::character(1);
constexpr BattlerSlot kMonster = BattlerSlot::monster(2);

// The cartridge's to-work-out bits, as StatusChangesResult::pendingUpdates holds them.
constexpr std::uint8_t kEnabledMagic = 0x80;
constexpr std::uint8_t kRemoveAllActions = 0x40;
constexpr std::uint8_t kStartCondemned = 0x20;
constexpr std::uint8_t kStopCondemned = 0x10;
constexpr std::uint8_t kCommandList = 0x08;
constexpr std::uint8_t kSpeed = 0x04;
constexpr std::uint8_t kMorph = 0x02;

class StatusChangesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // The statuses taking effect by the port, checked against the cartridge's own answer.
    static StatusChangesResult checked(const DamageScenario& scenario) {
        const StatusChangesResult port = trace::portStatusChanges(scenario);
        StatusChangesResult guest;
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            guest = oracle.statusChanges(scenario);
        });
        EXPECT_EQ(trace::differences(port, guest), "");
        return port;
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* StatusChangesTest::image_ = nullptr;

// A battle with one battler on the field at full HP, with `status`, to be given `toSet` and to lose
// `toClear`; it can be given anything.
DamageScenario battlerWith(BattlerSlot slot, const StatusSet& status, const StatusSet& toSet = {},
                           const StatusSet& toClear = {}) {
    DamageScenario scenario;
    battle::Battler& battler = scenario.state.at(slot);
    battler.present = true;
    battler.maxHp = 800;
    battler.hp = 800;
    battler.menuMayOpen = true;
    battler.status = status;
    battler.statusesToSet = toSet;
    battler.statusesToClear = toClear;
    return scenario;
}

std::size_t at(BattlerSlot slot) {
    return static_cast<std::size_t>(slot.index());
}

// --- the mask and the pass ------------------------------------------------------------------------

TEST_F(StatusChangesTest, WhatIsSetGoesOnWhatIsClearedComesOffAndBothAreEmptied) {
    const StatusChangesResult result = checked(
        battlerWith(kMember, StatusSet::of(StatusId::POISON), StatusSet::of(StatusId::BLIND), StatusSet::of(StatusId::POISON)));
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet::of(StatusId::BLIND).bytes);
    EXPECT_EQ(result.statusesToSet[at(kMember)].bytes, StatusSet{}.bytes);
    EXPECT_EQ(result.statusesToClear[at(kMember)].bytes, StatusSet{}.bytes);
}

TEST_F(StatusChangesTest, AStatusTheBattlerIsImmuneToDoesNotGoOn) {
    DamageScenario scenario = battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::POISON, StatusId::BLIND));
    scenario.state.at(kMember).inflictableStatuses = kAllStatuses.without(StatusSet::of(StatusId::POISON));
    EXPECT_EQ(checked(scenario).status[at(kMember)].bytes, StatusSet::of(StatusId::BLIND).bytes);
}

// The mask is on the clears too: a status the battler is immune to never comes off. That is how its
// equipment's statuses stay on.
TEST_F(StatusChangesTest, AStatusTheBattlerIsImmuneToDoesNotComeOff) {
    DamageScenario scenario =
        battlerWith(kMember, StatusSet::of(StatusId::FLOAT, StatusId::HASTE), {}, StatusSet::of(StatusId::FLOAT, StatusId::HASTE));
    scenario.state.at(kMember).inflictableStatuses = kAllStatuses.without(StatusSet::of(StatusId::FLOAT));
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet::of(StatusId::FLOAT).bytes);
    EXPECT_EQ(result.statusesToClear[at(kMember)].bytes, StatusSet{}.bytes);
}

TEST_F(StatusChangesTest, ABattlerNotOnTheFieldIsLeftAsItIs) {
    DamageScenario scenario = battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::POISON));
    scenario.state.at(kMember).present = false;
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet{}.bytes);
    EXPECT_EQ(result.statusesToSet[at(kMember)].bytes, StatusSet::of(StatusId::POISON).bytes);
}

// Every battler on the field takes its own statuses.
TEST_F(StatusChangesTest, EveryBattlerOnTheFieldTakesItsOwn) {
    DamageScenario scenario = battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::POISON));
    battle::Battler& monster = scenario.state.at(kMonster);
    monster.present = true;
    monster.maxHp = 3000;
    monster.hp = 3000;
    monster.statusesToSet = StatusSet::of(StatusId::SLOW);
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet::of(StatusId::POISON).bytes);
    EXPECT_EQ(result.status[at(kMonster)].bytes, StatusSet::of(StatusId::SLOW).bytes);
}

// A petrified battler is petrified again on every pass, whatever its immunities: it is marked as
// having died this action, its menu is closed and its actions dropped, each time.
TEST_F(StatusChangesTest, APetrifiedBattlerIsPetrifiedAgain) {
    DamageScenario scenario = battlerWith(kMonster, StatusSet::of(StatusId::PETRIFY));
    scenario.state.at(kMonster).inflictableStatuses = StatusSet{};
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.status[at(kMonster)].bytes, StatusSet::of(StatusId::PETRIFY).bytes);
    EXPECT_EQ(result.diedAwaitingCounter, BattlerSet::of(kMonster));
    EXPECT_FALSE(result.menuMayOpen[at(kMonster)]);
    EXPECT_EQ(result.pendingUpdates[at(kMonster)], kRemoveAllActions);
}

TEST_F(StatusChangesTest, NearFatalGoesOnAtAnEighthOfTheMaximum) {
    DamageScenario scenario = battlerWith(kMember, StatusSet{});
    scenario.state.at(kMember).hp = 100;  // 800 / 8
    EXPECT_EQ(checked(scenario).status[at(kMember)].bytes, StatusSet::of(StatusId::NEAR_FATAL).bytes);
}

TEST_F(StatusChangesTest, NearFatalStaysOffJustAboveAnEighth) {
    DamageScenario scenario = battlerWith(kMember, StatusSet{});
    scenario.state.at(kMember).hp = 101;
    EXPECT_EQ(checked(scenario).status[at(kMember)].bytes, StatusSet{}.bytes);
}

TEST_F(StatusChangesTest, NearFatalComesOffAboveAnEighth) {
    DamageScenario scenario = battlerWith(kMember, StatusSet::of(StatusId::NEAR_FATAL));
    scenario.state.at(kMember).hp = 101;
    EXPECT_EQ(checked(scenario).status[at(kMember)].bytes, StatusSet{}.bytes);
}

// The test is the battler's maximum shifted down three times: 807 / 8 is 100.
TEST_F(StatusChangesTest, NearFatalUsesTheEighthRoundedDown) {
    DamageScenario scenario = battlerWith(kMember, StatusSet::of(StatusId::NEAR_FATAL));
    scenario.state.at(kMember).maxHp = 807;
    scenario.state.at(kMember).hp = 101;
    EXPECT_EQ(checked(scenario).status[at(kMember)].bytes, StatusSet{}.bytes);
}

// Near fatal goes on whether the battler can be given it or not.
TEST_F(StatusChangesTest, NearFatalGoesOnPastTheMask) {
    DamageScenario scenario = battlerWith(kMember, StatusSet{});
    scenario.state.at(kMember).hp = 50;
    scenario.state.at(kMember).inflictableStatuses = StatusSet{};
    EXPECT_EQ(checked(scenario).status[at(kMember)].bytes, StatusSet::of(StatusId::NEAR_FATAL).bytes);
}

// At 0 HP the test reads 0 >= 0: a dead battler is set near fatal.
TEST_F(StatusChangesTest, ADeadBattlerReadsNearFatal) {
    DamageScenario scenario = battlerWith(kMember, StatusSet::of(StatusId::DEAD));
    scenario.state.at(kMember).hp = 0;
    EXPECT_EQ(checked(scenario).status[at(kMember)].bytes, StatusSet::of(StatusId::DEAD, StatusId::NEAR_FATAL).bytes);
}

// With its Overcast trigger, a battler to die becomes a Zombie, and its Condemned comes off.
TEST_F(StatusChangesTest, OvercastTurnsDeathIntoZombie) {
    DamageScenario scenario =
        battlerWith(kMember, StatusSet::of(StatusId::CONDEMNED), StatusSet::of(StatusId::DEAD));
    scenario.state.at(kMember).triggers.overcast = true;
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet::of(StatusId::ZOMBIE).bytes);
    EXPECT_EQ(result.pendingUpdates[at(kMember)], kStopCondemned);
}

TEST_F(StatusChangesTest, OvercastWithoutADeathDoesNothing) {
    DamageScenario scenario = battlerWith(kMember, StatusSet::of(StatusId::CONDEMNED));
    scenario.state.at(kMember).triggers.overcast = true;
    EXPECT_EQ(checked(scenario).status[at(kMember)].bytes, StatusSet::of(StatusId::CONDEMNED).bytes);
}

// --- the statuses for a counterattack ----------------------------------------------------------------

// Taken before the clears are masked: a Poison the battler cannot lose reads as gone, though the
// battler keeps it.
TEST_F(StatusChangesTest, TheStatusesForACounterattackAreTakenBeforeTheMask) {
    DamageScenario scenario =
        battlerWith(kMonster, StatusSet::of(StatusId::POISON), StatusSet::of(StatusId::SLOW), StatusSet::of(StatusId::POISON));
    scenario.state.at(kMonster).inflictableStatuses = kAllStatuses.without(StatusSet::of(StatusId::POISON));
    scenario.state.at(kMonster).retaliationPending = true;
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.statusForCounter[at(kMonster)].bytes, StatusSet::of(StatusId::SLOW).bytes);
    EXPECT_EQ(result.status[at(kMonster)].bytes, StatusSet::of(StatusId::POISON, StatusId::SLOW).bytes);
}

// Taken before any handler and before a Zombie's statuses come off: Haste taking Slow off and a
// Zombie losing its Blind do not show in it.
TEST_F(StatusChangesTest, TheStatusesForACounterattackAreTakenBeforeTheHandlers) {
    DamageScenario scenario = battlerWith(kMonster, StatusSet::of(StatusId::SLOW, StatusId::BLIND),
                                          StatusSet::of(StatusId::HASTE, StatusId::ZOMBIE));
    scenario.state.at(kMonster).retaliationPending = true;
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.statusForCounter[at(kMonster)].bytes,
              StatusSet::of(StatusId::SLOW, StatusId::BLIND, StatusId::HASTE, StatusId::ZOMBIE).bytes);
    EXPECT_EQ(result.status[at(kMonster)].bytes, StatusSet::of(StatusId::HASTE, StatusId::ZOMBIE).bytes);
}

TEST_F(StatusChangesTest, ABattlerNotWaitingToRetaliateKeepsItsStatusesForACounterattack) {
    DamageScenario scenario = battlerWith(kMonster, StatusSet{}, StatusSet::of(StatusId::SLOW));
    scenario.state.at(kMonster).statusForCounter = StatusSet::of(StatusId::IMAGE);
    EXPECT_EQ(checked(scenario).statusForCounter[at(kMonster)].bytes, StatusSet::of(StatusId::IMAGE).bytes);
}

// --- what each status does -----------------------------------------------------------------------

TEST_F(StatusChangesTest, ZombieGoingOnTakesDeadOffAndMarksTheBattler) {
    const StatusChangesResult result =
        checked(battlerWith(kMember, StatusSet::of(StatusId::DEAD), StatusSet::of(StatusId::ZOMBIE)));
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet::of(StatusId::ZOMBIE).bytes);
    EXPECT_EQ(result.diedAwaitingCounter, BattlerSet::of(kMember));
    EXPECT_EQ(result.statusChanged, BattlerSet::of(kMember));
}

// A monster's change is never marked: only the party members' bits are loaded.
TEST_F(StatusChangesTest, AMonstersStatusChangeIsNotMarked) {
    const StatusChangesResult result = checked(battlerWith(kMonster, StatusSet{}, StatusSet::of(StatusId::ZOMBIE)));
    EXPECT_EQ(result.diedAwaitingCounter, BattlerSet::of(kMonster));
    EXPECT_TRUE(result.statusChanged.empty());
}

TEST_F(StatusChangesTest, ZombieComingOffPutsAMonsterBackAmongTheStanding) {
    DamageScenario scenario = battlerWith(kMonster, StatusSet::of(StatusId::ZOMBIE), {}, StatusSet::of(StatusId::ZOMBIE));
    scenario.state.monstersFallen = BattlerSet::of(kMonster);
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.monstersStanding, BattlerSet::of(kMonster));
    EXPECT_TRUE(result.monstersFallen.empty());
}

TEST_F(StatusChangesTest, ConfuseGoingOnAndComingOffMarksAPartyMember) {
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::CONFUSE))).statusChanged,
              BattlerSet::of(kMember));
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet::of(StatusId::CONFUSE), {}, StatusSet::of(StatusId::CONFUSE)))
                  .statusChanged,
              BattlerSet::of(kMember));
}

// Imp changes the battler's spells and commands, and a party member's equipment is worked out again;
// a monster has no equipment to work out.
TEST_F(StatusChangesTest, ImpChangesSpellsCommandsAndEquipment) {
    const StatusChangesResult on = checked(battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::IMP)));
    EXPECT_EQ(on.pendingUpdates[at(kMember)], kEnabledMagic | kCommandList);
    EXPECT_TRUE(on.equipmentChanged[at(kMember)]);
    const StatusChangesResult off =
        checked(battlerWith(kMember, StatusSet::of(StatusId::IMP), {}, StatusSet::of(StatusId::IMP)));
    EXPECT_EQ(off.pendingUpdates[at(kMember)], kEnabledMagic | kCommandList);
    EXPECT_TRUE(off.equipmentChanged[at(kMember)]);
    const StatusChangesResult monster = checked(battlerWith(kMonster, StatusSet{}, StatusSet::of(StatusId::IMP)));
    EXPECT_EQ(monster.pendingUpdates[at(kMonster)], kEnabledMagic | kCommandList);
}

TEST_F(StatusChangesTest, RageComingOffWorksAPartyMembersEquipmentOut) {
    EXPECT_TRUE(checked(battlerWith(kMember, StatusSet::of(StatusId::RAGE), {}, StatusSet::of(StatusId::RAGE)))
                    .equipmentChanged[at(kMember)]);
}

// Petrify takes off what it rules out — Reraise, Hide and Interceptor stay, and statuses it does not
// name stay — ends Phantasm's drain, closes the menu, drops the actions and marks the battler.
TEST_F(StatusChangesTest, PetrifyGoingOnTakesItsStatusesOff) {
    DamageScenario scenario = battlerWith(
        kMember,
        StatusSet::of(StatusId::SLEEP, StatusId::POISON, StatusId::HASTE, StatusId::RERAISE, StatusId::HIDE,
                      StatusId::INTERCEPTOR, StatusId::MAGITEK, StatusId::CONDEMNED),
        StatusSet::of(StatusId::PETRIFY, StatusId::FLOAT));
    scenario.state.at(kMember).triggers.phantasm = true;
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.status[at(kMember)].bytes,
              StatusSet::of(StatusId::PETRIFY, StatusId::RERAISE, StatusId::HIDE, StatusId::INTERCEPTOR,
                            StatusId::MAGITEK, StatusId::CONDEMNED)
                  .bytes);
    EXPECT_FALSE(result.phantasm[at(kMember)]);
    EXPECT_FALSE(result.menuMayOpen[at(kMember)]);
    EXPECT_EQ(result.diedAwaitingCounter, BattlerSet::of(kMember));
    // Haste's coming off works its speed out again, and Sleep's empties its gauge after the action.
    EXPECT_EQ(result.pendingUpdates[at(kMember)], kRemoveAllActions | kSpeed);
    EXPECT_TRUE(result.emptyGaugeAfterAction[at(kMember)]);
}

// Dead takes Petrify and Condemned off as well, and wins over a Dead to clear.
TEST_F(StatusChangesTest, DeadGoingOnTakesPetrifyAndCondemnedOffAndWinsOverAClear) {
    const StatusChangesResult result =
        checked(battlerWith(kMember, StatusSet::of(StatusId::PETRIFY, StatusId::CONDEMNED, StatusId::IMAGE),
                            StatusSet::of(StatusId::DEAD), StatusSet::of(StatusId::DEAD)));
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet::of(StatusId::DEAD).bytes);
    EXPECT_EQ(result.diedAwaitingCounter, BattlerSet::of(kMember));
    EXPECT_FALSE(result.menuMayOpen[at(kMember)]);
}

TEST_F(StatusChangesTest, DeadComingOffBringsAMonsterBack) {
    DamageScenario scenario = battlerWith(kMonster, StatusSet::of(StatusId::DEAD), {}, StatusSet::of(StatusId::DEAD));
    scenario.state.monstersFallen = BattlerSet::of(kMonster);
    const StatusChangesResult result = checked(scenario);
    EXPECT_EQ(result.status[at(kMonster)].bytes, StatusSet{}.bytes);
    EXPECT_EQ(result.monstersStanding, BattlerSet::of(kMonster));
    EXPECT_TRUE(result.monstersFallen.empty());
    EXPECT_TRUE(result.emptyGaugeAfterAction[at(kMonster)]);
    EXPECT_EQ(result.pendingUpdates[at(kMonster)], kRemoveAllActions);
}

TEST_F(StatusChangesTest, PetrifyComingOffDoesWhatDeadsComingOffDoes) {
    const StatusChangesResult result =
        checked(battlerWith(kMember, StatusSet::of(StatusId::PETRIFY), {}, StatusSet::of(StatusId::PETRIFY)));
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet{}.bytes);
    EXPECT_TRUE(result.emptyGaugeAfterAction[at(kMember)]);
    EXPECT_EQ(result.pendingUpdates[at(kMember)], kRemoveAllActions);
    EXPECT_TRUE(result.monstersStanding.empty());
}

TEST_F(StatusChangesTest, CondemnedStartsAndStopsItsCount) {
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::CONDEMNED))).pendingUpdates[at(kMember)],
              kStartCondemned);
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet::of(StatusId::CONDEMNED), {}, StatusSet::of(StatusId::CONDEMNED)))
                  .pendingUpdates[at(kMember)],
              kStopCondemned);
}

TEST_F(StatusChangesTest, SilenceChangesTheCommandsBothWays) {
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::SILENCE))).pendingUpdates[at(kMember)],
              kCommandList);
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet::of(StatusId::SILENCE), {}, StatusSet::of(StatusId::SILENCE)))
                  .pendingUpdates[at(kMember)],
              kCommandList);
}

TEST_F(StatusChangesTest, SapTakesRegenOffAndRegenTakesSapOff) {
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet::of(StatusId::REGEN), StatusSet::of(StatusId::SAP)))
                  .status[at(kMember)]
                  .bytes,
              StatusSet::of(StatusId::SAP).bytes);
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet::of(StatusId::SAP), StatusSet::of(StatusId::REGEN)))
                  .status[at(kMember)]
                  .bytes,
              StatusSet::of(StatusId::REGEN).bytes);
}

// Given both at once, each takes the other off.
TEST_F(StatusChangesTest, SapAndRegenTogetherCancel) {
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::SAP, StatusId::REGEN)))
                  .status[at(kMember)]
                  .bytes,
              StatusSet{}.bytes);
}

TEST_F(StatusChangesTest, SleepStartsItsTimerAndClosesTheMenu) {
    const StatusChangesResult result = checked(battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::SLEEP)));
    EXPECT_EQ(result.sleepTimer[at(kMember)], battle::kSleepDuration);
    EXPECT_EQ(result.sleepTimer[at(kMember)], 18);
    EXPECT_FALSE(result.menuMayOpen[at(kMember)]);
    EXPECT_EQ(result.pendingUpdates[at(kMember)], kRemoveAllActions);
    EXPECT_TRUE(result.diedAwaitingCounter.empty());
}

TEST_F(StatusChangesTest, SleepComingOffEmptiesTheGaugeAfterTheAction) {
    const StatusChangesResult result =
        checked(battlerWith(kMember, StatusSet::of(StatusId::SLEEP), {}, StatusSet::of(StatusId::SLEEP)));
    EXPECT_TRUE(result.emptyGaugeAfterAction[at(kMember)]);
    EXPECT_TRUE(result.menuMayOpen[at(kMember)]);
}

TEST_F(StatusChangesTest, SlowTakesHasteOffAndHasteTakesSlowOff) {
    const StatusChangesResult slow = checked(battlerWith(kMember, StatusSet::of(StatusId::HASTE), StatusSet::of(StatusId::SLOW)));
    EXPECT_EQ(slow.status[at(kMember)].bytes, StatusSet::of(StatusId::SLOW).bytes);
    EXPECT_EQ(slow.pendingUpdates[at(kMember)], kSpeed);
    const StatusChangesResult haste = checked(battlerWith(kMember, StatusSet::of(StatusId::SLOW), StatusSet::of(StatusId::HASTE)));
    EXPECT_EQ(haste.status[at(kMember)].bytes, StatusSet::of(StatusId::HASTE).bytes);
    EXPECT_EQ(haste.pendingUpdates[at(kMember)], kSpeed);
}

TEST_F(StatusChangesTest, SlowOrHasteComingOffWorksTheSpeedOut) {
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet::of(StatusId::HASTE), {}, StatusSet::of(StatusId::HASTE)))
                  .pendingUpdates[at(kMember)],
              kSpeed);
}

TEST_F(StatusChangesTest, StopReflectAndFrozenStartTheirTimers) {
    const StatusChangesResult result = checked(
        battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::STOP, StatusId::REFLECT, StatusId::FROZEN)));
    EXPECT_EQ(result.stopTimer[at(kMember)], 18);
    EXPECT_EQ(result.reflectTimer[at(kMember)], 26);
    EXPECT_EQ(result.freezeTimer[at(kMember)], 34);
}

// A timer starts only as its status goes on; one already on leaves its timer where it is.
TEST_F(StatusChangesTest, AStatusAlreadyOnDoesNotRestartItsTimer) {
    DamageScenario scenario = battlerWith(kMember, StatusSet::of(StatusId::REFLECT));
    scenario.state.at(kMember).reflectTimer = 3;
    EXPECT_EQ(checked(scenario).reflectTimer[at(kMember)], 3);
}

TEST_F(StatusChangesTest, MorphWorksTheMenuOutBothWays) {
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::MORPH))).pendingUpdates[at(kMember)],
              kMorph);
    EXPECT_EQ(checked(battlerWith(kMember, StatusSet::of(StatusId::MORPH), {}, StatusSet::of(StatusId::MORPH)))
                  .pendingUpdates[at(kMember)],
              kMorph);
}

TEST_F(StatusChangesTest, AZombieLosesTheStatusesAZombieCannotHave) {
    const StatusChangesResult result = checked(battlerWith(
        kMember,
        StatusSet::of(StatusId::BLIND, StatusId::POISON, StatusId::BERSERK, StatusId::CONFUSE, StatusId::SLEEP,
                      StatusId::CONDEMNED, StatusId::IMAGE, StatusId::SILENCE, StatusId::SAP),
        StatusSet::of(StatusId::ZOMBIE)));
    EXPECT_EQ(result.status[at(kMember)].bytes,
              StatusSet::of(StatusId::ZOMBIE, StatusId::CONDEMNED, StatusId::IMAGE, StatusId::SILENCE, StatusId::SAP)
                  .bytes);
}

// The handlers of one half run highest status first: Dead's going on takes Dead out of the clears,
// and Zombie's, which runs after it, puts it back. Given both, the battler is a Zombie, not dead.
TEST_F(StatusChangesTest, GivenDeadAndZombieTheBattlerIsAZombie) {
    const StatusChangesResult result =
        checked(battlerWith(kMember, StatusSet{}, StatusSet::of(StatusId::DEAD, StatusId::ZOMBIE)));
    EXPECT_EQ(result.status[at(kMember)].bytes, StatusSet::of(StatusId::ZOMBIE).bytes);
}

TEST_F(StatusChangesTest, TheDurationsAreTheCartridges) {
    EXPECT_EQ(battle::kSleepDuration, 18);
    EXPECT_EQ(battle::kStopDuration, 18);
    EXPECT_EQ(battle::kReflectDuration, 26);
    EXPECT_EQ(battle::kFreezeDuration, 34);
}

// Every bit of every battler's statuses, pending statuses, immunities and flags drawn at random: the
// port and the cartridge agree on every draw.
TEST_F(StatusChangesTest, AgreesWithTheCartridgeOverDrawnBattles) {
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
