// The three commands only a monster's script queues (battle/system_commands.h,
// docs/contracts/battle_ai.md): a variable set, raised or lowered, each kept within its byte; a
// switch toggled, set or cleared — a bit of a variable or one of the game's battle switches; and
// every one of the fourteen effects. Each expected value is worked out by hand from the original;
// one case runs each command through CommandExecutor::execute().

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

#include "battle/action_queue.h"
#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/command_executor.h"
#include "battle/system_commands.h"
#include "battle_test_support.h"
#include "data/ai_scripts.h"
#include "ostinato/ai_target_selector.h"
#include "ostinato/attack_flags.h"
#include "ostinato/status_id.h"

namespace ostinato::test {
namespace {

using battle::AiEffectAction;
using battle::AttackInFlight;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleScriptBuilder;
using battle::BattleState;
using battle::BattleSwitchAction;
using battle::BattleVariableAction;

const BattlerSlot kMonster = BattlerSlot::monster(0);
const BattlerSlot kSecondMonster = BattlerSlot::monster(1);

// Hooks that keep a copy of every attack as its damage is about to be worked out.
struct AttackLog : battle::AttackHooks {
    std::vector<AttackInFlight> made;
    void baseDamage(BattleState&, AttackInFlight& attack, BattlerSlot) override { made.push_back(attack); }
};

BattleState monsterBattle() {
    BattleState state = makeAimableBattle();
    for (const BattlerSlot slot : {kMonster, kSecondMonster}) {
        state.at(slot).monster = MonsterId::LOBO;
    }
    return state;
}

// --- variables ------------------------------------------------------------------------------------

std::uint8_t changed(std::uint8_t start, AiVariableChange change, std::uint8_t amount) {
    BattleState state = monsterBattle();
    state.battleVariables[2] = start;
    battle::changeBattleVariable(state, kMonster,
                                 BattleVariableAction{.variable = AiVariable::battle(2), .change = change, .amount = amount});
    return state.battleVariables[2];
}

TEST(AiVariableCommandTest, SetReplacesTheValue) {
    EXPECT_EQ(changed(200, AiVariableChange::Set, 5), 5);
    EXPECT_EQ(changed(0, AiVariableChange::Set, 63), 63);
    EXPECT_EQ(changed(9, AiVariableChange::Set, 0), 0);
}

TEST(AiVariableCommandTest, AddAndSubtractStayWithinTheByte) {
    EXPECT_EQ(changed(4, AiVariableChange::Add, 1), 5);
    EXPECT_EQ(changed(255, AiVariableChange::Add, 1), 0);
    EXPECT_EQ(changed(250, AiVariableChange::Add, 63), 57);
    EXPECT_EQ(changed(4, AiVariableChange::Subtract, 4), 0);
    EXPECT_EQ(changed(0, AiVariableChange::Subtract, 1), 255);
}

TEST(AiVariableCommandTest, TheBattlersOwnVariableIsItsOwn) {
    BattleState state = monsterBattle();
    state.at(kMonster).aiVariable = 7;
    state.at(kSecondMonster).aiVariable = 7;
    battle::changeBattleVariable(state, kMonster,
                                 BattleVariableAction{.variable = AiVariable::own(), .change = AiVariableChange::Add, .amount = 2});
    EXPECT_EQ(state.at(kMonster).aiVariable, 9);
    EXPECT_EQ(state.at(kSecondMonster).aiVariable, 7);
    EXPECT_EQ(state.battleVariables, (std::array<std::uint8_t, kAiBattleVariables>{}));
}

// --- switches ---------------------------------------------------------------------------------------

std::uint8_t switched(std::uint8_t start, AiSwitchChange change, std::uint8_t bit) {
    BattleState state = monsterBattle();
    state.at(kMonster).aiVariable = start;
    battle::changeBattleSwitch(state, kMonster, BattleSwitchAction{.which = AiSwitch::own(bit), .change = change});
    return state.at(kMonster).aiVariable;
}

TEST(AiSwitchCommandTest, ABitOfAVariableIsToggledSetOrCleared) {
    EXPECT_EQ(switched(0b0000'0101, AiSwitchChange::Toggle, 0), 0b0000'0100);
    EXPECT_EQ(switched(0b0000'0100, AiSwitchChange::Toggle, 0), 0b0000'0101);
    EXPECT_EQ(switched(0b0000'0100, AiSwitchChange::Set, 7), 0b1000'0100);
    EXPECT_EQ(switched(0b1000'0100, AiSwitchChange::Set, 7), 0b1000'0100);
    EXPECT_EQ(switched(0b1000'0100, AiSwitchChange::Clear, 2), 0b1000'0000);
    EXPECT_EQ(switched(0b1000'0000, AiSwitchChange::Clear, 2), 0b1000'0000);
}

TEST(AiSwitchCommandTest, ABattleVariablesBitIsShared) {
    BattleState state = monsterBattle();
    battle::changeBattleSwitch(state, kMonster,
                               BattleSwitchAction{.which = AiSwitch::battleVariable(3, 1), .change = AiSwitchChange::Set});
    EXPECT_EQ(state.battleVariables[3], 0b10);
}

TEST(AiSwitchCommandTest, TheGamesBattleSwitchIsToggledSetOrCleared) {
    BattleState state = monsterBattle();
    const BattleSwitchAction toggle{.which = AiSwitch::battleSwitch(73), .change = AiSwitchChange::Toggle};
    battle::changeBattleSwitch(state, kMonster, toggle);
    EXPECT_TRUE(state.switches.isSet(73));
    // Switch 73 is bit 1 of the game's tenth switch byte.
    EXPECT_EQ(state.switches.variables[9], 0b10);
    battle::changeBattleSwitch(state, kMonster, toggle);
    EXPECT_FALSE(state.switches.isSet(73));
    battle::changeBattleSwitch(state, kMonster, BattleSwitchAction{.which = AiSwitch::battleSwitch(40), .change = AiSwitchChange::Set});
    EXPECT_TRUE(state.switches.isSet(40));
    battle::changeBattleSwitch(state, kMonster, BattleSwitchAction{.which = AiSwitch::battleSwitch(40), .change = AiSwitchChange::Clear});
    EXPECT_FALSE(state.switches.isSet(40));
}

// --- effects ----------------------------------------------------------------------------------------

struct EffectRun {
    BattleState state = monsterBattle();
    AttackLog log;
    AttackInFlight attack;
    BattleScriptBuilder script;

    void run(AiEffect effect, AiTarget target = {}, StatusId status = {}, BattlerSlot actor = kMonster) {
        battle::runAiEffect(state, log, script, attack, actor,
                            AiEffectAction{.effect = effect, .target = target, .status = status});
    }
};

TEST(AiEffectTest, TheTwoTimersGoBackToZero) {
    EffectRun r;
    r.state.at(kMonster).turnTimer = 300;
    r.state.battleCounter = 90;
    r.run(AiEffect::ResetOwnTimer);
    EXPECT_EQ(r.state.at(kMonster).turnTimer, 0);
    EXPECT_EQ(r.state.battleCounter, 90);
    r.run(AiEffect::ResetBattleTimer);
    EXPECT_EQ(r.state.battleCounter, 0);
}

TEST(AiEffectTest, InvincibilityComesAndGoesForEveryoneTheTargetNames) {
    EffectRun r;
    r.run(AiEffect::MakeInvincible, AiTargetSelector::ALL_MONSTERS);
    EXPECT_EQ(r.state.invincible, BattlerSet::of(kMonster) | BattlerSet::of(kSecondMonster));
    r.run(AiEffect::EndInvincible, AiTargetSelector::SELF);
    EXPECT_EQ(r.state.invincible, BattlerSet::of(kSecondMonster));
}

TEST(AiEffectTest, TargetabilityChangesOnlyForTheTargetsMonsters) {
    EffectRun r;
    r.run(AiEffect::MakeUntargetable, AiTargetSelector::ALL_TARGETS);
    EXPECT_EQ(r.state.aiUntargetable, BattlerSet::of(kMonster) | BattlerSet::of(kSecondMonster));
    r.run(AiEffect::MakeTargetable, AiTargetSelector::MONSTER_SLOT_2);
    EXPECT_EQ(r.state.aiUntargetable, BattlerSet::of(kMonster));
}

TEST(AiEffectTest, TheTwoEndsOfTheBattle) {
    EffectRun r;
    r.run(AiEffect::EndBattle);
    EXPECT_EQ(r.state.endOfBattleEvent, battle::EndOfBattleEvent::EndNow);
    r.run(AiEffect::EndBattleLearningRages);
    EXPECT_EQ(r.state.endOfBattleEvent, battle::EndOfBattleEvent::RagesLearned);
}

TEST(AiEffectTest, RecruitingGauPutsTheBattlersRecordInThePartyAtItsSlot) {
    // MiscAIEffect_03 :5213-5227: `ora #$40; and #$e0` sets enabled and keeps visible and the row.
    EffectRun r;
    const BattlerSlot gau = BattlerSlot::character(2);
    r.state.at(gau).record = 11;
    r.state.party.currentParty = 2;
    r.state.party.membership[11] = {.party = 0, .slot = 0, .backRow = true, .enabled = false, .visible = true};
    r.run(AiEffect::RecruitGau, {}, {}, gau);
    EXPECT_TRUE(r.state.gauRecruited);
    EXPECT_EQ(r.state.party.membership[11].party, 2);
    EXPECT_EQ(r.state.party.membership[11].slot, 2);
    EXPECT_TRUE(r.state.party.membership[11].backRow);
    EXPECT_TRUE(r.state.party.membership[11].enabled);
    EXPECT_TRUE(r.state.party.membership[11].visible);
}

TEST(AiEffectTest, RecruitingGauLeavesAnInvisibleRecordInvisible) {
    EffectRun r;
    const BattlerSlot gau = BattlerSlot::character(0);
    r.state.at(gau).record = 11;
    r.state.party.membership[11] = {.party = 0, .slot = 3, .backRow = false, .enabled = false, .visible = false};
    r.run(AiEffect::RecruitGau, {}, {}, gau);
    EXPECT_TRUE(r.state.party.membership[11].enabled);
    EXPECT_FALSE(r.state.party.membership[11].visible);
    EXPECT_FALSE(r.state.party.membership[11].backRow);
}

TEST(AiEffectTest, FillGaugeFillsTheAskersGaugeWhenTheTargetNamesAnyone) {
    EffectRun r;
    r.state.at(kMonster).atbRate = 0x0123;
    r.state.at(kSecondMonster).atbRate = 0x0123;
    r.run(AiEffect::FillGauge, AiTargetSelector::NOTHING);
    EXPECT_EQ(r.state.at(kMonster).atbRate, 0x0123);
    r.run(AiEffect::FillGauge, AiTargetSelector::MONSTER_SLOT_2);
    EXPECT_EQ(r.state.at(kMonster).atbRate, 0xFF23);
    EXPECT_EQ(r.state.at(kSecondMonster).atbRate, 0x0123);
}

TEST(AiEffectTest, NothingDoesNothing) {
    EffectRun r;
    const BattleState before = r.state;
    r.run(AiEffect::Nothing);
    EXPECT_TRUE(r.log.made.empty());
    EXPECT_EQ(r.state.battleCounter, before.battleCounter);
    EXPECT_TRUE(r.script.take().empty());
}

TEST(AiEffectTest, SetStatusIsAnAttackOnItselfShowingNothing) {
    EffectRun r;
    r.run(AiEffect::SetStatus, {}, StatusId::PROTECT);
    ASSERT_EQ(r.log.made.size(), 1u);
    const AttackInFlight& attack = r.log.made[0];
    EXPECT_TRUE(attack.record.statuses.has(StatusId::PROTECT));
    EXPECT_EQ(attack.record.statuses.bytes, (StatusSet::of(StatusId::PROTECT).bytes));
    EXPECT_FALSE(attack.record.flags2.has(AttackFlag2::REMOVE_STATUS));
    EXPECT_TRUE(attack.record.flags2.has(AttackFlag2::UNDODGEABLE));
    EXPECT_EQ(attack.command, battle::kNothingShown);
    EXPECT_EQ(attack.targets, BattlerSet::of(kMonster));
    EXPECT_EQ(attack.attackerLevel, 0);
}

TEST(AiEffectTest, ClearStatusIsTheSameAttackRemovingIt) {
    EffectRun r;
    r.run(AiEffect::ClearStatus, {}, StatusId::PROTECT);
    ASSERT_EQ(r.log.made.size(), 1u);
    EXPECT_TRUE(r.log.made[0].record.statuses.has(StatusId::PROTECT));
    EXPECT_TRUE(r.log.made[0].record.flags2.has(AttackFlag2::REMOVE_STATUS));
}

TEST(AiEffectTest, HideGoesStraightOntoTheStatuses) {
    EffectRun r;
    r.run(AiEffect::HideSelf);
    EXPECT_TRUE(r.state.at(kMonster).status.has(StatusId::HIDE));
    EXPECT_FALSE(r.state.at(kMonster).statusesToSet.has(StatusId::HIDE));
    EXPECT_TRUE(r.log.made.empty());
}

// --- through the executor -----------------------------------------------------------------------------

TEST(AiCommandsTest, TheExecutorRunsEachForTheBattlerThatQueuedIt) {
    BattleState state = monsterBattle();
    AttackLog log;
    battle::CommandExecutor executor{log, blankMonsterNames(), noMonsterSprites()};
    executor.execute(state, kMonster,
                     BattleVariableAction{.variable = AiVariable::own(), .change = AiVariableChange::Set, .amount = 3});
    executor.execute(state, kMonster, BattleSwitchAction{.which = AiSwitch::own(7), .change = AiSwitchChange::Set});
    executor.execute(state, kMonster, AiEffectAction{.effect = AiEffect::HideSelf});
    EXPECT_EQ(state.at(kMonster).aiVariable, 0b1000'0011);
    EXPECT_TRUE(state.at(kMonster).status.has(StatusId::HIDE));
}

}  // namespace
}  // namespace ostinato::test
