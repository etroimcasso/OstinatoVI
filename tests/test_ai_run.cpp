// How a monster's AI script is read (battle/ai.h, docs/contracts/battle_ai.md): the turn and the
// counterattack part read over hand-written steps, one rule at a time — a block that acts ends the
// run, a failed block is skipped and counted, a wait and the run that goes on from it, a condition
// after acting steps, a choice of nothing, the targets kept and put back, Confuse, Roulette and the
// advance wait, every step that queues an action of its own, the counterattack list, the restricted
// run and the eight ways a counterattack's restriction is decided; and a script readied into a slot,
// the Mag Roader's borrowed counterattack part and what a slot keeps when a new monster takes it.
// Every expected value is worked out by hand from the original.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <variant>
#include <vector>

#include "battle/action_queue.h"
#include "battle/ai.h"
#include "battle/attack.h"
#include "battle/battle_rng.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle_test_support.h"
#include "data/ai_scripts.h"
#include "data/battle_commands.h"
#include "ostinato/ai_target_selector.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_animation.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_id.h"
#include "ostinato/monster_visibility_mode.h"
#include "ostinato/status_id.h"

namespace ostinato::test {
namespace {

using battle::Action;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleState;
using battle::CommandAction;

constexpr BattlerSlot kMonster = BattlerSlot::monster(0);
constexpr BattlerSlot kOtherMonster = BattlerSlot::monster(1);
constexpr BattlerSlot kTerra = BattlerSlot::character(0);
constexpr BattlerSlot kLocke = BattlerSlot::character(1);

// Who the recording executor aims a command at when it has no targets.
const BattlerSet kAimedAt = BattlerSet::of(kLocke);

// Records each aim, and aims a command with no targets at kAimedAt.
class AimingExecutor : public battle::ActionExecutor {
public:
    void aim(BattleState&, BattlerSlot, CommandAction& command) override {
        ++aims;
        if (command.targets.empty()) {
            command.targets = kAimedAt;
        }
    }
    int aims = 0;
};

// Terra and Locke on the field for the party; two monsters, the first the one whose script runs.
BattleState battle() {
    BattleState state;
    for (const BattlerSlot slot : {kTerra, kLocke}) {
        state.at(slot).present = true;
        state.at(slot).actor = slot == kTerra ? CharacterPropId::TERRA : CharacterPropId::LOCKE;
    }
    for (const BattlerSlot slot : {kMonster, kOtherMonster}) {
        state.at(slot).present = true;
        state.at(slot).monster = MonsterId::LOBO;
    }
    state.alive = BattlerSet::of(kTerra) | BattlerSet::of(kLocke) | BattlerSet::of(kMonster) | BattlerSet::of(kOtherMonster);
    state.rng = battle::BattleRng(7);
    return state;
}

template <std::size_t N>
void giveTurn(BattleState& state, const std::array<AiStep, N>& steps, BattlerSlot slot = kMonster) {
    state.at(slot).turnScript = steps;
}

template <std::size_t N>
void giveCounter(BattleState& state, const std::array<AiStep, N>& steps, BattlerSlot slot = kMonster) {
    state.at(slot).counterScript = steps;
}

// A draw that RandA(n) turns into `k`.
std::function<bool(std::uint8_t)> drawsTo(int n, int k) {
    return [n, k](std::uint8_t r) { return ((r * n) >> 8) == k; };
}

const CommandAction& command(const Action& action) {
    return std::get<CommandAction>(action);
}

// The attacks queued, in order.
std::vector<AttackId> attacksQueued(const std::deque<Action>& actions) {
    std::vector<AttackId> attacks;
    for (const Action& action : actions) {
        attacks.push_back(command(action).attack);
    }
    return attacks;
}

void runTurn(BattleState& state, BattlerSlot slot = kMonster) {
    AimingExecutor executor;
    battle::runAiTurn(state, executor, slot);
}

// --- reading a part --------------------------------------------------------------------------------

TEST(AiRunTest, ABlockThatActsEndsTheRunAtItsEndIf) {
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::endIf(), AiStep::attack(AttackId::ICE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions), std::vector<AttackId>{AttackId::FIRE});
    EXPECT_EQ(state.at(kMonster).turnResume.blocksSkipped, battle::kNoResume);
}

TEST(AiRunTest, AFailedBlockIsSkippedAndTheRunGoesOnAfterIt) {
    const std::array steps{AiStep::never(),  AiStep::attack(AttackId::FIRE), AiStep::endIf(),
                           AiStep::attack(AttackId::ICE), AiStep::endIf(), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions), std::vector<AttackId>{AttackId::ICE});
}

