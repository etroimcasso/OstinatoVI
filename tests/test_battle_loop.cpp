// The battle loop: the order it serves the lines in, a battler's turn, counterattacks, what
// happens after each action, and the battle's end. The expected values come from
// docs/contracts/battle_core.md, which cites the line of the original each one is read from.

#include <gtest/gtest.h>

#include <variant>

#include "battle/actions.h"
#include "battle/battle_core.h"
#include "battle_test_support.h"
#include "data/battle_commands.h"
#include "data/battle_tables.h"
#include "data/rng_table.h"
#include "ostinato/battle_command_flags.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleState;
using battle::CommandAction;

const BattlerSlot kHero = BattlerSlot::character(0);
const BattlerSlot kFriend = BattlerSlot::character(1);
const BattlerSlot kMonster = BattlerSlot::monster(0);
const BattlerSlot kOtherMonster = BattlerSlot::monster(1);

// A starting position whose next entry is at least `low` and below `high`.
std::uint8_t positionDrawing(int low, int high) {
    for (int position = 0; position < 256; ++position) {
        const int entry = rngByte(static_cast<std::uint8_t>(position + 1));
        if (entry >= low && entry < high) {
            return static_cast<std::uint8_t>(position);
        }
    }
    ADD_FAILURE() << "no entry in [" << low << ", " << high << ")";
    return 0;
}

// A monster whose action is ready to run: gauge full and stopped, in the action line.
void readyToAct(BattleState& state, BattlerSlot slot, battle::Action action) {
    battle::Battler& battler = state.at(slot);
    battler.gauge.setMeter(0);
    battler.atbStopped = true;
    battler.actionQueued = true;
    battle::addAction(state, slot, std::move(action));
    state.actionQueue.push(slot);
}

template <typename T>
const T* lastScriptCommand(const BattleState& state) {
    for (auto it = state.requests.rbegin(); it != state.requests.rend(); ++it) {
        if (const auto* run = std::get_if<battle::RunBattleScript>(&*it); run != nullptr && !run->commands.empty()) {
            return std::get_if<T>(&run->commands.back());
        }
    }
    return nullptr;
}

// --- the order of service ------------------------------------------------------------------------

TEST(BattleLoop, ServesImmediateThenCounterThenAdvanceWaitThenAction) {
    // BattleLoop, :67-144.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    readyToAct(state, kOtherMonster, CommandAction{.command = BattleCommandId::FIGHT});
    state.advanceWaitQueue.push(kHero);
    battle::addCounterAction(state, kMonster, CommandAction{.command = BattleCommandId::FIGHT});
    battle::addImmediateAction(state, battle::PartyEscape{});

    stepUntilIdle(state, executor);
    ASSERT_EQ(executor.executed.size(), 1u);
    EXPECT_FALSE(executor.executed[0].actor.has_value());
    EXPECT_TRUE(std::holds_alternative<battle::PartyEscape>(executor.executed[0].action));

    stepUntilIdle(state, executor);
    ASSERT_EQ(executor.executed.size(), 2u);
    EXPECT_EQ(executor.executed[1].actor, kMonster);

    stepUntilIdle(state, executor);
    EXPECT_EQ(executor.automatic.size(), 1u);  // the hero's advance wait: nothing chosen yet
    EXPECT_EQ(executor.executed.size(), 2u);

    stepUntilIdle(state, executor);
    ASSERT_EQ(executor.executed.size(), 3u);
    EXPECT_EQ(executor.executed[2].actor, kOtherMonster);
}

TEST(BattleLoop, AnActionWaitsForItsScriptThenStepsBack) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    readyToAct(state, kMonster, CommandAction{.command = BattleCommandId::FIGHT});
    battle::stepBattle(state, executor);
    ASSERT_TRUE(state.waitingForScript.has_value());
    EXPECT_EQ(state.waitingForScript->resume, battle::ScriptResume::FinishAction);
    ASSERT_EQ(state.requests.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<battle::RunBattleScript>(state.requests[0]));
    state.requests.clear();

    battle::stepBattle(state, executor);  // the rest of the turn: the gauge empties, step back
    ASSERT_TRUE(state.waitingForScript.has_value());
    EXPECT_EQ(state.waitingForScript->resume, battle::ScriptResume::NextStep);
    EXPECT_NE(lastScriptCommand<battle::StepBack>(state), nullptr);
    const battle::Battler& monster = state.at(kMonster);
    EXPECT_EQ(monster.gauge.meter(), 1);
    EXPECT_FALSE(monster.advanceWait.has_value());
    EXPECT_FALSE(monster.atbStopped);
    EXPECT_FALSE(monster.actionQueued);

    battle::stepBattle(state, executor);
    EXPECT_FALSE(state.waitingForScript.has_value());
}

