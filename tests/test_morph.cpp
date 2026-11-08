// Morph's meter (docs/contracts/battle_effects.md § Morph): the menu after Morph or Revert, the speed
// the meter runs down at for the party's supply, the meter running down and running out, and the
// supply kept, each worked out by hand from the original; and the battle reaching each through
// CommandExecutor.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <variant>

#include "battle/actions.h"
#include "battle/battle_core.h"
#include "battle/battle_hooks.h"
#include "battle/command_executor.h"
#include "battle/command_menu.h"
#include "battle/morph.h"
#include "battle_test_support.h"
#include "data/attack_properties.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/status_id.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;
using battle::MenuCommand;

constexpr BattlerSlot kTerra = BattlerSlot::character(0);

// Terra's menu with Morph in its second row.
constexpr std::array<BattleCommandId, 4> kMorphMenu{BattleCommandId::FIGHT, BattleCommandId::MORPH,
                                                    BattleCommandId::MAGIC, BattleCommandId::ITEM};

BattleState battleWithTerra(std::uint8_t supply, bool morphed) {
    BattleState state;
    state.party.morphSupply = supply;
    battle::Battler& terra = state.at(kTerra);
    terra.present = true;
    for (std::size_t row = 0; row < kMorphMenu.size(); ++row) {
        terra.commands[row] = MenuCommand{.command = kMorphMenu[row], .usable = true};
    }
    if (morphed) {
        terra.status.set(StatusId::MORPH);
    }
    terra.gauge.empty();
    return state;
}

// Terra as she is once Morph has gone on and the meter has started.
BattleState morphedTerra(std::uint8_t supply) {
    BattleState state = battleWithTerra(supply, true);
    battle::morphMenuChanged(state, kTerra);
    return state;
}

// --- the menu ---------------------------------------------------------------------------------------

TEST(MorphMenu, MorphingPutsRevertInMorphsRow) {
    BattleState state = morphedTerra(255);
    const auto& commands = state.at(kTerra).commands;
    EXPECT_EQ(commands[0].command, BattleCommandId::FIGHT);
    EXPECT_EQ(commands[1].command, BattleCommandId::REVERT);
    EXPECT_EQ(commands[2].command, BattleCommandId::MAGIC);
    EXPECT_EQ(commands[3].command, BattleCommandId::ITEM);
}

TEST(MorphMenu, RevertingPutsMorphBackInRevertsRow) {
    BattleState state = morphedTerra(255);
    state.at(kTerra).status.clear(StatusId::MORPH);
    battle::morphMenuChanged(state, kTerra);
    EXPECT_EQ(state.at(kTerra).commands[1].command, BattleCommandId::MORPH);
}

TEST(MorphMenu, AFullGaugeLetsTheMenuOpenAgain) {
    BattleState state = battleWithTerra(255, true);
    state.at(kTerra).gauge.setMeter(0);
    battle::morphMenuChanged(state, kTerra);
    EXPECT_TRUE(state.at(kTerra).atbStopped);
    EXPECT_TRUE(state.at(kTerra).menuMayOpen);
}

TEST(MorphMenu, AFullGaugeDuringACounterattackLeavesTheMenuShut) {
    BattleState state = battleWithTerra(255, true);
    state.at(kTerra).gauge.setMeter(0);
    state.counterattackRunning = true;
    battle::morphMenuChanged(state, kTerra);
    EXPECT_FALSE(state.at(kTerra).atbStopped);
    EXPECT_FALSE(state.at(kTerra).menuMayOpen);
}

TEST(MorphMenu, AGaugeStillFillingLeavesTheMenuShut) {
    BattleState state = battleWithTerra(255, true);
    battle::morphMenuChanged(state, kTerra);
    EXPECT_FALSE(state.at(kTerra).atbStopped);
    EXPECT_FALSE(state.at(kTerra).menuMayOpen);
}

// --- the meter starting -----------------------------------------------------------------------------

TEST(MorphMeter, MorphingStartsTheMeterFull) {
    const BattleState state = morphedTerra(255);
    EXPECT_EQ(state.morphedBattler, kTerra);
    EXPECT_EQ(state.morphCounter, 0xFFFF);
}

// The speed: 65,535 over the supply, over 8, plus 1 — over 16 when Morph lasts twice as long.
TEST(MorphMeter, TheSpeedForTheSmallestSupplyMorphCanStartWith) {
    EXPECT_EQ(morphedTerra(16).morphCounterSpeed, 65535 / 16 / 8 + 1);
    EXPECT_EQ(morphedTerra(16).morphCounterSpeed, 512);
}

