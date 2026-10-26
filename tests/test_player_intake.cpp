// The player's finished commands coming into the battle: GetPlayerAction, FixPlayerAttack and
// FixPlayerCmd, battle/battle_main.asm:12620-12785.

#include <gtest/gtest.h>

#include <optional>
#include <variant>
#include <vector>

#include "battle/player_intake.h"
#include "battle_test_support.h"
#include "data/battle_commands.h"
#include "data/battle_tables.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleState;
using battle::CommandAction;
using battle::PlayerAction;

const BattlerSlot kHero = BattlerSlot::character(0);
const BattlerSlot kFriend = BattlerSlot::character(1);
const BattlerSet kAtMonster = BattlerSet::of(BattlerSlot::monster(0));

// An executor whose choosers answer with fixed attacks and count their calls.
struct Choosers : RecordingExecutor {
    int dances = 0;
    int rages = 0;
    int espers = 0;
    AttackId chooseDanceStep(BattleState&, BattlerSlot, AttackId) override {
        ++dances;
        return AttackId{0x65};
    }
    AttackId chooseRageAttack(BattleState&, BattlerSlot, AttackId) override {
        ++rages;
        return AttackId{0x66};
    }
    AttackId chooseRandomEsper(BattleState&, BattlerSlot, AttackId) override {
        ++espers;
        return AttackId::RAMUH;
    }
};

std::vector<CommandAction> takeIn(BattleState& state, battle::ActionExecutor& executor, PlayerAction chosen) {
    state.chosenActions.push_back(chosen);
    battle::takePlayerActions(state, executor);
    std::vector<CommandAction> added;
    for (const battle::Action& action : state.at(chosen.character).actions) {
        added.push_back(std::get<CommandAction>(action));
    }
    return added;
}

TEST(PlayerIntake, AChosenCommandStepsIntoTheAdvanceWaitLineWithItsMenuClosed) {
    // :12621-12641: the advance-wait line, menu bits 2 and 7 cleared, the action added.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.at(kHero).inBattleMenu = true;
    state.at(kHero).menuMayOpen = true;
    state.at(kHero).takesMenuInput = true;
    state.at(kHero).skipAdvanceWait = true;
    const auto added = takeIn(state, executor, {.character = kHero, .command = BattleCommandId::FIGHT,
                                                .targets = kAtMonster});
    EXPECT_TRUE(state.advanceWaitQueue.contains(kHero));
    EXPECT_FALSE(state.at(kHero).inBattleMenu);
    EXPECT_FALSE(state.at(kHero).menuMayOpen);
    EXPECT_TRUE(state.at(kHero).takesMenuInput);
    EXPECT_FALSE(state.at(kHero).skipAdvanceWait);
    ASSERT_EQ(added.size(), 1u);
    EXPECT_EQ(added[0].command, BattleCommandId::FIGHT);
    EXPECT_EQ(added[0].targets, kAtMonster);
    EXPECT_TRUE(state.chosenActions.empty());
}

TEST(PlayerIntake, EveryWaitingChoiceIsTakenInTheOrderItWasMade) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.chosenActions.push_back({.character = kFriend, .command = BattleCommandId::FIGHT});
    state.chosenActions.push_back({.character = kHero, .command = BattleCommandId::FIGHT});
    battle::takePlayerActions(state, executor);
    ASSERT_EQ(state.advanceWaitQueue.size(), 2u);
    EXPECT_EQ(state.advanceWaitQueue.at(0), kFriend);
    EXPECT_EQ(state.advanceWaitQueue.at(1), kHero);
}

TEST(PlayerIntake, XMagicBecomesTwoSpellsTheFirstAsMagic) {
    // :12642-12651 and :12691-12693.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    const BattlerSet second = BattlerSet::of(BattlerSlot::monster(1));
    const auto added = takeIn(state, executor, {.character = kHero, .command = BattleCommandId::X_MAGIC,
                                                .attack = AttackId::FIRE, .targets = kAtMonster,
                                                .secondAttack = AttackId::ICE, .secondTargets = second});
    ASSERT_EQ(added.size(), 2u);
    EXPECT_EQ(added[0].command, BattleCommandId::MAGIC);
    EXPECT_EQ(added[0].attack, AttackId::FIRE);
    EXPECT_EQ(added[0].targets, kAtMonster);
    EXPECT_EQ(added[1].command, BattleCommandId::X_MAGIC);
    EXPECT_EQ(added[1].attack, AttackId::ICE);
    EXPECT_EQ(added[1].targets, second);
}

