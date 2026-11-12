// A monster's AI script run checked against the cartridge's own routines (battle/ai.h,
// battle/system_commands.h, docs/contracts/battle_ai.md), run on the hosted cartridge over the same
// battle (tools/damage-trace/damage_oracle): every script readied into an empty slot and into one
// that held another script; turns and counterattacks; and the three commands only a script queues.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include "ai_battle.h"
#include "battle/ai.h"
#include "battle/battle_state.h"
#include "damage_oracle.h"
#include "data/ai_scripts.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"

namespace ostinato::test {
namespace {

using trace::AiRunResult;
using trace::AiScenario;
using trace::AiScriptPlaces;

// The scripts a scripted battle gives a party member start here.
constexpr std::uint16_t kCharacterAiScripts = 256;

AiScenario withScript(AiScenario s, battle::BattlerSlot slot, std::uint16_t script) {
    s.state.at(slot).aiScript = script;
    battle::readyAi(s.state, slot);
    return s;
}

TEST(AiRunOracleTest, ReadyingEveryScriptIntoAnEmptySlot) {
    withAiOracle([&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            AiScenario s = aiBattle();
            s.state.at(kEmptyMonsterSlot) = battle::Battler{};
            s.state.at(kEmptyMonsterSlot).aiScript = script;
            const AiScriptPlaces guest = oracle.readyAi(s, kEmptyMonsterSlot);
            battle::readyAi(s.state, kEmptyMonsterSlot);
            ASSERT_EQ(trace::differences(trace::portScriptPlaces(s.state, kEmptyMonsterSlot), guest), "")
                << "script " << script;
        }
    });
}

// Where each step stands in the cartridge's script block, for a report.
std::string differenceIn(const std::string& differences, std::uint16_t script, int turn, const AiScenario& s) {
    return "script " + std::to_string(script) + " on battler " + std::to_string(s.actor.index()) + ", turn " +
           std::to_string(turn + 1) + " (" + trace::describe(s) + "):\n" + differences;
}

void emptyLists(battle::BattleState& state, battle::BattlerSlot actor) {
    state.at(actor).actions.clear();
    state.at(actor).counterActions.clear();
    state.counterQueue = {};
}

// What a sweep compared, so a sweep that compares nothing shows.
struct SweepCounts {
    int runs = 0;
    int differing = 0;
    int actions = 0;
    int resumed = 0;  // runs that stopped at a wait
    int draws = 0;    // random numbers drawn
    std::string first;

    void note(const std::string& difference) {
        ++runs;
        if (!difference.empty() && ++differing == 1) {
            first = difference;
        }
    }
    void count(const AiRunResult& result, std::uint8_t rngBefore) {
        actions += static_cast<int>(result.actions.size() + result.counterActions.size());
        if (result.script.turnSkipped != battle::kNoResume || result.script.counterSkipped != battle::kNoResume) {
            ++resumed;
        }
        draws += static_cast<std::uint8_t>(result.rngPosition - rngBefore);
    }
    std::string report() const {
        return std::to_string(differing) + " of " + std::to_string(runs) + " runs differ; the first:\n" + first;
    }
};

// Six turns of `script` for `actor`, port and cartridge each going on from its own last turn.
// Returns the first difference, or empty.
std::string sixTurns(trace::DamageOracle& oracle, const AiScenario& s, std::uint16_t script, SweepCounts& counts) {
    battle::BattleState port = s.state;
    for (int turn = 0; turn < 6; ++turn) {
        const std::uint8_t rngBefore = port.rng.position();
        const AiRunResult guest = turn == 0 ? oracle.aiTurn(s) : oracle.nextAiTurn(s.actor);
        const AiRunResult ours = trace::portAiTurn(port, s.actor);
        const std::string differences = trace::differences(ours, guest);
        if (!differences.empty()) {
            return differenceIn(differences, script, turn, s);
        }
        counts.count(guest, rngBefore);
        emptyLists(port, s.actor);
    }
    return "";
}

TEST(AiRunOracleTest, EveryScriptForSixTurnsMatchesTheCartridge) {
    std::mt19937 rng{trace::kSweepSeed};
    SweepCounts counts;
    withAiOracle([&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            std::vector<battle::BattlerSlot> actors{battle::BattlerSlot::monster(script % battle::kMonsterSlots)};
            if (script >= kCharacterAiScripts) {
                actors.push_back(battle::BattlerSlot::character(script % battle::kPartySlots));
            }
            for (const battle::BattlerSlot actor : actors) {
                const AiScenario s = trace::drawAiRunScenario(rng, script, actor);
                counts.note(sixTurns(oracle, s, script, counts));
            }
        }
    });
    EXPECT_EQ(counts.differing, 0) << counts.report();
    // Every script on a monster slot, and the ones a scripted battle gives a party member on a party
    // slot too; the sweep queues actions, stops at waits and draws.
    EXPECT_EQ(counts.runs, 384 + 128);
    EXPECT_GE(counts.actions, 3000);
    EXPECT_GE(counts.resumed, 500);
    EXPECT_GE(counts.draws, 3000);
}