TEST(MorphMeter, TheSpeedForAFullSupply) {
    EXPECT_EQ(morphedTerra(255).morphCounterSpeed, 65535 / 255 / 8 + 1);
    EXPECT_EQ(morphedTerra(255).morphCounterSpeed, 33);
}

TEST(MorphMeter, LastingTwiceAsLongHalvesTheSpeed) {
    for (const std::uint8_t supply : {std::uint8_t{16}, std::uint8_t{100}, std::uint8_t{255}}) {
        BattleState state = battleWithTerra(supply, true);
        state.morphLastsTwiceAsLong = true;
        battle::morphMenuChanged(state, kTerra);
        EXPECT_EQ(state.morphCounterSpeed, 65535 / supply / 16 + 1) << "supply " << int{supply};
    }
    BattleState sixteen = battleWithTerra(16, true);
    sixteen.morphLastsTwiceAsLong = true;
    battle::morphMenuChanged(sixteen, kTerra);
    EXPECT_EQ(sixteen.morphCounterSpeed, 256);
}

// The speed is capped at 2,047 before the 1 is added, but no supply Morph can start with comes near
// it: the fastest, at a supply of 16, is 512.
TEST(MorphMeter, NoSupplyMorphCanStartWithReachesTheSpeedCap) {
    for (int supply = battle::kMorphSupplyToMorph; supply <= 255; ++supply) {
        const BattleState state = morphedTerra(static_cast<std::uint8_t>(supply));
        EXPECT_LT(state.morphCounterSpeed, 0x7FF + 1) << "supply " << supply;
    }
}

TEST(MorphMeter, NoOneElseMorphsWhileSomeoneIsMorphed) {
    BattleState state = battleWithTerra(255, true);
    state.morphedBattler = BattlerSlot::character(1);
    state.morphCounter = 0x1234;
    state.morphCounterSpeed = 40;
    battle::morphMenuChanged(state, kTerra);
    EXPECT_EQ(state.morphedBattler, BattlerSlot::character(1));
    EXPECT_EQ(state.morphCounter, 0x1234);
    EXPECT_EQ(state.morphCounterSpeed, 40);
    EXPECT_EQ(state.at(kTerra).commands[1].command, BattleCommandId::REVERT);
}

TEST(MorphMeter, APermanentMorphStartsNoMeter) {
    BattleState state = battleWithTerra(255, true);
    state.permanentMorph = true;
    battle::morphMenuChanged(state, kTerra);
    EXPECT_FALSE(state.morphedBattler.has_value());
    EXPECT_EQ(state.morphCounter, 0);
}

// Morph goes on only through the Morph command (or at the start of a battle that keeps Terra
// morphed, with the supply filled): no attack puts it on, so the meter always starts from a supply
// the menu allowed.
TEST(MorphCorpus, NoAttackPutsMorphOn) {
    int carriers = 0;
    for (const AttackPropertiesEntry& entry : attackPropertiesEn()) {
        carriers += entry.record.statuses.has(StatusId::MORPH) ? 1 : 0;
    }
    EXPECT_EQ(carriers, 0);
}

// --- reverting --------------------------------------------------------------------------------------

TEST(MorphRevert, RevertingKeepsTheSupplyTheMeterHadLeft) {
    BattleState state = morphedTerra(200);
    state.morphCounter = 0x80FF;  // the meter at 128 of 256
    state.at(kTerra).status.clear(StatusId::MORPH);
    battle::morphMenuChanged(state, kTerra);
    EXPECT_EQ(state.party.morphSupply, 200 * 128 / 256);
    EXPECT_FALSE(state.morphedBattler.has_value());
    EXPECT_EQ(state.morphCounter, 0);
    EXPECT_EQ(state.at(kTerra).morphGauge, 0);
}

TEST(MorphRevert, RevertingRefreshesTheCommands) {
    BattleState state = morphedTerra(200);
    state.morphCounter = 0x0F00;  // 200 × 15 / 256 = 11: too little to Morph again
    state.at(kTerra).status.clear(StatusId::MORPH);
    battle::morphMenuChanged(state, kTerra);
    ASSERT_EQ(state.party.morphSupply, 11);
    EXPECT_TRUE(state.menusToRefresh.has(kTerra));
    EXPECT_EQ(state.at(kTerra).commands[1].command, BattleCommandId::MORPH);
    EXPECT_FALSE(state.at(kTerra).commands[1].usable);
}