TEST(BattleLoop, TheRandomPositionMovesOnOncePerStep) {
    // :68 — once per pass of the loop, after any wait.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.rng = battle::BattleRng(100);
    battle::stepBattle(state, executor);
    EXPECT_EQ(state.rng.position(), 101);
    battle::stepBattle(state, executor);
    EXPECT_EQ(state.rng.position(), 102);

    readyToAct(state, kMonster, CommandAction{});
    battle::stepBattle(state, executor);  // parks on the script: no step yet
    EXPECT_EQ(state.rng.position(), 102);
}

TEST(BattleLoop, AnEmptyActionLineLetsTheBattleEnd) {
    // :137-139.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.battleMayNotEnd = true;
    battle::stepBattle(state, executor);
    EXPECT_FALSE(state.battleMayNotEnd);
}

TEST(BattleLoop, MonstersThatDiedLeaveTheFieldOnAStepOfTheirOwn) {
    // :100-104.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.monstersFallen.add(kMonster);
    readyToAct(state, kOtherMonster, CommandAction{});
    battle::stepBattle(state, executor);
    EXPECT_FALSE(state.monstersStanding.has(kMonster));
    EXPECT_TRUE(executor.executed.empty());
    stepUntilIdle(state, executor);
    EXPECT_EQ(executor.executed.size(), 1u);
}

TEST(BattleLoop, MenusMarkedForRefreshAreRefreshed) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.menusToRefresh.add(kFriend);
    battle::stepBattle(state, executor);
    ASSERT_EQ(state.requests.size(), 1u);
    EXPECT_EQ(std::get<battle::RefreshBattleMenu>(state.requests[0]).character, kFriend);
    EXPECT_TRUE(state.menusToRefresh.empty());
}

// --- a battler's turn ----------------------------------------------------------------------------

TEST(Turn, AHeldBattlerKeepsItsActionForLater) {
    // ExecAction, :245-250.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    readyToAct(state, kMonster, CommandAction{});
    state.at(kMonster).heldByControl = true;
    stepUntilIdle(state, executor);
    EXPECT_TRUE(executor.executed.empty());
    EXPECT_EQ(state.at(kMonster).actions.size(), 1u);
    EXPECT_TRUE(state.at(kMonster).requeuePending);
}

TEST(Turn, QuickRunsOutOnTheQuickTargetsLastAction) {
    // :237-244, then the loop clears a spent count, :69-71.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    readyToAct(state, kHero, CommandAction{});
    state.quickTarget = kHero;
    state.quickTurnsLeft = 1;
    state.menusDisabled = true;
    battle::stepBattle(state, executor);
    EXPECT_FALSE(state.quickTarget.has_value());
    EXPECT_EQ(state.quickTurnsLeft, 0);
    EXPECT_FALSE(state.menusDisabled);  // a battler's last action allows menus again
    stepUntilIdle(state, executor);
    EXPECT_FALSE(state.quickTurnsLeft.has_value());
}

TEST(Turn, AnAiTurnRunsTheAiAndThenWhatItChose) {
    // :211-224.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    executor.onAi = [](BattleState& s, BattlerSlot actor) {
        battle::addAction(s, actor, CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kHero)});
    };
    readyToAct(state, kMonster, battle::AiTurn{});
    battle::stepBattle(state, executor);
    EXPECT_EQ(executor.ai.size(), 1u);
    ASSERT_EQ(executor.executed.size(), 1u);
    EXPECT_EQ(std::get<CommandAction>(executor.executed[0].action).targets, BattlerSet::of(kHero));
}

TEST(Turn, NothingChosenPassesTheTurn) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kMonster).gauge.setMeter(0);
    state.actionQueue.push(kMonster);
    battle::stepBattle(state, executor);
    ASSERT_EQ(executor.executed.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<battle::NoAction>(executor.executed[0].action));
}

TEST(Turn, AJumpLeavesTheFieldThenLands) {
    // :225-230 and _00e4, :178-185.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    readyToAct(state, kHero, CommandAction{.command = BattleCommandId::JUMP});
    stepUntilIdle(state, executor);
    EXPECT_TRUE(executor.executed.empty());
    EXPECT_TRUE(state.awayFromTheField.has(kHero));
    EXPECT_TRUE(state.advanceWaitQueue.contains(kHero));
    EXPECT_TRUE(state.at(kHero).atbStopped);

    // Back through the advance-wait line: the leap plays while the wait runs (:5757-5789).
    stepUntilIdle(state, executor);
    EXPECT_TRUE(executor.executed.empty());
    EXPECT_TRUE(state.at(kHero).status.has(StatusId::HIDE));
    EXPECT_TRUE(state.mimic.replayJump);
    bool leapt = false;
    for (const auto& request : state.requests) {
        if (const auto* run = std::get_if<battle::RunBattleScript>(&request)) {
            for (const auto& command : run->commands) {
                if (const auto* stance = std::get_if<battle::ReadyStance>(&command)) {
                    leapt = stance->command == BattleCommandId::JUMP;
                }
            }
        }
    }
    EXPECT_TRUE(leapt);

    // When its wait is over, it comes down with the attack.
    state.actionQueue.push(kHero);
    stepUntilIdle(state, executor);
    ASSERT_EQ(executor.executed.size(), 1u);
    EXPECT_EQ(std::get<CommandAction>(executor.executed[0].action).command, BattleCommandId::JUMP);
    EXPECT_FALSE(state.awayFromTheField.has(kHero));
}

