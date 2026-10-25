// The lines battlers wait in, and adding and removing their actions. The expected values come
// from docs/contracts/battle_core.md, which cites the line of the original each one is read from.

#include <gtest/gtest.h>

#include <variant>

#include "battle/action_queue.h"
#include "battle/actions.h"
#include "battle_test_support.h"
#include "data/attack_properties.h"
#include "data/battle_commands.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;
using battle::CommandAction;

const BattlerSlot kHero = BattlerSlot::character(0);
const BattlerSlot kMonster = BattlerSlot::monster(0);

TEST(BattlerQueue, FirstInFirstOut) {
    battle::BattlerQueue queue;
    queue.push(BattlerSlot::monster(1));
    queue.push(kHero);
    queue.push(BattlerSlot::monster(1));
    EXPECT_EQ(queue.size(), 3u);
    EXPECT_EQ(queue.pop(), BattlerSlot::monster(1));
    EXPECT_EQ(queue.pop(), kHero);
    EXPECT_EQ(queue.pop(), BattlerSlot::monster(1));
    EXPECT_FALSE(queue.pop().has_value());
}

TEST(BattlerQueue, EraseTakesTheBattlerOutEverywhere) {
    // RemoveAllActions, :1583-1591, and QuetzEffect, :1788-1796, blank every entry for the battler.
    battle::BattlerQueue queue;
    queue.push(kMonster);
    queue.push(kHero);
    queue.push(kMonster);
    queue.erase(kMonster);
    EXPECT_FALSE(queue.contains(kMonster));
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_EQ(queue.pop(), kHero);
}

TEST(AddAction, ActionsWaitInTheOrderTheyWereAdded) {
    // CreateNormalAction / CreateAction, :12928-12964: each new entry goes at the end of the chain.
    BattleState state = makeBattle();
    battle::addAction(state, kHero, CommandAction{.command = BattleCommandId::FIGHT});
    battle::addAction(state, kHero, CommandAction{.command = BattleCommandId::DEF});
    ASSERT_EQ(state.at(kHero).actions.size(), 2u);
    EXPECT_EQ(std::get<CommandAction>(state.at(kHero).actions[0]).command, BattleCommandId::FIGHT);
    EXPECT_EQ(std::get<CommandAction>(state.at(kHero).actions[1]).command, BattleCommandId::DEF);
    battle::removeAction(state, kHero);
    EXPECT_EQ(std::get<CommandAction>(state.at(kHero).actions[0]).command, BattleCommandId::DEF);
}

TEST(AddAction, AFirstCounterattackJoinsTheCounterLine) {
    // CreateRetalAction, :12910-12922: only a battler with nothing waiting joins the line.
    BattleState state = makeBattle();
    battle::addCounterAction(state, kMonster, battle::AiTurn{});
    battle::addCounterAction(state, kMonster, battle::AiTurn{});
    EXPECT_EQ(state.at(kMonster).counterActions.size(), 2u);
    EXPECT_EQ(state.counterQueue.size(), 1u);
}

TEST(AddAction, DefaultGoesToTheCounterListDuringACounterattack) {
    // CreateDefaultAction, :12900-12904.
    BattleState state = makeBattle();
    battle::addActionOrCounter(state, kMonster, battle::AiTurn{});
    EXPECT_EQ(state.at(kMonster).actions.size(), 1u);
    state.counterattackRunning = true;
    battle::addActionOrCounter(state, kMonster, battle::AiTurn{});
    EXPECT_EQ(state.at(kMonster).counterActions.size(), 1u);
}

TEST(AddAction, ImmediateActionsRunInTheOrderAdded) {
    // CreateImmediateAction, :12881-12894: a second one chains behind the first.
    BattleState state = makeBattle();
    battle::addImmediateAction(state, battle::PartyEscape{});
    battle::addImmediateAction(state, battle::ResetBattlePose{.character = kHero});
    ASSERT_EQ(state.immediateActions.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<battle::PartyEscape>(state.immediateActions[0]));
    EXPECT_TRUE(std::holds_alternative<battle::ResetBattlePose>(state.immediateActions[1]));
}

TEST(MpCost, MonstersPayTheSpellsBaseCost) {
    // GetMPCost, :13004-13009: a monster's spell costs MagicProp's MP byte.
    BattleState state = makeBattle();
    const CommandAction fire{.command = BattleCommandId::MAGIC, .attack = AttackId::FIRE};
    EXPECT_EQ(battle::commandMpCost(state, kMonster, fire), getAttackProperties(AttackId::FIRE).mpCost);
    EXPECT_GT(getAttackProperties(AttackId::FIRE).mpCost, 0);
    const CommandAction fight{.command = BattleCommandId::FIGHT, .attack = AttackId::FIRE};
    EXPECT_EQ(battle::commandMpCost(state, kMonster, fight), 0);
}