TEST(PlayerIntake, SummoningTheEquippedEsperNamesIt) {
    // :12694-12704 then the Summon base, :12764-12774.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.at(kHero).spells.esper = EsperId::SHIVA;
    const auto added = takeIn(state, executor, {.character = kHero, .command = BattleCommandId::SUMMON,
                                                .attack = AttackId::NONE});
    ASSERT_EQ(added.size(), 1u);
    EXPECT_EQ(static_cast<int>(added[0].attack), static_cast<int>(EsperId::SHIVA));
    EXPECT_TRUE(state.espersSummoned.has(kHero));
}

TEST(PlayerIntake, TheFiveCountedCommandsAddTheirBaseAndOverflowToAPlainAttack) {
    // CmdWithAttackTbl / CmdAttackOffsetTbl, :12764-12774: past 255 is BATTLE ($ee).
    for (const CommandAttackBase& base : kCommandAttackBases) {
        BattleState state = makeBattle();
        battle::NullExecutor executor;
        state.party.blitzesKnown = AbilityLearnedSet{0xFF};
        const auto first = takeIn(state, executor, {.character = kHero, .command = base.command, .attack = AttackId{}});
        EXPECT_EQ(first.back().attack, base.attackBase) << static_cast<int>(base.command);
        const auto last = takeIn(state, executor, {.character = kHero, .command = base.command,
                                                   .attack = static_cast<AttackId>(255 - static_cast<int>(base.attackBase) + 1)});
        EXPECT_EQ(last.back().attack, AttackId::BATTLE) << static_cast<int>(base.command);
    }
}

TEST(PlayerIntake, ABlitzThePartyHasNotLearnedBecomesAPlainAttack) {
    // :12749-12763.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.party.blitzesKnown = AbilityLearnedSet{0x03};  // the first two
    const auto known = takeIn(state, executor, {.character = kHero, .command = BattleCommandId::BLITZ,
                                                .attack = AttackId{1}});
    EXPECT_EQ(static_cast<int>(known.back().attack), 0x5D + 1);
    const auto unknown = takeIn(state, executor, {.character = kHero, .command = BattleCommandId::BLITZ,
                                                  .attack = AttackId{2}});
    EXPECT_EQ(unknown.back().attack, AttackId::BATTLE);
}

TEST(PlayerIntake, ItemsAndThrowsTakeTheItemInHand) {
    // :12705-12711.
    for (BattleCommandId command : {BattleCommandId::ITEM, BattleCommandId::THROW}) {
        BattleState state = makeBattle();
        battle::NullExecutor executor;
        takeIn(state, executor, {.character = kHero, .command = command, .attack = static_cast<AttackId>(ItemId::POTION)});
        EXPECT_EQ(state.at(kHero).itemInHand, ItemId::POTION);
    }
}

TEST(PlayerIntake, ASlotReelNamesItsSpellAndTheJokerDoomsAimThemselves) {
    // :12712-12731: SlotAttackTbl; outcomes 0 and 1 take their side's targets from JokerTargetTbl,
    // and the party's byte has its characters fighting for the other side flipped.
    for (std::size_t outcome = 0; outcome < kSlotOutcomes.size(); ++outcome) {
        BattleState state = makeBattle();
        Choosers executor;
        state.at(kFriend).actsAsEnemy = true;
        const auto added = takeIn(state, executor, {.character = kHero, .command = BattleCommandId::SLOT,
                                                    .attack = static_cast<AttackId>(outcome), .targets = {}});
        const AttackId spell = kSlotOutcomes[outcome].attack;
        EXPECT_EQ(added.back().attack, spell == AttackId::NONE ? AttackId::RAMUH : spell) << outcome;
        EXPECT_EQ(executor.espers, spell == AttackId::NONE ? 1 : 0) << outcome;
        if (outcome < 2) {
            BattleSlotMask characters = outcome == 0 ? kJokerDoomTargets[0].targets : BattleSlotMask{};
            characters.bits = static_cast<std::uint8_t>(characters.bits ^ 0x02);
            EXPECT_EQ(added.back().targets.characters.bits, characters.bits) << outcome;
            if (outcome == 1) {
                EXPECT_EQ(added.back().targets.monsters.bits, kJokerDoomTargets[1].targets.bits);
            }
        }
    }
}