TEST(Turn, AnImpFightsWithCommandsAnImpCannotUse) {
    // InitPlayerAction, :408-439.
    ASSERT_FALSE(battleCommandProperties(BattleCommandId::STEAL).flags.has(BattleCommandFlags::IMP));
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kHero).status.set(StatusId::IMP);
    readyToAct(state, kHero,
               CommandAction{.command = BattleCommandId::STEAL, .attack = AttackId::FIRE,
                             .targets = BattlerSet::of(kMonster) | BattlerSet::of(kOtherMonster) |
                                        BattlerSet::of(kFriend),
                             .mpCost = 5});
    battle::stepBattle(state, executor);
    ASSERT_EQ(executor.executed.size(), 1u);
    const auto& fight = std::get<CommandAction>(executor.executed[0].action);
    EXPECT_EQ(fight.command, BattleCommandId::FIGHT);
    EXPECT_EQ(fight.mpCost, 0);
    EXPECT_EQ(fight.targets.count(), 1);
    EXPECT_FALSE(fight.targets.has(kFriend));  // never its own side
}

TEST(Turn, AnImpKeepsACommandAnImpCanUse) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kHero).status.set(StatusId::IMP);
    readyToAct(state, kHero, CommandAction{.command = BattleCommandId::ITEM, .targets = BattlerSet::of(kFriend)});
    battle::stepBattle(state, executor);
    EXPECT_EQ(std::get<CommandAction>(executor.executed[0].action).command, BattleCommandId::ITEM);
}

TEST(Turn, AChangedStatusMakesTheCommandAimAgain) {
    // :400-407.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    readyToAct(state, kHero, CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kMonster)});
    state.statusChanged.add(kHero);
    battle::stepBattle(state, executor);
    EXPECT_TRUE(std::get<CommandAction>(executor.executed[0].action).targets.empty());
    EXPECT_FALSE(state.statusChanged.has(kHero));
}

TEST(Turn, MimicCopiesTheLastCommand) {
    // _c201d9, :298-327, and SaveForMimic, :333-368.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    readyToAct(state, kHero, CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kMonster)});
    stepUntilIdle(state, executor);
    EXPECT_EQ(state.mimic.last.command, BattleCommandId::FIGHT);
    EXPECT_EQ(state.mimic.last.targets, BattlerSet::of(kMonster));

    readyToAct(state, kFriend, CommandAction{.command = BattleCommandId::MIMIC});
    stepUntilIdle(state, executor);
    ASSERT_EQ(executor.executed.size(), 2u);
    const auto& copied = std::get<CommandAction>(executor.executed[1].action);
    EXPECT_EQ(copied.command, BattleCommandId::FIGHT);
    EXPECT_EQ(copied.targets, BattlerSet::of(kMonster));
}

TEST(Turn, AMonstersCommandIsNotRemembered) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    readyToAct(state, kMonster, CommandAction{.command = BattleCommandId::ITEM});
    stepUntilIdle(state, executor);
    EXPECT_EQ(state.mimic.last.command, BattleCommandId::FIGHT);
    EXPECT_TRUE(state.mimic.last.targets.empty());
}

// --- the advance wait ----------------------------------------------------------------------------

TEST(AdvanceWaitStart, ACharacterStepsForwardForItsCommand) {
    // _c22188, :5731-5790, and QueueAction, :488-534.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    battle::addAction(state, kHero, CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(kMonster)});
    state.at(kHero).defending = true;
    state.advanceWaitQueue.push(kHero);
    battle::stepBattle(state, executor);
    const battle::Battler& hero = state.at(kHero);
    EXPECT_EQ(hero.advanceWait, 16);
    EXPECT_TRUE(hero.skipAdvanceWait);
    EXPECT_FALSE(hero.defending);
    EXPECT_EQ(executor.aimed.size(), 1u);
    const auto* stance = lastScriptCommand<battle::ReadyStance>(state);
    ASSERT_NE(stance, nullptr);
    EXPECT_EQ(stance->actor, kHero);
    EXPECT_EQ(stance->targets, BattlerSet::of(kMonster));
}

