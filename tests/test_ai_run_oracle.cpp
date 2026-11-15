// A monster's AI script run worked out by hand (battle/ai.h, battle/system_commands.h,
// docs/contracts/battle_ai.md): every script readied into a slot that held another script, the part
// one script runs into another's, a turn on the fixed battle, and the three commands only a script
// queues. Each is checked against the cartridge's own routines, run on the hosted cartridge over the
// same battle (tools/damage-trace/src/oracle/damage_oracle.h), where the run asks it
// (tests/with_cartridge.h). Every script readied and run over drawn battles is in
// tests/oracle/test_ai_run_oracle.cpp.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

#include "ai_battle.h"
#include "battle/ai.h"
#include "battle/battle_state.h"
#include "oracle/damage_oracle.h"
#include "data/ai_scripts.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::AiRunResult;
using trace::AiScenario;
using trace::AiScriptPlaces;

AiScenario withScript(AiScenario s, battle::BattlerSlot slot, std::uint16_t script) {
    s.state.at(slot).aiScript = script;
    battle::readyAi(s.state, slot);
    return s;
}

// The places the cartridge readies `slot`'s script at over `s`, where the run asks it.
std::optional<AiScriptPlaces> cartridgeReadyAi(const Cartridge& cartridge, const AiScenario& s, battle::BattlerSlot slot) {
    if (!cartridge) {
        return std::nullopt;
    }
    return cartridge.oracle().readyAi(s, slot);
}

// The port's places for `slot` in `state` are the cartridge's, where there is a cartridge's.
::testing::AssertionResult theSamePlaces(const battle::BattleState& state, battle::BattlerSlot slot,
                                         const std::optional<AiScriptPlaces>& guest) {
    if (!guest) {
        return ::testing::AssertionSuccess();
    }
    const std::string apart = trace::differences(trace::portScriptPlaces(state, slot), *guest);
    return apart.empty() ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << apart;
}

// The cartridge's counterattack part stands at `place` of the script block, where there is a
// cartridge's.
void expectTheCartridgesCounterPartAt(const std::optional<AiScriptPlaces>& guest, int place) {
    if (guest) {
        EXPECT_EQ(guest->counter, place);
    }
}

// The cartridge's turn over `s` is the port's, where the run asks it.
void expectTheCartridgesTurn(const Cartridge& cartridge, const AiScenario& s, const AiRunResult& port) {
    if (cartridge) {
        EXPECT_EQ(trace::differences(port, cartridge.oracle().aiTurn(s)), "");
    }
}

TEST(AiRunOracleTest, ReadyingEveryScriptIntoASlotThatHeldAnotherKeepsWhatTheCartridgeKeeps) {
    // The slot held Repo Man's script, which has a counterattack part, and both parts had stopped at
    // a wait. A new script with no counterattack part keeps Repo Man's; nothing else is reset.
    const std::uint16_t before = static_cast<std::uint16_t>(MonsterId::REPO_MAN);
    ASSERT_TRUE(aiScript(before).hasCounter());
    int keptOldCounter = 0;
    withCartridge(aiCartridge(), [&](const Cartridge& cartridge) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            AiScenario s = withScript(aiBattle(), kSelf, before);
            battle::Battler& slot = s.state.at(kSelf);
            slot.turnResume = battle::AiResume{.blocksSkipped = 1, .at = &slot.turnScript[1]};
            slot.counterResume = battle::AiResume{.blocksSkipped = 0, .at = &slot.counterScript[1]};
            slot.aiScript = script;
            const std::optional<AiScriptPlaces> guest = cartridgeReadyAi(cartridge, s, kSelf);
            battle::readyAi(s.state, kSelf);
            ASSERT_TRUE(theSamePlaces(s.state, kSelf, guest)) << "script " << script;
            if (!aiScript(script).hasCounter()) {
                ++keptOldCounter;
                EXPECT_EQ(slot.counterScript.data(), aiScript(before).counter.data());
            }
        }
    });
    EXPECT_EQ(keptOldCounter, 174);
}

