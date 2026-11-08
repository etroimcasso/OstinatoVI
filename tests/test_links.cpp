// The links between battlers let go (docs/contracts/battle_effects.md § Control's release, § The links
// let go): a Control link marked as ending by each of the ten statuses and let go from either end,
// a live link left alone; Zinger's monster brought back and Love Token and Charm let go when a
// battler falls or leaves; each worked out by hand from the original, and the battle reaching each
// through CommandExecutor.

#include <gtest/gtest.h>

#include <variant>

#include "battle/battle_core.h"
#include "battle/battle_hooks.h"
#include "battle/command_executor.h"
#include "battle/links.h"
#include "battle_test_support.h"
#include "ostinato/game_version.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/status_id.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleState;
using battle::MonsterVisibilityAction;
using battle::MonsterVisibilityMode;

constexpr BattlerSlot kController = BattlerSlot::character(0);
constexpr BattlerSlot kMonster = BattlerSlot::monster(1);

// A party member controlling a monster, as Control leaves them.
BattleState controlling(GameVersion version = GameVersion::US_1_1) {
    BattleState state = makeBattle();
    state.version = version;
    battle::Battler& holder = state.at(kController);
    battle::Battler& held = state.at(kMonster);
    holder.controlling = kMonster;
    held.controlledBy = kController;
    holder.status.set(StatusId::CHANT);
    holder.triggers.usesControlMenu = true;
    holder.menuMayOpen = true;
    held.menuMayOpen = true;
    state.flippedByControl.add(kMonster);
    return state;
}

void expectLetGo(const BattleState& state) {
    const battle::Battler& holder = state.at(kController);
    const battle::Battler& held = state.at(kMonster);
    EXPECT_FALSE(holder.controlling.has_value());
    EXPECT_FALSE(held.controlledBy.has_value());
    EXPECT_FALSE(holder.controlEnding);
    EXPECT_FALSE(held.controlEnding);
    EXPECT_FALSE(holder.status.has(StatusId::CHANT));
    EXPECT_FALSE(state.flippedByControl.has(kMonster));
    for (const battle::Battler* battler : {&holder, &held}) {
        EXPECT_TRUE(battler->emptyGaugeAfterAction);
        EXPECT_TRUE(battler->pendingUpdates.removeAllActions);
        EXPECT_FALSE(battler->menuMayOpen);
    }
}

void expectStillHeld(const BattleState& state) {
    EXPECT_EQ(state.at(kController).controlling, kMonster);
    EXPECT_EQ(state.at(kMonster).controlledBy, kController);
    EXPECT_TRUE(state.at(kController).status.has(StatusId::CHANT));
    EXPECT_TRUE(state.flippedByControl.has(kMonster));
    EXPECT_TRUE(state.at(kMonster).menuMayOpen);
}

// --- marking a link as ending -----------------------------------------------------------------------

class EndControl : public ::testing::TestWithParam<StatusId> {};

TEST_P(EndControl, TheStatusOnTheControllerMarksItsLink) {
    BattleState state = controlling();
    state.at(kController).status.set(GetParam());
    battle::endControlIfUnable(state, kController);
    EXPECT_TRUE(state.at(kController).controlEnding);
    EXPECT_FALSE(state.at(kMonster).controlEnding);
}

TEST_P(EndControl, TheStatusOnTheMonsterMarksItsLink) {
    BattleState state = controlling();
    state.at(kMonster).status.set(GetParam());
    battle::endControlIfUnable(state, kMonster);
    EXPECT_TRUE(state.at(kMonster).controlEnding);
    EXPECT_FALSE(state.at(kController).controlEnding);
}

INSTANTIATE_TEST_SUITE_P(TheTenStatuses, EndControl,
                         ::testing::Values(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE, StatusId::BERSERK,
                                           StatusId::CONFUSE, StatusId::SLEEP, StatusId::DANCE, StatusId::STOP,
                                           StatusId::RAGE, StatusId::FROZEN));

TEST(EndControlOthers, OtherStatusesLeaveTheLinkLive) {
    for (const StatusId status : {StatusId::POISON, StatusId::IMP, StatusId::SILENCE, StatusId::SLOW,
                                  StatusId::HASTE, StatusId::FLOAT, StatusId::HIDE, StatusId::MORPH}) {
        BattleState state = controlling();
        state.at(kController).status.set(status);
        state.at(kMonster).status.set(status);
        battle::endControlIfUnable(state, kController);
        battle::endControlIfUnable(state, kMonster);
        EXPECT_FALSE(state.at(kController).controlEnding) << "status " << int(status);
        EXPECT_FALSE(state.at(kMonster).controlEnding) << "status " << int(status);
    }
}