TEST(AdvanceWaitStart, AMonsterWithNothingChosenGetsAnAiTurn) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.advanceWaitQueue.push(kMonster);
    battle::stepBattle(state, executor);
    ASSERT_EQ(state.at(kMonster).actions.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<battle::AiTurn>(state.at(kMonster).actions[0]));
    EXPECT_EQ(state.at(kMonster).advanceWait, 0);  // an AI turn adds no wait
    EXPECT_TRUE(state.requests.empty());
}

TEST(AdvanceWaitStart, AHeldBattlerWaitsToBeRequeued) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kHero).heldByStatus = true;
    state.advanceWaitQueue.push(kHero);
    battle::stepBattle(state, executor);
    EXPECT_TRUE(state.at(kHero).requeuePending);
    EXPECT_TRUE(executor.automatic.empty());
}

TEST(AdvanceWaitStart, CharacterAiTakesTheTurn) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kHero).aiTakesTurns = true;
    state.advanceWaitQueue.push(kHero);
    battle::stepBattle(state, executor);
    EXPECT_EQ(executor.ai.size(), 1u);
}

// --- counterattacks ------------------------------------------------------------------------------

// A party member's turn that hits `target`, setting it up to answer.
void heroHits(BattleState& state, RecordingExecutor& executor, BattlerSlot target, bool physical) {
    executor.onExecute = [=](BattleState& s, std::optional<BattlerSlot> actor, const battle::Action&) {
        s.at(target).lastAttacker = actor;
        s.at(target).retaliationPending = true;
        s.scratch.physical = physical;
    };
    readyToAct(state, kHero, CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(target)});
    battle::stepBattle(state, executor);
}

TEST(Counterattack, AMonsterWithACounterScriptAnswers) {
    // CheckRetal, :12525-12564.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kMonster).hasCounterScript = true;
    heroHits(state, executor, kMonster, true);
    ASSERT_EQ(state.at(kMonster).counterActions.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<battle::AiTurn>(state.at(kMonster).counterActions[0]));
    EXPECT_TRUE(state.counterQueue.contains(kMonster));
    EXPECT_TRUE(state.attackedThisStep.has(kMonster));
}

TEST(Counterattack, ACounterIsNotAnswered) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kMonster).hasCounterScript = true;
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kMonster).lastAttacker = kHero;
        s.at(kMonster).retaliationPending = true;
    };
    battle::addCounterAction(state, kHero, CommandAction{});
    battle::stepBattle(state, executor);
    EXPECT_TRUE(state.at(kMonster).counterActions.empty());
}

TEST(Counterattack, RetortAnswersAPhysicalHit) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kFriend).triggers.retort = true;
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kFriend).lastAttacker = kMonster;
        s.at(kFriend).retaliationPending = true;
        s.scratch.physical = true;
    };
    readyToAct(state, kMonster, CommandAction{});
    battle::stepBattle(state, executor);
    ASSERT_EQ(state.at(kFriend).counterActions.size(), 1u);
    const auto& retort = std::get<CommandAction>(state.at(kFriend).counterActions[0]);
    EXPECT_EQ(retort.command, BattleCommandId::BUSHIDO);
    EXPECT_EQ(retort.attack, AttackId::RETORT);
    EXPECT_EQ(retort.targets, BattlerSet::of(kMonster));
}

TEST(Counterattack, ABlackBeltAnswersAMonsterThreeTimesInFour) {
    // :12598-12614: the entry must fall below 192 — an entry from 64 to 191 answers, so a reading
    // of one time in four fails here.
    for (bool answers : {true, false}) {
        BattleState state = makeBattle();
        RecordingExecutor executor;
        state.at(kFriend).relicEffects4.set(RelicEffect4::RANDOM_COUNTER);
        executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
            s.at(kFriend).lastAttacker = kMonster;
            s.at(kFriend).retaliationPending = true;
            s.scratch.physical = true;
            s.scratch.blackBeltEligible.add(kFriend);
        };
        readyToAct(state, kMonster, CommandAction{});
        state.rng = battle::BattleRng(answers ? positionDrawing(64, 192) : positionDrawing(192, 256));
        battle::stepBattle(state, executor);
        EXPECT_EQ(state.at(kFriend).counterActions.size(), answers ? 1u : 0u);
    }
}

TEST(Counterattack, AMagicHitIsNotAnsweredByRetort) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kFriend).triggers.retort = true;
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kFriend).lastAttacker = kMonster;
        s.at(kFriend).retaliationPending = true;
    };
    readyToAct(state, kMonster, CommandAction{});
    battle::stepBattle(state, executor);
    EXPECT_TRUE(state.at(kFriend).counterActions.empty());
}

TEST(Counterattack, ACounterAiTurnRunsTheCounterAi) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    battle::addCounterAction(state, kMonster, battle::AiTurn{});
    battle::stepBattle(state, executor);
    EXPECT_EQ(executor.counterAi.size(), 1u);
    EXPECT_TRUE(state.counterattackRunning);
}