TEST(PlayerIntake, DanceAndRageRememberTheChoiceAndLetTheChooserPickTheAttack) {
    // :12732-12748.
    BattleState state = makeBattle();
    Choosers executor;
    const auto dance = takeIn(state, executor, {.character = kHero, .command = BattleCommandId::DANCE,
                                                .attack = static_cast<AttackId>(DanceId{3})});
    EXPECT_EQ(state.at(kHero).dance, DanceId{3});
    EXPECT_EQ(state.lastDance, DanceId{3});
    EXPECT_EQ(dance.back().attack, AttackId{0x65});
    EXPECT_EQ(executor.dances, 1);

    const auto rage = takeIn(state, executor, {.character = kFriend, .command = BattleCommandId::RAGE,
                                               .attack = static_cast<AttackId>(MonsterId::LOBO)});
    EXPECT_EQ(state.at(kFriend).rage, MonsterId::LOBO);
    EXPECT_EQ(rage.back().attack, AttackId{0x66});
    EXPECT_EQ(executor.rages, 1);

    takeIn(state, executor, {.character = kFriend, .command = BattleCommandId::RAGE, .attack = AttackId::NONE});
    EXPECT_FALSE(state.at(kFriend).rage.has_value());
}

TEST(PlayerIntake, TheCommandsThatAimAgainDropTheirTargets) {
    // RetargetCmdTbl, :12775-12781.
    for (BattleCommandId command : {BattleCommandId::BUSHIDO, BattleCommandId::BLITZ, BattleCommandId::RAGE,
                                    BattleCommandId::LEAP, BattleCommandId::DANCE, BattleCommandId::FIGHT}) {
        BattleState state = makeBattle();
        battle::NullExecutor executor;
        state.party.blitzesKnown = AbilityLearnedSet{0xFF};
        const auto added = takeIn(state, executor, {.character = kHero, .command = command, .targets = kAtMonster});
        EXPECT_EQ(added.back().targets.empty(), kRetargetCommands.has(command)) << static_cast<int>(command);
    }
    EXPECT_FALSE(kRetargetCommands.has(BattleCommandId::FIGHT));
}

TEST(PlayerIntake, AConfusedOrZombieCharacterAimsWhenItActs) {
    // FixPlayerCmd :12671-12680, for X-Magic's second spell too.
    for (StatusId status : {StatusId::CONFUSE, StatusId::ZOMBIE}) {
        BattleState state = makeBattle();
        battle::NullExecutor executor;
        state.at(kHero).status.set(status);
        const auto added = takeIn(state, executor, {.character = kHero, .command = BattleCommandId::X_MAGIC,
                                                    .attack = AttackId::FIRE, .targets = kAtMonster,
                                                    .secondAttack = AttackId::FIRE, .secondTargets = kAtMonster});
        ASSERT_EQ(added.size(), 2u);
        EXPECT_TRUE(added[0].targets.empty());
        EXPECT_TRUE(added[1].targets.empty());
    }
}

TEST(PlayerIntake, ChoicesWaitWhileTimeIsStoppedAndAreTakenEvenWhileAMenuHoldsTime) {
    // UpdateBattleTime :2668-2674: the intake comes first, before the wait-mode check, but only
    // on the frames battle time runs at all.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.chosenActions.push_back({.character = kHero, .command = BattleCommandId::FIGHT});
    battle::tickBattleTime(state, false, executor);
    EXPECT_EQ(state.chosenActions.size(), 1u);
    state.config.waitMode = true;
    state.timeStoppedByMenu = true;
    battle::tickBattleTime(state, true, executor);
    EXPECT_TRUE(state.chosenActions.empty());
    EXPECT_TRUE(state.advanceWaitQueue.contains(kHero));
}

}  // namespace
}  // namespace ostinato::test