TEST(AiRunTest, AFailedBlockBeforeTheEndEndsTheRun) {
    const std::array steps{AiStep::never(), AiStep::attack(AttackId::FIRE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    EXPECT_TRUE(state.at(kMonster).actions.empty());
    EXPECT_EQ(state.at(kMonster).turnResume.blocksSkipped, battle::kNoResume);
}

TEST(AiRunTest, EveryRunStartsAtTheTopOfThePart) {
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::endIf(), AiStep::attack(AttackId::ICE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    runTurn(state);
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions), (std::vector<AttackId>{AttackId::FIRE, AttackId::FIRE}));
}

TEST(AiRunTest, AWaitEndsTheRunAndTheNextGoesOnAfterIt) {
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::wait(), AiStep::attack(AttackId::ICE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    EXPECT_EQ(state.at(kMonster).turnResume.blocksSkipped, 0);
    EXPECT_EQ(state.at(kMonster).turnResume.at, &steps[2]);
    runTurn(state);
    // The second run resumes once, reaches the end and forgets the wait; the third starts over.
    EXPECT_EQ(state.at(kMonster).turnResume.blocksSkipped, battle::kNoResume);
    EXPECT_EQ(state.at(kMonster).turnResume.at, &steps[2]);
    runTurn(state);
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions),
              (std::vector<AttackId>{AttackId::FIRE, AttackId::ICE, AttackId::FIRE}));
}

TEST(AiRunTest, AWaitRemembersHowManyBlocksTheRunSkipped) {
    const std::array steps{AiStep::never(), AiStep::endIf(), AiStep::attack(AttackId::FIRE), AiStep::wait(),
                           AiStep::attack(AttackId::ICE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    EXPECT_EQ(state.at(kMonster).turnResume.blocksSkipped, 1);
    runTurn(state);
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions), (std::vector<AttackId>{AttackId::FIRE, AttackId::ICE}));
}

TEST(AiRunTest, ARunThatSkipsAnotherNumberOfBlocksActsFromWhereItIs) {
    // The first run skips no block and stops at the wait having skipped none. In the second the
    // switch is clear, so the first block is skipped and the run reaches the bolt having skipped
    // one: it does not go back to the wait.
    const std::array steps{AiStep::ifSwitchSet(AiSwitch::own(0)), AiStep::attack(AttackId::FIRE), AiStep::wait(),
                           AiStep::attack(AttackId::ICE), AiStep::endIf(), AiStep::attack(AttackId::BOLT),
                           AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    state.at(kMonster).aiVariable = 1;
    runTurn(state);
    state.at(kMonster).aiVariable = 0;
    runTurn(state);
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions), (std::vector<AttackId>{AttackId::FIRE, AttackId::BOLT}));
    EXPECT_EQ(state.at(kMonster).turnResume.blocksSkipped, battle::kNoResume);
}

TEST(AiRunTest, AConditionAfterActingStepsCanSkipTheRestOfTheBlock) {
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::never(), AiStep::attack(AttackId::ICE),
                           AiStep::endIf(), AiStep::attack(AttackId::BOLT), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions), (std::vector<AttackId>{AttackId::FIRE, AttackId::BOLT}));
}