TEST(Counterattack, ACharacterDoesNotCounterOnceTheMonstersAreGone) {
    // ExecRetal, :12436-12449.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.enemyCount = 0;
    state.battleMayNotEnd = true;
    battle::addCounterAction(state, kHero, CommandAction{});
    battle::stepBattle(state, executor);
    EXPECT_TRUE(executor.executed.empty());
}

// --- after the action ----------------------------------------------------------------------------

TEST(AfterAction, AKilledMonsterFallsAndLeavesTheField) {
    // AfterAction1, :1068-1116; CheckDeadMonsters, :3135-3153.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kMonster).status.set(StatusId::DEAD);
    };
    readyToAct(state, kHero, CommandAction{.targets = BattlerSet::of(kMonster)});
    battle::stepBattle(state, executor);
    EXPECT_EQ(state.at(kMonster).hp, 0);
    EXPECT_TRUE(state.monstersFallen.has(kMonster));
    EXPECT_FALSE(state.monstersStanding.has(kMonster));
    battle::stepBattle(state, executor);
    EXPECT_FALSE(state.alive.has(kMonster));
    EXPECT_EQ(state.enemyCount, 1);
}

TEST(AfterAction, AMonsterWithACounterLeftStaysStanding) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kMonster).hasCounterScript = true;
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot> actor, const battle::Action&) {
        s.at(kMonster).status.set(StatusId::DEAD);
        s.at(kMonster).lastAttacker = actor;
        s.at(kMonster).retaliationPending = true;
    };
    readyToAct(state, kHero, CommandAction{.targets = BattlerSet::of(kMonster)});
    battle::stepBattle(state, executor);
    EXPECT_TRUE(state.monstersFallen.has(kMonster));
    EXPECT_TRUE(state.monstersStanding.has(kMonster));
}

TEST(AfterAction, BanonFallingLosesTheBattle) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kFriend).actor = CharacterPropId::BANON;
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kFriend).status.set(StatusId::DEAD);
    };
    readyToAct(state, kMonster, CommandAction{});
    stepUntilIdle(state, executor);
    EXPECT_EQ(state.endOfBattleEvent, battle::EndOfBattleEvent::BanonFell);
    const auto outcome = battle::stepBattle(state, executor);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(std::get<battle::BattleLost>(*outcome).message, battle::kMessageBanonFell);
}

TEST(AfterAction, ReraiseBringsADeadBattlerBack) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kFriend).status.set(StatusId::RERAISE);
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kFriend).status.set(StatusId::DEAD);
    };
    readyToAct(state, kMonster, CommandAction{});
    battle::stepBattle(state, executor);
    EXPECT_FALSE(state.at(kFriend).status.has(StatusId::RERAISE));
    ASSERT_EQ(state.immediateActions.size(), 1u);
    const auto& life = std::get<battle::SpellWithoutCaster>(state.immediateActions[0]);
    EXPECT_EQ(life.spell, AttackId::LIFE);
    EXPECT_EQ(life.target, kFriend);
}

TEST(AfterAction, ABattlerKilledInTheAirLivesUntilItLands) {
    // CheckJump, :1122-1132.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.awayFromTheField.add(kFriend);
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kFriend).status.set(StatusId::DEAD);
    };
    readyToAct(state, kMonster, CommandAction{});
    battle::stepBattle(state, executor);
    EXPECT_FALSE(state.at(kFriend).status.has(StatusId::DEAD));
    EXPECT_TRUE(state.at(kFriend).airAnchored);
}

TEST(AfterAction, TheCondemnedCountStartsFromTheAttackersLevel) {
    // StartCondemn, :1512-1525: 20 + max(0, 60 - (L + random(0..L-1))).
    for (std::uint8_t level : {std::uint8_t{20}, std::uint8_t{99}}) {
        BattleState state = makeBattle();
        RecordingExecutor executor;
        state.lastAttackLevel = level;
        executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
            s.at(kFriend).pendingUpdates.startCondemned = true;
        };
        readyToAct(state, kMonster, CommandAction{});
        battle::stepBattle(state, executor);
        const std::uint8_t position = state.rng.position();
        battle::stepBattle(state, executor);
        const int rolled = level + ((rngByte(static_cast<std::uint8_t>(position + 1)) * level) >> 8);
        EXPECT_EQ(state.at(kFriend).condemnedNumber, 20 + std::max(0, 60 - rolled)) << int(level);
    }
}