TEST(EndControlOthers, ABattlerWithNoLinkIsLeftAsItIs) {
    BattleState state = makeBattle();
    state.at(kController).status.set(StatusId::SLEEP);
    battle::endControlIfUnable(state, kController);
    EXPECT_FALSE(state.at(kController).controlEnding);
}

// --- letting the link go ----------------------------------------------------------------------------

TEST(ReleaseEndedControl, AnEndingMarkOnTheMonsterLetsTheLinkGo) {
    BattleState state = controlling();
    state.at(kMonster).controlEnding = true;
    battle::releaseEndedControl(state, kMonster);
    expectLetGo(state);
    EXPECT_FALSE(state.at(kController).triggers.usesControlMenu);
}

TEST(ReleaseEndedControl, AnEndingMarkOnTheControllerLetsTheLinkGo) {
    BattleState state = controlling();
    state.at(kController).controlEnding = true;
    battle::releaseEndedControl(state, kController);
    expectLetGo(state);
}

TEST(ReleaseEndedControl, ALiveLinkIsLeftAlone) {
    BattleState state = controlling();
    battle::releaseEndedControl(state, kController);
    battle::releaseEndedControl(state, kMonster);
    expectStillHeld(state);
    EXPECT_FALSE(state.at(kController).emptyGaugeAfterAction);
    EXPECT_FALSE(state.at(kMonster).pendingUpdates.removeAllActions);
}

// Chant comes off the status word itself: nothing is left waiting to be cleared.
TEST(ReleaseEndedControl, ChantComesOffTheStatusDirectly) {
    BattleState state = controlling();
    state.at(kMonster).controlEnding = true;
    battle::releaseEndedControl(state, kMonster);
    EXPECT_FALSE(state.at(kController).status.has(StatusId::CHANT));
    EXPECT_FALSE(state.at(kController).statusesToClear.has(StatusId::CHANT));
}

// The Japanese cartridge's menu reads Chant, not the control-menu flag, and its release leaves the
// flag as it is.
TEST(ReleaseEndedControl, AJapaneseCartridgeLeavesTheControlMenuFlag) {
    BattleState state = controlling(GameVersion::JP_1_0);
    state.at(kMonster).controlEnding = true;
    battle::releaseEndedControl(state, kMonster);
    expectLetGo(state);
    EXPECT_TRUE(state.at(kController).triggers.usesControlMenu);
}

// --- Zinger, Love Token and Charm -------------------------------------------------------------------

TEST(ReleaseLinks, ThePartyMemberZingerSwallowedBringsTheMonsterBack) {
    BattleState state = makeBattle();
    state.heldByZinger = kMonster;
    state.swallowedByZinger = kController;
    battle::releaseLinks(state, kController);
    ASSERT_EQ(state.immediateActions.size(), 2u);
    const auto& hidden = std::get<MonsterVisibilityAction>(state.immediateActions[0]);
    EXPECT_EQ(hidden.mode, MonsterVisibilityMode::HideAndKeepBattleGoing);
    EXPECT_EQ(hidden.monsters, BattlerSet::of(kMonster));
    EXPECT_EQ(hidden.type, MonsterEntranceType::PRE_DRAWN);
    const auto& back = std::get<MonsterVisibilityAction>(state.immediateActions[1]);
    EXPECT_EQ(back.mode, MonsterVisibilityMode::ReviveAtCurrentHp);
    EXPECT_EQ(back.monsters, BattlerSet::of(kMonster));
    EXPECT_EQ(back.type, MonsterEntranceType::FADE_IN_FROM_TOP);
    EXPECT_FALSE(state.heldByZinger.has_value());
    EXPECT_FALSE(state.swallowedByZinger.has_value());
}

TEST(ReleaseLinks, ZingerHoldsOnWhenSomeoneElseFalls) {
    BattleState state = makeBattle();
    state.heldByZinger = kMonster;
    state.swallowedByZinger = kController;
    battle::releaseLinks(state, BattlerSlot::character(1));
    battle::releaseLinks(state, kMonster);
    EXPECT_TRUE(state.immediateActions.empty());
    EXPECT_EQ(state.heldByZinger, kMonster);
    EXPECT_EQ(state.swallowedByZinger, kController);
}