TEST(AiRunTest, AChoiceOfNothingDrawsAndTheRunGoesOn) {
    const std::array steps{AiStep::attackOneOf({kNoAttack, kNoAttack, kNoAttack}), AiStep::attack(AttackId::FIRE),
                           AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    const std::uint8_t before = state.rng.position();
    runTurn(state);
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions), std::vector<AttackId>{AttackId::FIRE});
    EXPECT_EQ(state.rng.position(), static_cast<std::uint8_t>(before + 1));
}

TEST(AiRunTest, OneOfThreeAttacksIsTheOneTheDrawNames) {
    const std::array attacks{AttackId::FIRE, AttackId::ICE, AttackId::BOLT};
    for (int choice = 0; choice < 3; ++choice) {
        const std::array steps{AiStep::attackOneOf({attacks[0], attacks[1], attacks[2]}), AiStep::end()};
        BattleState state = battle();
        rngWhere(state, {drawsTo(3, choice)});
        giveTurn(state, steps);
        runTurn(state);
        EXPECT_EQ(attacksQueued(state.at(kMonster).actions), std::vector<AttackId>{attacks[choice]});
    }
}

TEST(AiRunTest, ASingleAttackDrawsNothing) {
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    const std::uint8_t before = state.rng.position();
    runTurn(state);
    EXPECT_EQ(state.rng.position(), before);
}

// --- targets -----------------------------------------------------------------------------------------