TEST(AfterAction, PendingUpdatesAreUsedUp) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kFriend).condemnedNumber = 30;
        s.at(kFriend).pendingUpdates.stopCondemned = true;
        s.at(kFriend).speed = 80;
        s.at(kFriend).pendingUpdates.speed = true;
    };
    readyToAct(state, kMonster, CommandAction{});
    stepUntilIdle(state, executor);
    EXPECT_EQ(state.at(kFriend).condemnedNumber, 0);
    EXPECT_EQ(state.at(kFriend).atbRate, (100 * 96) >> 4);
    EXPECT_FALSE(state.at(kFriend).pendingUpdates.speed);
}

TEST(AfterAction, ADeadBattlersGaugeEmpties) {
    // AfterAction2, :1331-1336; DisableATB, :1469-1476.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kFriend).gauge.counter = 0x8000;
    executor.onExecute = [](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(kFriend).status.set(StatusId::DEAD);
    };
    readyToAct(state, kMonster, CommandAction{});
    stepUntilIdle(state, executor);
    EXPECT_EQ(state.at(kFriend).gauge.meter(), 1);
    EXPECT_TRUE(state.at(kFriend).heldByStatus);
}

TEST(AfterAction, OnlyTheQuickTargetMayAct) {
    // _countercheck, :1383-1392.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.quickTarget = kFriend;
    state.quickTurnsLeft = 2;
    readyToAct(state, kMonster, CommandAction{});
    stepUntilIdle(state, executor);
    EXPECT_TRUE(state.at(kHero).heldByStatus);
    EXPECT_TRUE(state.at(kHero).heldByControl);
    EXPECT_FALSE(state.at(kFriend).heldByStatus);
    EXPECT_FALSE(state.at(kFriend).heldByControl);
}

TEST(AfterAction, AFreedBattlersHeldActionsGoBackInLine) {
    // :1410-1420.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    battle::addAction(state, kFriend, CommandAction{});
    state.at(kFriend).requeuePending = true;
    state.at(kFriend).skipAdvanceWait = true;
    readyToAct(state, kMonster, CommandAction{});
    stepUntilIdle(state, executor);
    EXPECT_FALSE(state.at(kFriend).requeuePending);
    EXPECT_TRUE(state.actionQueue.contains(kFriend));
}

TEST(AfterAction, APartyMemberWhoseMenuMayOpenGetsIt) {
    // CheckPlayerAction, :1426-1463.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kFriend).menuMayOpen = true;
    readyToAct(state, kMonster, CommandAction{});
    stepUntilIdle(state, executor);
    EXPECT_TRUE(state.at(kFriend).takesMenuInput);
    EXPECT_TRUE(state.at(kFriend).inBattleMenu);
    EXPECT_TRUE(state.at(kFriend).atbStopped);
}

TEST(AfterAction, AConfusedPartyMemberActsOnItsOwn) {
    BattleState state = makeBattle();
    RecordingExecutor executor;
    state.at(kFriend).menuMayOpen = true;
    state.at(kFriend).status.set(StatusId::CONFUSE);
    readyToAct(state, kMonster, CommandAction{});
    stepUntilIdle(state, executor);
    EXPECT_FALSE(state.at(kFriend).inBattleMenu);
    EXPECT_FALSE(state.at(kFriend).menuMayOpen);
    EXPECT_TRUE(state.advanceWaitQueue.contains(kFriend));
}

TEST(AfterAction, JoiningAndLeavingTheField) {
    // _timeenable, :3187-3212, and _timeenable2, :3159-3181.
    BattleState state = makeBattle();
    RecordingExecutor executor;
    const BattlerSlot newcomer = BattlerSlot::monster(3);
    state.at(newcomer).status.set(StatusId::HIDE);
    state.at(newcomer).monsterFlags.set(MonsterBattleFlag::FIRST_STRIKE);
    executor.onExecute = [&](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.joiningAfterAction.add(newcomer);
        s.leavingAfterAction.add(kOtherMonster);
    };
    readyToAct(state, kMonster, CommandAction{});
    stepUntilIdle(state, executor);
    EXPECT_TRUE(state.at(newcomer).present);
    EXPECT_FALSE(state.at(newcomer).status.has(StatusId::HIDE));
    EXPECT_TRUE(state.monstersStanding.has(newcomer));
    EXPECT_TRUE(state.at(newcomer).gauge.isFull());  // first strike
    EXPECT_FALSE(state.at(kOtherMonster).present);
    EXPECT_TRUE(state.at(kOtherMonster).status.has(StatusId::HIDE));
    EXPECT_FALSE(state.monstersStanding.has(kOtherMonster));
    EXPECT_TRUE(state.alive.has(newcomer));
    EXPECT_FALSE(state.alive.has(kOtherMonster));
}

TEST(AliveSets, APartyMemberFightingForTheOtherSideCountsAsAnEnemy) {
    // UpdateDead, :12291-12361.
    BattleState state = makeBattle();
    state.at(kFriend).actsAsEnemy = true;
    battle::refreshAliveSets(state);
    EXPECT_EQ(state.allyCount, 1);
    EXPECT_EQ(state.enemyCount, 3);
    EXPECT_TRUE(state.aliveEnemyCharacters.has(kFriend));
    EXPECT_FALSE(state.alive.has(kFriend));
}