TEST(MpCost, PartyMembersPayWhatTheirSpellListSays) {
    // :12987-13003: the character's own list, byte 3 of the spell's entry; a spell not there is free.
    BattleState state = makeBattle();
    state.at(kHero).spellCosts.push_back({.spell = AttackId::FIRE, .mpCost = 2});
    for (BattleCommandId command :
         {BattleCommandId::MAGIC, BattleCommandId::X_MAGIC, BattleCommandId::LORE, BattleCommandId::SUMMON}) {
        EXPECT_EQ(battle::commandMpCost(state, kHero, CommandAction{.command = command, .attack = AttackId::FIRE}),
                  2);
    }
    EXPECT_EQ(battle::commandMpCost(state, kHero, CommandAction{.command = BattleCommandId::MAGIC,
                                                                 .attack = AttackId::ICE}),
              0);
}

TEST(MpCost, AnActionMimicAddsIsFreeAndTheMarkIsUsedOnce) {
    // :12975-12977: the mark is taken by the next action created, whatever it is.
    BattleState state = makeBattle();
    state.at(kHero).spellCosts.push_back({.spell = AttackId::FIRE, .mpCost = 2});
    const CommandAction fire{.command = BattleCommandId::MAGIC, .attack = AttackId::FIRE};
    state.mimicking = true;
    battle::addAction(state, kHero, fire);
    EXPECT_EQ(std::get<CommandAction>(state.at(kHero).actions.back()).mpCost, 0);
    EXPECT_FALSE(state.mimicking);
    battle::addAction(state, kHero, fire);
    EXPECT_EQ(std::get<CommandAction>(state.at(kHero).actions.back()).mpCost, 2);
    state.mimicking = true;
    battle::addImmediateAction(state, battle::PartyEscape{});
    EXPECT_FALSE(state.mimicking);
}

TEST(AdvanceWaitLength, EachCommandAddsItsWaitUpTo254) {
    // CalcCmdDelay, :614-647: the command's wait adds to the current one, capped at 254; an
    // unset wait (255) plus anything is 254.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    CommandAction fight{.command = BattleCommandId::FIGHT};
    state.at(kHero).advanceWait = 0;
    battle::addAdvanceWait(state, executor, kHero, fight);
    EXPECT_EQ(state.at(kHero).advanceWait, commandAdvanceWait(BattleCommandId::FIGHT));
    EXPECT_EQ(commandAdvanceWait(BattleCommandId::FIGHT), 16);
    battle::addAdvanceWait(state, executor, kHero, fight);
    EXPECT_EQ(state.at(kHero).advanceWait, 32);
    state.at(kHero).advanceWait = 250;
    battle::addAdvanceWait(state, executor, kHero, fight);
    EXPECT_EQ(state.at(kHero).advanceWait, 254);
    state.at(kHero).advanceWait.reset();
    CommandAction item{.command = BattleCommandId::ITEM};
    ASSERT_EQ(commandAdvanceWait(BattleCommandId::ITEM), 16);
    battle::addAdvanceWait(state, executor, kHero, item);
    EXPECT_EQ(state.at(kHero).advanceWait, 254);
    EXPECT_EQ(executor.aimed.size(), 4u);
}

TEST(RemoveAllActions, DropsTheChoicesButNotTheCounters) {
    // RemoveAllActions, :1579-1600.
    BattleState state = makeBattle();
    battle::Battler& hero = state.at(kHero);
    battle::addAction(state, kHero, CommandAction{});
    battle::addCounterAction(state, kHero, CommandAction{});
    state.advanceWaitQueue.push(kHero);
    hero.gauge.counter = 0x0040;
    hero.atbStopped = true;
    hero.inBattleMenu = true;
    hero.actionQueued = true;
    battle::removeAllActions(state, kHero);
    EXPECT_TRUE(hero.actions.empty());
    EXPECT_EQ(hero.counterActions.size(), 1u);
    EXPECT_FALSE(state.advanceWaitQueue.contains(kHero));
    EXPECT_EQ(hero.gauge.counter, 0xFF40);
    EXPECT_FALSE(hero.atbStopped);
    EXPECT_FALSE(hero.inBattleMenu);
    EXPECT_FALSE(hero.actionQueued);
    ASSERT_EQ(state.immediateActions.size(), 1u);
    EXPECT_EQ(std::get<battle::ResetBattlePose>(state.immediateActions[0]).character, kHero);
}

TEST(RemoveAllActions, AGaugeStillFillingIsLeftAlone) {
    BattleState state = makeBattle();
    state.at(kMonster).gauge.counter = 0x4000;
    state.at(kMonster).atbStopped = true;
    battle::removeAllActions(state, kMonster);
    EXPECT_EQ(state.at(kMonster).gauge.counter, 0x4000);
    EXPECT_TRUE(state.at(kMonster).atbStopped);
    EXPECT_TRUE(state.immediateActions.empty());  // only party members reset their pose
}

}  // namespace
}  // namespace ostinato::test