constexpr BattlerSlot kCaster = BattlerSlot::character(0);
constexpr BattlerSlot kTarget = BattlerSlot::character(1);

// Love Token as it leaves the two: the caster's hits go to the target.
BattleState loveToken() {
    BattleState state = makeBattle();
    state.at(kCaster).bodyguard = kTarget;
    state.at(kTarget).guarding = kCaster;
    return state;
}

TEST(ReleaseLinks, LoveTokenIsLetGoWhenTheCoveredFalls) {
    BattleState state = loveToken();
    battle::releaseLinks(state, kCaster);
    EXPECT_FALSE(state.at(kCaster).bodyguard.has_value());
    EXPECT_FALSE(state.at(kTarget).guarding.has_value());
}

TEST(ReleaseLinks, LoveTokenIsLetGoWhenTheBodyguardFalls) {
    BattleState state = loveToken();
    battle::releaseLinks(state, kTarget);
    EXPECT_FALSE(state.at(kCaster).bodyguard.has_value());
    EXPECT_FALSE(state.at(kTarget).guarding.has_value());
}

// The far end is cleared whoever it now points to: a bodyguard that has since taken a second Love
// Token stops guarding that one too.
TEST(ReleaseLinks, TheBodyguardsGuardingIsClearedWhoeverItNowNames) {
    BattleState state = loveToken();
    const BattlerSlot second = BattlerSlot::monster(0);
    state.at(second).bodyguard = kTarget;
    state.at(kTarget).guarding = second;
    battle::releaseLinks(state, kCaster);
    EXPECT_FALSE(state.at(kTarget).guarding.has_value());
    EXPECT_EQ(state.at(second).bodyguard, kTarget);
}

TEST(ReleaseLinks, CharmIsLetGoWhenTheCharmerFalls) {
    BattleState state = makeBattle();
    state.at(kMonster).charming = kController;
    state.at(kController).charmedBy = kMonster;
    battle::releaseLinks(state, kMonster);
    EXPECT_FALSE(state.at(kMonster).charming.has_value());
    EXPECT_FALSE(state.at(kController).charmedBy.has_value());
}

TEST(ReleaseLinks, CharmIsLetGoWhenTheCharmedFalls) {
    BattleState state = makeBattle();
    state.at(kMonster).charming = kController;
    state.at(kController).charmedBy = kMonster;
    battle::releaseLinks(state, kController);
    EXPECT_FALSE(state.at(kMonster).charming.has_value());
    EXPECT_FALSE(state.at(kController).charmedBy.has_value());
}

TEST(ReleaseLinks, AnUnlinkedBattlerChangesNothing) {
    BattleState state = loveToken();
    state.at(kMonster).charming = BattlerSlot::monster(0);
    state.at(BattlerSlot::monster(0)).charmedBy = kMonster;
    battle::releaseLinks(state, BattlerSlot::character(2));
    EXPECT_EQ(state.at(kCaster).bodyguard, kTarget);
    EXPECT_EQ(state.at(kTarget).guarding, kCaster);
    EXPECT_EQ(state.at(kMonster).charming, BattlerSlot::monster(0));
    EXPECT_EQ(state.at(BattlerSlot::monster(0)).charmedBy, kMonster);
    EXPECT_TRUE(state.immediateActions.empty());
}

// --- through the battle -----------------------------------------------------------------------------

TEST(LinksThroughTheBattle, AControllerFallingAsleepLetsTheMonsterGoAfterTheAction) {
    BattleState state = controlling();
    state.at(kController).status.set(StatusId::SLEEP);
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    battle::checkTheFallen(state, executor);
    expectLetGo(state);
}

TEST(LinksThroughTheBattle, AControlledMonsterThatDiesIsLetGoAndReleasesItsLinks) {
    BattleState state = controlling();
    state.at(kMonster).status.set(StatusId::DEAD);
    state.at(kMonster).charming = BattlerSlot::character(1);
    state.at(BattlerSlot::character(1)).charmedBy = kMonster;
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    battle::checkTheFallen(state, executor);
    expectLetGo(state);
    EXPECT_FALSE(state.at(kMonster).charming.has_value());
    EXPECT_FALSE(state.at(BattlerSlot::character(1)).charmedBy.has_value());
}

TEST(LinksThroughTheBattle, ALiveLinkSurvivesTheChecks) {
    BattleState state = controlling();
    battle::BattleHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    battle::checkTheFallen(state, executor);
    expectStillHeld(state);
}

}  // namespace
}  // namespace ostinato::test