TEST(RunDifficulty, TwoPerMonsterAndSixForAHardOne) {
    // UpdateMonsterGfxBuf, :15192-15250.
    BattleState state = makeBattle();
    battle::refreshRunDifficulty(state);
    EXPECT_EQ(state.runDifficulty, 4);
    state.at(kOtherMonster).monsterFlags.set(MonsterBattleFlag::HARDER_TO_RUN);
    battle::refreshRunDifficulty(state);
    EXPECT_EQ(state.runDifficulty, 8);
    state.at(kMonster).monsterFlags.set(MonsterBattleFlag::CANT_RUN);
    battle::refreshRunDifficulty(state);
    EXPECT_TRUE(state.cannotRun);
    EXPECT_TRUE(state.escapeBlockedByMonster);
    state.battleType = battle::BattleType::Side;
    battle::refreshRunDifficulty(state);
    EXPECT_EQ(state.runDifficulty, 0);
}

TEST(RunDifficulty, APincerWithBothSidesStandingBlocksEscape) {
    BattleState state = makeBattle();
    state.battleType = battle::BattleType::Pincer;
    state.pincerSides = {BattleSlotMask(0x01), BattleSlotMask(0x02)};
    battle::refreshRunDifficulty(state);
    EXPECT_TRUE(state.cannotRun);
    state.monstersStanding.remove(kOtherMonster);
    battle::refreshRunDifficulty(state);
    EXPECT_FALSE(state.cannotRun);
}

// --- the battle's end ----------------------------------------------------------------------------

std::optional<battle::BattleOutcome> stepOnce(BattleState& state) {
    battle::NullExecutor executor;
    return battle::stepBattle(state, executor);
}

TEST(BattleEnd, ThePartyFallingLosesWithAMessage) {
    // CheckBattleEnd, :11907-11922.
    BattleState state = makeBattle();
    state.alive = state.alive.monstersOnly();
    const auto outcome = stepOnce(state);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(std::get<battle::BattleLost>(*outcome).message, battle::kMessageAnnihilated);
    EXPECT_EQ(stepOnce(state).has_value(), true);

    BattleState colosseum = makeBattle();
    colosseum.colosseum = true;
    colosseum.alive = colosseum.alive.monstersOnly();
    EXPECT_FALSE(std::get<battle::BattleLost>(*stepOnce(colosseum)).message.has_value());
}

TEST(BattleEnd, APartyThatGotAwayOrWasEatenEndsTheBattle) {
    BattleState sneezed = makeBattle();
    sneezed.alive = sneezed.alive.monstersOnly();
    sneezed.partyLeft.add(kHero);
    EXPECT_EQ(std::get<battle::BattleEnded>(*stepOnce(sneezed)).reason, battle::BattleEndReason::RanOutOfTime);

    BattleState eaten = makeBattle();
    eaten.alive = eaten.alive.monstersOnly();
    eaten.engulfed = eaten.partyPresentAtStart;
    EXPECT_EQ(std::get<battle::BattleEnded>(*stepOnce(eaten)).reason, battle::BattleEndReason::EatenByZoneEater);
}

TEST(BattleEnd, NoEnemiesLeftWinsTheBattle) {
    BattleState state = makeBattle();
    state.enemyCount = 0;
    EXPECT_TRUE(std::holds_alternative<battle::BattleWon>(*stepOnce(state)));
}

TEST(BattleEnd, AHiddenMonsterKeepsTheBattleGoing) {
    BattleState state = makeBattle();
    state.enemyCount = 0;
    state.battleMayNotEnd = true;
    EXPECT_FALSE(stepOnce(state).has_value());
}

TEST(BattleEnd, AStoryTimerEndsTheBattleAtOnce) {
    BattleState state = makeBattle();
    state.eventTimerExpired = true;
    EXPECT_EQ(std::get<battle::BattleEnded>(*stepOnce(state)).reason, battle::BattleEndReason::TimerExpired);
}

TEST(BattleEnd, EndOfBattleEventsAreHeldBackInTheFinalBattle) {
    BattleState state = makeBattle();
    state.endOfBattleEvent = battle::EndOfBattleEvent::EndNow;
    state.finalBattleChain = true;
    EXPECT_FALSE(stepOnce(state).has_value());
    state.finalBattleChain = false;
    EXPECT_EQ(std::get<battle::BattleEnded>(*stepOnce(state)).reason, battle::BattleEndReason::EndNow);
}