TEST(AiRunOracleTest, TheMagRoadersCounterattackPartIsWildCatsTurnPart) {
    // Script 243's part runs into script 119's, which the cartridge holds at byte 3410 of the block.
    const AiScenario s = withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::MAG_ROADER_4));
    std::optional<AiScriptPlaces> guest;
    withCartridge(aiCartridge(), [&](const Cartridge& cartridge) { guest = cartridgeReadyAi(cartridge, s, kSelf); });
    expectTheCartridgesCounterPartAt(guest, 3410);
    EXPECT_EQ(trace::portScriptPlaces(s.state, kSelf).counter, 3410);
    EXPECT_EQ(trace::portScriptPlaces(withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::WILD_CAT)).state,
                                      kSelf)
                  .turn,
              3410);
}

// --- the three commands, worked out by hand ------------------------------------------------------------

trace::AiCommandResult checkedCommand(const AiScenario& s, const battle::Action& action) {
    const trace::AiCommandResult port = trace::portAiCommand(s, action);
    withCartridge(aiCartridge(), [&](const Cartridge& cartridge) {
        if (!cartridge) {
            return;
        }
        EXPECT_EQ(trace::differences(port, cartridge.oracle().aiCommand(s, action)), "")
            << trace::listedActionText(action);
    });
    return port;
}

TEST(AiRunOracleTest, AVariableWrapsWithinItsByteOnBothSides) {
    AiScenario s = aiBattle();
    s.state.battleVariables = {255, 0, 9, 0};
    s.state.at(kSelf).aiVariable = 200;
    const auto variable = [&](AiVariable which, AiVariableChange change, std::uint8_t amount) {
        return checkedCommand(s, battle::BattleVariableAction{.variable = which, .change = change, .amount = amount});
    };
    EXPECT_EQ(variable(AiVariable::battle(0), AiVariableChange::Add, 1).battleVariables[0], 0);
    EXPECT_EQ(variable(AiVariable::battle(1), AiVariableChange::Subtract, 1).battleVariables[1], 255);
    EXPECT_EQ(variable(AiVariable::battle(2), AiVariableChange::Set, 63).battleVariables[2], 63);
    EXPECT_EQ(variable(AiVariable::own(), AiVariableChange::Add, 63).ownVariables[kSelf.index()], 7);
}

