// The battle script builder: each animation takes the params added for it, in order, and an
// animation already written can be replaced in place. Behaviors and their sources:
// docs/contracts/battle_commands.md § The battle script.

#include <gtest/gtest.h>

#include <variant>

#include "battle/battle_script.h"

namespace ostinato::test {
namespace {

using battle::AttackAnimation;
using battle::AttackAnimationParams;
using battle::BattlerSlot;
using battle::BattleScriptBuilder;

AttackAnimationParams paramsBy(BattlerSlot attacker) {
    AttackAnimationParams params;
    params.attacker = attacker;
    return params;
}

TEST(BattleScript, EachAnimationTakesTheParamsAddedForItInOrder) {
    BattleScriptBuilder script;
    script.addAnimation(AttackAnimation{.attack = AttackId::FIRE});
    script.add(battle::BattleMessage{.message = 3});
    script.addAnimation(AttackAnimation{.attack = AttackId::ICE});
    script.addParams(paramsBy(BattlerSlot::monster(2)));
    script.addParams(paramsBy(BattlerSlot::character(1)));
    const auto commands = script.take();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(std::get<AttackAnimation>(commands[0]).params.attacker, BattlerSlot::monster(2));
    EXPECT_EQ(std::get<AttackAnimation>(commands[2]).params.attacker, BattlerSlot::character(1));
}

TEST(BattleScript, AnAnimationWithNoParamsAddedKeepsEmptyOnes) {
    BattleScriptBuilder script;
    script.addAnimation(AttackAnimation{});
    script.addAnimation(AttackAnimation{});
    script.addParams(paramsBy(BattlerSlot::monster(0)));
    const auto commands = script.take();
    EXPECT_EQ(std::get<AttackAnimation>(commands[0]).params.attacker, BattlerSlot::monster(0));
    EXPECT_EQ(std::get<AttackAnimation>(commands[1]).params.attacker, BattlerSlot::character(0));
}

TEST(BattleScript, ReplaceAnimationWritesOverTheLastAnimationAdded) {
    BattleScriptBuilder script;
    script.addAnimation(AttackAnimation{.attack = AttackId::FIRE});
    script.addAnimation(AttackAnimation{.attack = AttackId::ICE});
    script.add(battle::BattleMessage{.message = 9});
    script.replaceAnimation(AttackAnimation{.attack = AttackId::BOLT, .secondary = 92});
    const auto commands = script.take();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(std::get<AttackAnimation>(commands[0]).attack, AttackId::FIRE);
    EXPECT_EQ(std::get<AttackAnimation>(commands[1]).attack, AttackId::BOLT);
    EXPECT_EQ(std::get<AttackAnimation>(commands[1]).secondary, 92);
    EXPECT_TRUE(std::holds_alternative<battle::BattleMessage>(commands[2]));
}

TEST(BattleScript, ReplacingWithNoAnimationAddedDoesNothing) {
    BattleScriptBuilder script;
    script.add(battle::StepBack{});
    script.replaceAnimation(AttackAnimation{});
    const auto commands = script.take();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<battle::StepBack>(commands[0]));
}

TEST(BattleScript, TheLastParamsAttackerIsTheCasterOfTheLastAttackShown) {
    BattleScriptBuilder script;
    EXPECT_FALSE(script.lastParamsAttacker());
    script.addParams(paramsBy(BattlerSlot::monster(3)));
    EXPECT_EQ(script.lastParamsAttacker(), BattlerSlot::monster(3));
}

TEST(BattleScript, TakeStartsAFreshScript) {
    BattleScriptBuilder script;
    script.addAnimation(AttackAnimation{});
    script.addParams(paramsBy(BattlerSlot::monster(1)));
    script.take();
    script.addAnimation(AttackAnimation{});
    const auto commands = script.take();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(std::get<AttackAnimation>(commands[0]).params.attacker, BattlerSlot::character(0));
    script.replaceAnimation(AttackAnimation{});
    EXPECT_TRUE(script.commands().empty());
}

}  // namespace
}  // namespace ostinato::test