TEST(MorphRevert, RevertingWithNoMeterRunningKeepsTheSupply) {
    BattleState state = battleWithTerra(255, false);
    state.at(kTerra).commands[1].command = BattleCommandId::REVERT;
    battle::morphMenuChanged(state, kTerra);
    EXPECT_EQ(state.party.morphSupply, 255);
    EXPECT_EQ(state.at(kTerra).commands[1].command, BattleCommandId::MORPH);
}

// --- the meter running down -------------------------------------------------------------------------

TEST(MorphRunDown, EachTimeUpdateTakesTheSpeedOffAndShowsTheHighByte) {
    BattleState state = morphedTerra(16);
    ASSERT_EQ(state.morphCounterSpeed, 512);
    battle::runDownMorph(state, kTerra);
    EXPECT_EQ(state.morphCounter, 0xFFFF - 512);
    EXPECT_EQ(state.at(kTerra).morphGauge, 0xFD);
    EXPECT_EQ(state.morphedBattler, kTerra);
}

TEST(MorphRunDown, ExactlyTheSpeedLeftEmptiesTheMeterWithoutRunningOut) {
    BattleState state = morphedTerra(255);
    state.morphCounter = state.morphCounterSpeed;
    battle::runDownMorph(state, kTerra);
    EXPECT_EQ(state.morphCounter, 0);
    EXPECT_EQ(state.morphedBattler, kTerra);
    EXPECT_EQ(state.party.morphSupply, 255);
    EXPECT_TRUE(state.at(kTerra).counterActions.empty());
}

// A meter of 300 with a speed of 512 runs out: the meter reads empty, not the 1 its high byte held.
TEST(MorphRunDown, RunningOutUsesUpTheSupplyAndQueuesARevertAsACounterattack) {
    BattleState state = morphedTerra(16);
    ASSERT_EQ(state.morphCounterSpeed, 512);
    state.morphCounter = 300;
    battle::runDownMorph(state, kTerra);
    EXPECT_EQ(state.party.morphSupply, 0);
    EXPECT_FALSE(state.morphedBattler.has_value());
    EXPECT_EQ(state.at(kTerra).morphGauge, 0);
    ASSERT_EQ(state.at(kTerra).counterActions.size(), 1u);
    const auto* revert = std::get_if<battle::CommandAction>(&state.at(kTerra).counterActions.front());
    ASSERT_NE(revert, nullptr);
    EXPECT_EQ(revert->command, BattleCommandId::REVERT);
    EXPECT_EQ(revert->targets, battle::BattlerSet::of(kTerra));
    EXPECT_TRUE(state.counterQueue.contains(kTerra));
}

TEST(MorphRunDown, ARevertQueuedBehindACounterattackAlreadyWaitingGoesAfterIt) {
    BattleState state = morphedTerra(255);
    battle::addCounterAction(state, kTerra, battle::CommandAction{.command = BattleCommandId::FIGHT});
    state.morphCounter = 0;
    battle::runDownMorph(state, kTerra);
    ASSERT_EQ(state.at(kTerra).counterActions.size(), 2u);
    EXPECT_EQ(std::get<battle::CommandAction>(state.at(kTerra).counterActions[1]).command, BattleCommandId::REVERT);
}

// --- the supply -------------------------------------------------------------------------------------

TEST(MorphSupply, TheSupplyIsScaledByTheMetersHighByte) {
    BattleState state = morphedTerra(255);
    state.morphCounter = 0x40FF;
    battle::saveMorphSupply(state);
    EXPECT_EQ(state.party.morphSupply, 255 * 64 / 256);
}

TEST(MorphSupply, NothingChangesWithNoOneMorphed) {
    BattleState state = battleWithTerra(255, false);
    state.morphCounter = 0x1000;
    battle::saveMorphSupply(state);
    EXPECT_EQ(state.party.morphSupply, 255);
}

// --- through the battle -----------------------------------------------------------------------------

TEST(MorphThroughTheBattle, ATimeUpdateRunsTheMeterDown) {
    BattleState state = morphedTerra(16);
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    runTimeUpdate(state, executor);
    EXPECT_EQ(state.morphCounter, 0xFFFF - 512);
    EXPECT_EQ(state.at(kTerra).morphGauge, 0xFD);
}

TEST(MorphThroughTheBattle, TheUpdatesAfterAnActionStartTheMeter) {
    BattleState state = battleWithTerra(255, true);
    state.at(kTerra).pendingUpdates.morph = true;
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    battle::settleAfterAction(state, executor);
    EXPECT_EQ(state.morphedBattler, kTerra);
    EXPECT_EQ(state.at(kTerra).commands[1].command, BattleCommandId::REVERT);
}

}  // namespace
}  // namespace ostinato::test