TEST(BattleEnd, TheFinalBattleMovesOnToItsNextStage) {
    // CheckFinalBattle, :12245-12262.
    BattleState state = makeBattle();
    state.finalBattleChain = true;
    state.enemyCount = 0;
    state.formation = kFinalBattleFormations[1];
    const auto outcome = stepOnce(state);
    const auto& ended = std::get<battle::BattleEnded>(*outcome);
    EXPECT_EQ(ended.reason, battle::BattleEndReason::FinalBattleNextStage);
    EXPECT_EQ(ended.nextFormation, kFinalBattleFormations[2]);

    BattleState last = makeBattle();
    last.finalBattleChain = true;
    last.enemyCount = 0;
    last.formation = kFinalBattleFormations[3];
    EXPECT_TRUE(std::holds_alternative<battle::BattleWon>(*stepOnce(last)));
}

TEST(BattleEnd, GauMayTurnUpOnTheVeldt) {
    // :11928-11941 and GauAppears, :12010-12017: an entry below 160 — five times in eight. An
    // entry from 96 to 159 appears, so a reading of three in eight fails here.
    for (bool appears : {true, false}) {
        BattleState state = makeBattle();
        state.enemyCount = 0;
        state.at(kFriend).actor = CharacterPropId::GAU;
        state.gauMayAppear = true;
        state.rng = battle::BattleRng(appears ? positionDrawing(96, 160) : positionDrawing(160, 256));
        const auto outcome = stepOnce(state);
        EXPECT_FALSE(state.gauMayAppear);
        if (!appears) {
            EXPECT_TRUE(std::holds_alternative<battle::BattleWon>(*outcome));
            continue;
        }
        EXPECT_FALSE(outcome.has_value());
        EXPECT_TRUE(state.joiningAfterAction.has(kFriend));
        EXPECT_TRUE(state.at(kFriend).actsAsEnemy);
        EXPECT_TRUE(state.gauJoining);
        ASSERT_EQ(state.immediateActions.size(), 1u);
        EXPECT_EQ(std::get<battle::BattleEventAction>(state.immediateActions[0]).event, battle::kGauJoinsEvent);
    }
}

TEST(BattleEnd, GauTurnsUpForASmallPartyOnlyOnceHeHasJoined) {
    // :11936-11941: with fewer than two allies standing, only the battle switch for "Gau has
    // joined before" lets him appear.
    for (bool obtained : {true, false}) {
        BattleState state = makeBattle();
        state.enemyCount = 0;
        state.allyCount = 1;
        state.at(kFriend).actor = CharacterPropId::GAU;
        state.gauMayAppear = true;
        state.gauObtained = obtained;
        state.gauRecruited = !obtained;  // the roster bit plays no part here
        state.rng = battle::BattleRng(positionDrawing(0, 160));
        const auto outcome = stepOnce(state);
        EXPECT_EQ(state.gauJoining, obtained);
        EXPECT_EQ(outcome.has_value(), !obtained);
    }
}

TEST(BattleEnd, ShadowMayWalkOff) {
    // :11942-11959 and ShadowLeaves, :11990-12007.
    for (bool leaves : {true, false}) {
        BattleState state = makeBattle();
        state.enemyCount = 0;
        state.at(kFriend).actor = CharacterPropId::SHADOW;
        state.shadowAvailable = true;
        state.rng = battle::BattleRng(leaves ? positionDrawing(8, 16) : positionDrawing(16, 32));
        const auto outcome = stepOnce(state);
        if (!leaves) {
            EXPECT_TRUE(std::holds_alternative<battle::BattleWon>(*outcome));
            continue;
        }
        EXPECT_FALSE(outcome.has_value());
        EXPECT_TRUE(state.shadowLeftParty);
        EXPECT_FALSE(state.shadowAvailable);
        EXPECT_FALSE(state.at(kFriend).present);
        EXPECT_EQ(std::get<battle::BattleEventAction>(state.immediateActions.at(0)).event, battle::kShadowLeavesEvent);
    }
}

TEST(BattleEnd, ShadowStaysAfterAnythingButAFrontAttack) {
    BattleState state = makeBattle();
    state.enemyCount = 0;
    state.at(kFriend).actor = CharacterPropId::SHADOW;
    state.shadowAvailable = true;
    state.battleType = battle::BattleType::Back;
    state.rng = battle::BattleRng(positionDrawing(0, 16));
    EXPECT_TRUE(std::holds_alternative<battle::BattleWon>(*stepOnce(state)));
}

TEST(BattleEnd, WhenGauTurnsUpThePartyDropsItsChoices) {
    // ResetForVeldtGau, :1663-1673.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::addAction(state, kHero, CommandAction{});
    state.gauJoining = true;
    battle::stepBattle(state, executor);
    EXPECT_FALSE(state.gauJoining);
    EXPECT_TRUE(state.at(kHero).actions.empty());
}

}  // namespace
}  // namespace ostinato::test