TEST(AiRunOracleTest, ASwitchIsToggledSetOrClearedOnBothSides) {
    AiScenario s = aiBattle();
    s.state.at(kSelf).aiVariable = 0b0000'0101;
    const auto change = [&](AiSwitch which, AiSwitchChange how) {
        return checkedCommand(s, battle::BattleSwitchAction{.which = which, .change = how});
    };
    EXPECT_EQ(change(AiSwitch::own(0), AiSwitchChange::Toggle).ownVariables[kSelf.index()], 0b0000'0100);
    EXPECT_EQ(change(AiSwitch::own(7), AiSwitchChange::Set).ownVariables[kSelf.index()], 0b1000'0101);
    EXPECT_EQ(change(AiSwitch::own(2), AiSwitchChange::Clear).ownVariables[kSelf.index()], 0b0000'0001);
    // Switch 73 is bit 1 of the battle's tenth switch byte.
    EXPECT_EQ(change(AiSwitch::battleSwitch(73), AiSwitchChange::Set).switches[9], 0b10);
    EXPECT_EQ(change(AiSwitch::battleVariable(3, 4), AiSwitchChange::Toggle).battleVariables[3], 0b1'0000);
}

TEST(AiRunOracleTest, EachEffectThatNamesATargetOnBothSidesWithAndWithoutOne) {
    AiScenario s = aiBattle();
    s.state.at(kSelf).atbRate = 0x0123;
    const auto effect = [&](AiEffect which, AiTarget target) {
        return checkedCommand(s, battle::AiEffectAction{.effect = which, .target = target});
    };
    EXPECT_EQ(effect(AiEffect::MakeInvincible, AiTargetSelector::ALL_MONSTERS).invincible,
              setOf(kSelf, kReflecting, kFallen, kHidden, kJumping));
    EXPECT_TRUE(effect(AiEffect::MakeInvincible, AiTargetSelector::NOTHING).invincible.empty());
    EXPECT_EQ(effect(AiEffect::MakeUntargetable, AiTargetSelector::ALL_TARGETS).aiUntargetable,
              setOf(kSelf, kReflecting, kFallen, kHidden, kJumping));
    EXPECT_EQ(effect(AiEffect::FillGauge, AiTargetSelector::CHAR_SLOT_1).atbRates[kSelf.index()], 0xFF23);
    EXPECT_EQ(effect(AiEffect::FillGauge, AiTargetSelector::NOTHING).atbRates[kSelf.index()], 0x0123);
    s.state.invincible = setOf(kSelf, kReflecting);
    s.state.aiUntargetable = setOf(kSelf, kReflecting);
    EXPECT_EQ(effect(AiEffect::EndInvincible, AiTargetSelector::SELF).invincible, setOf(kReflecting));
    EXPECT_EQ(effect(AiEffect::MakeTargetable, AiTargetSelector::MONSTER_SLOT_2).aiUntargetable, setOf(kSelf));
}

TEST(AiRunOracleTest, TheEffectsThatNameNoTargetOnBothSides) {
    AiScenario s = aiBattle();
    s.state.at(kSelf).turnTimer = 300;
    s.state.battleCounter = 90;
    const auto effect = [&](AiEffect which) { return checkedCommand(s, battle::AiEffectAction{.effect = which}); };
    EXPECT_EQ(effect(AiEffect::ResetOwnTimer).ownTimers[kSelf.index()], 0);
    EXPECT_EQ(effect(AiEffect::ResetBattleTimer).battleCounter, 0);
    EXPECT_EQ(effect(AiEffect::EndBattle).endOfBattleEvent, battle::EndOfBattleEvent::EndNow);
    EXPECT_EQ(effect(AiEffect::EndBattleLearningRages).endOfBattleEvent, battle::EndOfBattleEvent::RagesLearned);
    EXPECT_TRUE(effect(AiEffect::HideSelf).statuses[kSelf.index()].has(StatusId::HIDE));
    EXPECT_EQ(effect(AiEffect::Nothing).battleCounter, 90);
}

// SetStatus and ClearStatus make an attack by the monster on itself, run whole on both sides: the
// status goes on or comes off, and nothing else of the battle moves.
TEST(AiRunOracleTest, SetStatusPutsProtectOnTheMonsterItself) {
    const AiScenario s = aiBattle();
    const trace::AiCommandResult result =
        checkedCommand(s, battle::AiEffectAction{.effect = AiEffect::SetStatus, .status = StatusId::PROTECT});
    EXPECT_TRUE(result.statuses[kSelf.index()].has(StatusId::PROTECT));
    EXPECT_EQ(result.statuses[kReflecting.index()].bytes, s.state.at(kReflecting).status.bytes);
    EXPECT_EQ(result.attack.cells.at("HP of battler " + std::to_string(kSelf.index())),
              std::to_string(s.state.at(kSelf).hp));
}

TEST(AiRunOracleTest, ClearStatusTakesReflectOffMonsterSlotTwo) {
    AiScenario s = aiBattle();
    s.actor = kReflecting;
    const trace::AiCommandResult result =
        checkedCommand(s, battle::AiEffectAction{.effect = AiEffect::ClearStatus, .status = StatusId::REFLECT});
    EXPECT_FALSE(result.statuses[kReflecting.index()].has(StatusId::REFLECT));
    EXPECT_EQ(result.statuses[kSelf.index()].bytes, s.state.at(kSelf).status.bytes);
    EXPECT_EQ(result.attack.cells.at("HP of battler " + std::to_string(kReflecting.index())),
              std::to_string(s.state.at(kReflecting).hp));
}

TEST(AiRunOracleTest, RecruitingGauOnBothSides) {
    AiScenario s = aiBattle();
    s.actor = kEdgar;
    s.state.at(kEdgar).record = 11;
    s.state.party.currentParty = 2;
    s.state.party.membership[11] = {.party = 0, .slot = 0, .backRow = true};
    const trace::AiCommandResult result = checkedCommand(s, battle::AiEffectAction{.effect = AiEffect::RecruitGau});
    EXPECT_TRUE(result.gauRecruited);
    EXPECT_EQ(result.membership[11].party, 2);
    EXPECT_EQ(result.membership[11].slot, kEdgar.position());
    EXPECT_TRUE(result.membership[11].backRow);
}

TEST(AiRunOracleTest, LobosTurnOnTheFixedBattle) {
    const AiScenario s = withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::LOBO));
    const AiRunResult port = trace::portAiTurn(s);
    withCartridge(aiCartridge(), [&](const Cartridge& cartridge) { expectTheCartridgesTurn(cartridge, s, port); });
    EXPECT_FALSE(port.actions.empty());
}

}  // namespace
}  // namespace ostinato::test