TEST(AiRunTest, AStepsTargetsStayForEveryLaterActionOfTheRun) {
    const std::array steps{AiStep::setTarget(AiTargetSelector::CHAR_SLOT_1), AiStep::attack(AttackId::FIRE),
                           AiStep::attack(AttackId::ICE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    const auto& actions = state.at(kMonster).actions;
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(command(actions[0]).targets, BattlerSet::of(kTerra));
    EXPECT_EQ(command(actions[1]).targets, BattlerSet::of(kTerra));
}

TEST(AiRunTest, ASkippedBlockPutsBackTheTargetsTheRunStartedWith) {
    // A turn starts with no targets; the bolt is aimed by the executor.
    const std::array steps{AiStep::setTarget(AiTargetSelector::CHAR_SLOT_1), AiStep::never(), AiStep::endIf(),
                           AiStep::attack(AttackId::BOLT), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    EXPECT_EQ(command(state.at(kMonster).actions.at(0)).targets, kAimedAt);
}

TEST(AiRunTest, ACounterattackStartsAimedAtTheBattlerItAnswers) {
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::end()};
    BattleState state = battle();
    giveCounter(state, steps);
    AimingExecutor executor;
    battle::runAiCounter(state, executor, kMonster, BattlerSet::of(kTerra));
    EXPECT_EQ(command(state.at(kMonster).actions.at(0)).targets, BattlerSet::of(kTerra));
}

TEST(AiRunTest, AConfusedBattlersCommandsGoWithNoTargetsAndAreAimed) {
    const std::array steps{AiStep::setTarget(AiTargetSelector::CHAR_SLOT_1), AiStep::attack(AttackId::FIRE),
                           AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    state.at(kMonster).status.set(StatusId::CONFUSE);
    runTurn(state);
    EXPECT_EQ(command(state.at(kMonster).actions.at(0)).targets, kAimedAt);
}

// --- the commands queued -----------------------------------------------------------------------------

TEST(AiRunTest, AnAttackGoesAsTheCommandItBelongsToWithItsAdvanceWait) {
    const std::array steps{AiStep::attack(AttackId::BATTLE), AiStep::attack(AttackId::FIRE_SKEAN),
                           AiStep::attack(AttackId::FIRE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    AimingExecutor executor;
    battle::runAiTurn(state, executor, kMonster);
    const auto& actions = state.at(kMonster).actions;
    ASSERT_EQ(actions.size(), 3u);
    EXPECT_EQ(command(actions[0]).command, BattleCommandId::FIGHT);
    EXPECT_EQ(command(actions[1]).command, BattleCommandId::MAGIC);
    EXPECT_EQ(command(actions[2]).command, BattleCommandId::MAGIC);
    EXPECT_EQ(executor.aims, 3);
    // Each command adds its wait to the battler's, from 0 as a turn starts.
    state.at(kMonster).actions.clear();
    state.at(kMonster).advanceWait = 0;
    battle::runAiTurn(state, executor, kMonster);
    EXPECT_EQ(state.at(kMonster).advanceWait,
              commandAdvanceWait(BattleCommandId::FIGHT) + 2 * commandAdvanceWait(BattleCommandId::MAGIC));
}

TEST(AiRunTest, LoresRouletteGoesAsTheAisRouletteWithNoWaitOrAim) {
    const std::array steps{AiStep::attack(AttackId::ROULETTE), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    AimingExecutor executor;
    battle::runAiTurn(state, executor, kMonster);
    EXPECT_EQ(command(state.at(kMonster).actions.at(0)).command, battle::kRouletteCommand);
    EXPECT_EQ(executor.aims, 0);
    EXPECT_FALSE(state.at(kMonster).advanceWait.has_value());
}

TEST(AiRunTest, OneOfThreeCommandsGoesWithAttackZero) {
    const std::array steps{AiStep::commandOneOf({BattleCommandId::STEAL, BattleCommandId::STEAL, BattleCommandId::STEAL}),
                           AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    runTurn(state);
    EXPECT_EQ(command(state.at(kMonster).actions.at(0)).command, BattleCommandId::STEAL);
    EXPECT_EQ(command(state.at(kMonster).actions.at(0)).attack, AttackId{});
}

TEST(AiRunTest, AnItemStepUsesTheFirstItemTwoTimesInThree) {
    for (int choice = 0; choice < 3; ++choice) {
        const std::array steps{AiStep::throwItemOneOf({.twoInThree = ItemId::TONIC, .oneInThree = ItemId::POTION}),
                               AiStep::end()};
        BattleState state = battle();
        rngWhere(state, {drawsTo(3, choice)});
        giveTurn(state, steps);
        const ItemId expected = choice == 2 ? ItemId::POTION : ItemId::TONIC;
        runTurn(state);
        EXPECT_EQ(command(state.at(kMonster).actions.at(0)).command, BattleCommandId::THROW);
        EXPECT_EQ(command(state.at(kMonster).actions.at(0)).attack, static_cast<AttackId>(expected));
    }
}

TEST(AiRunTest, EveryOtherStepQueuesItsOwnActionWithNoWait) {
    const std::array steps{
        AiStep::changeBattle({.formation = static_cast<FormationId>(424),
                              .exit = MonsterEntranceType::PRE_DRAWN,
                              .restoreMonsters = true,
                              .scrollBackground = true}),
        AiStep::dialog(35),
        AiStep::showOrHideMonsters({.mode = MonsterVisibilityMode::Hide,
                                    .monsters = AiMonsters::self(),
                                    .type = MonsterEntranceType::PRE_DRAWN}),
        AiStep::battleEvent(13),
        AiStep::addToVariable({.variable = AiVariable::own(), .amount = 1}),
        AiStep::setSwitch(AiSwitch::battleSwitch(73)),
        AiStep::animate({.animation = MonsterAnimation::FlashRed,
                         .monsters = AiMonsters::of(AiTargetSelector::MONSTER_SLOT_2)}),
        AiStep::makeInvincible(AiTargetSelector::SELF),
        AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    AimingExecutor executor;
    battle::runAiTurn(state, executor, kMonster);
    const auto& actions = state.at(kMonster).actions;
    ASSERT_EQ(actions.size(), 8u);
    const auto& change = std::get<battle::FormationChange>(actions[0]);
    EXPECT_EQ(change.formation, static_cast<FormationId>(424));
    EXPECT_TRUE(change.restoreMonsters);
    EXPECT_TRUE(change.scrollBackground);
    EXPECT_EQ(std::get<battle::MonsterDialogAction>(actions[1]).dialog, 35);
    const auto& hide = std::get<battle::MonsterVisibilityAction>(actions[2]);
    EXPECT_EQ(hide.mode, MonsterVisibilityMode::Hide);
    EXPECT_EQ(hide.monsters, BattlerSet::of(kMonster));
    EXPECT_EQ(std::get<battle::BattleEventAction>(actions[3]).event, 13);
    const auto& variable = std::get<battle::BattleVariableAction>(actions[4]);
    EXPECT_TRUE(variable.variable.isOwn());
    EXPECT_EQ(variable.change, AiVariableChange::Add);
    EXPECT_EQ(variable.amount, 1);
    EXPECT_EQ(std::get<battle::BattleSwitchAction>(actions[5]).which, AiSwitch::battleSwitch(73));
    EXPECT_EQ(std::get<battle::MonsterAnimationAction>(actions[6]).monsters, BattlerSet::of(kOtherMonster));
    const auto& effect = std::get<battle::AiEffectAction>(actions[7]);
    EXPECT_EQ(effect.effect, AiEffect::MakeInvincible);
    EXPECT_EQ(effect.target, AiTarget{AiTargetSelector::SELF});
    EXPECT_EQ(executor.aims, 0);
    EXPECT_FALSE(state.at(kMonster).advanceWait.has_value());
}

TEST(AiRunTest, APartyMemberShowingOrHidingItselfNamesNoMonster) {
    const std::array steps{AiStep::showOrHideMonsters({.mode = MonsterVisibilityMode::Hide,
                                                       .monsters = AiMonsters::self(),
                                                       .type = MonsterEntranceType::PRE_DRAWN}),
                           AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps, kTerra);
    runTurn(state, kTerra);
    EXPECT_TRUE(std::get<battle::MonsterVisibilityAction>(state.at(kTerra).actions.at(0)).monsters.empty());
}

TEST(AiRunTest, WhileACounterattackRunsTheActionsAreCounterattacks) {
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::dialog(3), AiStep::end()};
    BattleState state = battle();
    giveTurn(state, steps);
    state.counterattackRunning = true;
    runTurn(state);
    EXPECT_TRUE(state.at(kMonster).actions.empty());
    EXPECT_EQ(state.at(kMonster).counterActions.size(), 2u);
    EXPECT_TRUE(state.counterQueue.contains(kMonster));
}

// --- the counterattack ---------------------------------------------------------------------------------

// A part that acts only if it runs freely, then a block an always() opens.
const std::array kCounter{AiStep::attack(AttackId::FIRE), AiStep::endIf(), AiStep::always(),
                          AiStep::attack(AttackId::ICE), AiStep::end()};

std::vector<AttackId> counterattack(BattleState& state) {
    giveCounter(state, kCounter);
    state.counterattackRunning = true;
    AimingExecutor executor;
    battle::runAiCounter(state, executor, kMonster, BattlerSet::of(kTerra));
    return attacksQueued(state.at(kMonster).counterActions);
}

const std::vector<AttackId> kFree{AttackId::FIRE};
const std::vector<AttackId> kRestricted{AttackId::ICE};

TEST(AiCounterTest, ABattlerFreeToAnswerRunsItsCounterattackPart) {
    BattleState state = battle();
    EXPECT_EQ(counterattack(state), kFree);
    EXPECT_TRUE(state.counterAiRanThisStep.has(kMonster));
}

TEST(AiCounterTest, ThePartRunsOnceAStep) {
    BattleState state = battle();
    state.counterAiRanThisStep.add(kMonster);
    EXPECT_TRUE(counterattack(state).empty());
}

TEST(AiCounterTest, ABattlerThatDiedAfterAnotherAttackedItAnswersFreely) {
    BattleState state = battle();
    state.diedAwaitingCounter.add(kMonster);
    state.attackedThisStep.add(kMonster);
    EXPECT_EQ(counterattack(state), kFree);
    EXPECT_FALSE(state.diedAwaitingCounter.has(kMonster));
    EXPECT_FALSE(state.attackedThisStep.has(kMonster));
}

TEST(AiCounterTest, ABattlerThatDiedWithNoOneAttackingItIsRestricted) {
    BattleState state = battle();
    state.diedAwaitingCounter.add(kMonster);
    EXPECT_EQ(counterattack(state), kRestricted);
    EXPECT_FALSE(state.diedAwaitingCounter.has(kMonster));
}

TEST(AiCounterTest, QuickRunningForAnotherBattlerRestricts) {
    BattleState state = battle();
    state.quickTarget = kTerra;
    EXPECT_EQ(counterattack(state), kRestricted);
    BattleState own = battle();
    own.quickTarget = kMonster;
    EXPECT_EQ(counterattack(own), kFree);
}

TEST(AiCounterTest, EachStatusItAnswersWithThatHoldsItBackRestricts) {
    for (const StatusId status :
         {StatusId::SLEEP, StatusId::CONFUSE, StatusId::BERSERK, StatusId::STOP, StatusId::FROZEN}) {
        BattleState state = battle();
        state.at(kMonster).statusForCounter.set(status);
        EXPECT_EQ(counterattack(state), kRestricted) << static_cast<int>(status);
    }
    // Only the statuses it answers with count, not the ones it has now.
    BattleState now = battle();
    now.at(kMonster).status.set(StatusId::SLEEP);
    EXPECT_EQ(counterattack(now), kFree);
}

TEST(AiCounterTest, ACharmedBattlerIsRestricted) {
    BattleState state = battle();
    state.at(kMonster).charmedBy = kTerra;
    EXPECT_EQ(counterattack(state), kRestricted);
}

TEST(AiCounterTest, MonstersDeadLiftsTheRestrictionForItsBlock) {
    // The third slot holds a monster that is no longer alive, so the test holds.
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::endIf(),
                           AiStep::ifMonstersDead(AiMonsters::of(AiTargetSelector::MONSTER_SLOT_3)),
                           AiStep::attack(AttackId::ICE), AiStep::end()};
    BattleState state = battle();
    state.at(BattlerSlot::monster(2)).monster = MonsterId::LOBO;
    state.diedAwaitingCounter.add(kMonster);
    giveCounter(state, steps);
    battle::ActionExecutor executor;
    battle::runAiCounter(state, executor, kMonster, BattlerSet{});
    EXPECT_EQ(attacksQueued(state.at(kMonster).actions), std::vector<AttackId>{AttackId::ICE});
}

TEST(AiCounterTest, AMonstersDeadThatFailsLeavesTheRunRestricted) {
    // ifMonstersDead lifts the restriction before it tests, but a block that fails puts it back.
    const std::array steps{AiStep::ifMonstersDead(AiMonsters::of(AiTargetSelector::MONSTER_SLOT_3)), AiStep::endIf(),
                           AiStep::attack(AttackId::FIRE), AiStep::end()};
    BattleState state = battle();
    state.diedAwaitingCounter.add(kMonster);
    giveCounter(state, steps);
    battle::ActionExecutor executor;
    battle::runAiCounter(state, executor, kMonster, BattlerSet{});
    EXPECT_TRUE(state.at(kMonster).actions.empty());
}

TEST(AiCounterTest, ASkippedBlockRestrictsTheRunAgain) {
    const std::array steps{AiStep::always(), AiStep::never(), AiStep::endIf(), AiStep::attack(AttackId::FIRE),
                           AiStep::end()};
    BattleState state = battle();
    state.diedAwaitingCounter.add(kMonster);
    giveCounter(state, steps);
    battle::ActionExecutor executor;
    battle::runAiCounter(state, executor, kMonster, BattlerSet{});
    EXPECT_TRUE(state.at(kMonster).actions.empty());
}

TEST(AiCounterTest, ACounterattackKeepsItsOwnPlaceToResume) {
    const std::array steps{AiStep::attack(AttackId::FIRE), AiStep::wait(), AiStep::attack(AttackId::ICE), AiStep::end()};
    BattleState state = battle();
    giveCounter(state, steps);
    battle::ActionExecutor executor;
    battle::runAiCounter(state, executor, kMonster, BattlerSet{});
    EXPECT_EQ(state.at(kMonster).counterResume.at, &steps[2]);
    EXPECT_EQ(state.at(kMonster).turnResume.blocksSkipped, battle::kNoResume);
}

// --- readying a script ------------------------------------------------------------------------------

TEST(AiReadyTest, AScriptsTwoPartsAreWhereTheScriptTableHoldsThem) {
    BattleState state = battle();
    state.at(kMonster).aiScript = static_cast<std::uint16_t>(MonsterId::LOBO);
    battle::readyAi(state, kMonster);
    const AiScript& lobo = aiScript(MonsterId::LOBO);
    EXPECT_EQ(state.at(kMonster).turnScript.data(), lobo.turn.data());
    EXPECT_EQ(state.at(kMonster).hasCounterScript, lobo.hasCounter());
}

TEST(AiReadyTest, TheMagRoaderAnswersWithWildCatsTurnPart) {
    BattleState state = battle();
    state.at(kMonster).aiScript = static_cast<std::uint16_t>(MonsterId::MAG_ROADER_4);
    battle::readyAi(state, kMonster);
    EXPECT_EQ(state.at(kMonster).counterScript.data(), aiScript(MonsterId::WILD_CAT).turn.data());
    EXPECT_TRUE(state.at(kMonster).hasCounterScript);
}

TEST(AiReadyTest, ANewMonsterWithNoCounterattackPartKeepsTheSlotsAndAllItsScriptMemory) {
    const AiScript* withCounter = nullptr;
    const AiScript* without = nullptr;
    for (const AiScript& script : aiScripts()) {
        (script.hasCounter() ? withCounter : without) = &script;
    }
    ASSERT_NE(withCounter, nullptr);
    ASSERT_NE(without, nullptr);
    BattleState state = battle();
    battle::Battler& slot = state.at(kMonster);
    slot.aiScript = static_cast<std::uint16_t>(withCounter->monster);
    battle::readyAi(state, kMonster);
    slot.turnResume = battle::AiResume{.blocksSkipped = 2, .at = &withCounter->turn[1]};
    slot.counterResume = battle::AiResume{.blocksSkipped = 0, .at = &withCounter->counter[1]};
    slot.aiVariable = 9;
    slot.turnTimer = 300;

    slot.aiScript = static_cast<std::uint16_t>(without->monster);
    battle::readyAi(state, kMonster);
    EXPECT_EQ(slot.turnScript.data(), without->turn.data());
    EXPECT_EQ(slot.counterScript.data(), withCounter->counter.data());
    EXPECT_TRUE(slot.hasCounterScript);
    EXPECT_EQ(slot.turnResume.blocksSkipped, 2);
    EXPECT_EQ(slot.turnResume.at, &withCounter->turn[1]);
    EXPECT_EQ(slot.counterResume.at, &withCounter->counter[1]);
    EXPECT_EQ(slot.aiVariable, 9);
    EXPECT_EQ(slot.turnTimer, 300);
}

TEST(AiReadyTest, ABattlerWithNoScriptIsLeftAsItIs) {
    BattleState state = battle();
    battle::readyAi(state, kTerra);
    EXPECT_TRUE(state.at(kTerra).turnScript.empty());
    EXPECT_FALSE(state.at(kTerra).hasCounterScript);
}

}  // namespace
}  // namespace ostinato::test