TEST(AiRunOracleTest, EveryCounterattackPartOnEveryPathThroughItsGuardMatchesTheCartridge) {
    std::mt19937 rng{trace::kSweepSeed};
    SweepCounts counts;
    int restricted = 0;
    withAiOracle([&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            if (!aiScript(script).hasCounter()) {
                continue;
            }
            for (int path = 0; path < trace::kCounterGuards; ++path) {
                const auto guard = static_cast<trace::CounterGuard>(path);
                const AiScenario s =
                    trace::drawAiRunScenario(rng, script, battle::BattlerSlot::monster(script % battle::kMonsterSlots), guard);
                const AiRunResult guest = oracle.aiCounter(s);
                const AiRunResult port = trace::portAiCounter(s);
                const std::string difference = trace::differences(port, guest);
                counts.note(difference.empty() ? "" : differenceIn(difference, script, path, s));
                counts.count(guest, s.state.rng.position());
                if (guard == trace::CounterGuard::DiedUnattacked || guard == trace::CounterGuard::QuickForAnother ||
                    guard == trace::CounterGuard::HeldByAStatus || guard == trace::CounterGuard::Charmed) {
                    ++restricted;
                }
            }
        }
    });
    EXPECT_EQ(counts.differing, 0) << counts.report();
    EXPECT_EQ(counts.runs, 210 * trace::kCounterGuards);
    EXPECT_EQ(restricted, 210 * 4);
    EXPECT_GE(counts.actions, 300);
}

TEST(AiRunOracleTest, TheThreeCommandsOverFiveHundredDrawnCasesMatchTheCartridge) {
    std::mt19937 rng{trace::kSweepSeed};
    int differing = 0;
    int statusesSet = 0;
    int statusesCleared = 0;
    std::string first;
    withAiOracle([&](trace::DamageOracle& oracle) {
        for (int i = 0; i < 500; ++i) {
            const trace::AiCommandCase c = trace::drawAiCommandCase(rng);
            if (const auto* effect = std::get_if<battle::AiEffectAction>(&c.action)) {
                statusesSet += effect->effect == AiEffect::SetStatus ? 1 : 0;
                statusesCleared += effect->effect == AiEffect::ClearStatus ? 1 : 0;
            }
            const std::string difference =
                trace::differences(trace::portAiCommand(c.scenario, c.action), oracle.aiCommand(c.scenario, c.action));
            if (!difference.empty() && ++differing == 1) {
                first = trace::listedActionText(c.action) + " by battler " + std::to_string(c.scenario.actor.index()) +
                        ":\n" + difference;
            }
        }
    });
    EXPECT_EQ(differing, 0) << differing << " of 500 differ; the first:\n" << first;
    EXPECT_GT(statusesSet, 0);
    EXPECT_GT(statusesCleared, 0);
}

TEST(AiRunOracleTest, ReadyingEveryScriptIntoASlotThatHeldAnotherKeepsWhatTheCartridgeKeeps) {
    // The slot held Repo Man's script, which has a counterattack part, and both parts had stopped at
    // a wait. A new script with no counterattack part keeps Repo Man's; nothing else is reset.
    const std::uint16_t before = static_cast<std::uint16_t>(MonsterId::REPO_MAN);
    ASSERT_TRUE(aiScript(before).hasCounter());
    int keptOldCounter = 0;
    withAiOracle([&](trace::DamageOracle& oracle) {
        for (std::uint16_t script = 0; script < kAiScriptCount; ++script) {
            AiScenario s = withScript(aiBattle(), kSelf, before);
            battle::Battler& slot = s.state.at(kSelf);
            slot.turnResume = battle::AiResume{.blocksSkipped = 1, .at = &slot.turnScript[1]};
            slot.counterResume = battle::AiResume{.blocksSkipped = 0, .at = &slot.counterScript[1]};
            slot.aiScript = script;
            const AiScriptPlaces guest = oracle.readyAi(s, kSelf);
            battle::readyAi(s.state, kSelf);
            ASSERT_EQ(trace::differences(trace::portScriptPlaces(s.state, kSelf), guest), "") << "script " << script;
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
    AiScriptPlaces guest;
    withAiOracle([&](trace::DamageOracle& oracle) { guest = oracle.readyAi(s, kSelf); });
    EXPECT_EQ(guest.counter, 3410);
    EXPECT_EQ(trace::portScriptPlaces(s.state, kSelf).counter, 3410);
    EXPECT_EQ(trace::portScriptPlaces(withScript(aiBattle(), kSelf, static_cast<std::uint16_t>(MonsterId::WILD_CAT)).state,
                                      kSelf)
                  .turn,
              3410);
}

// --- the three commands, worked out by hand ------------------------------------------------------------

trace::AiCommandResult checkedCommand(const AiScenario& s, const battle::Action& action) {
    trace::AiCommandResult guest;
    withAiOracle([&](trace::DamageOracle& oracle) { guest = oracle.aiCommand(s, action); });
    const trace::AiCommandResult port = trace::portAiCommand(s, action);
    EXPECT_EQ(trace::differences(port, guest), "") << trace::listedActionText(action);
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
    AiRunResult guest;
    withAiOracle([&](trace::DamageOracle& oracle) { guest = oracle.aiTurn(s); });
    const AiRunResult port = trace::portAiTurn(s);
    EXPECT_EQ(trace::differences(port, guest), "");
    EXPECT_FALSE(port.actions.empty());
}

}  // namespace
}  // namespace ostinato::test
