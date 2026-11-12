// Who a monster's AI script aims at (battle/ai_targets.h, docs/contracts/battle_ai.md): every one of
// the thirty selectors, a party member by who it is and each monster slot, with and without the cut to
// the battlers an attack can reach, over one fixed battle (tests/ai_battle.h). Each answer is worked out
// by hand from the original, and the port and the cartridge's own routine (CheckAITarget, run on the
// oracle) must both give it.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include "ai_battle.h"
#include "battle/ai_targets.h"
#include "data/rng_table.h"
#include "ostinato/ai_target_selector.h"
#include "ostinato/character_prop_id.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using battle::BattlerSlot;
using trace::AiScenario;
using trace::AiTargetsResult;

// The battlers the cut leaves: on the field, not fallen or hidden, not held in Seize, not away.
const BattlerSet kReachable = setOf(kTerra, kSelf, kReflecting);
// Every party member and monster in the battle, on the field or not.
const BattlerSet kInTheBattle = setOf(kTerra, kLocke, kEdgar, kSelf, kReflecting, kFallen, kHidden, kJumping);

// The port's and the cartridge's answer for `target` over `s`; they must agree.
AiTargetsResult checked(const AiScenario& s, AiTarget target, bool reachableOnly) {
    AiTargetsResult guest;
    withAiOracle([&](trace::DamageOracle& oracle) { guest = oracle.aiTargets(s, target, reachableOnly); });
    const AiTargetsResult port = trace::portAiTargets(s, target, reachableOnly);
    EXPECT_EQ(trace::differences(port, guest), "");
    return port;
}

// `target` names `expected` and draws nothing, cut or not as given.
void expectTargets(AiTarget target, bool reachableOnly, BattlerSet expected, const AiScenario& s = aiBattle()) {
    const AiTargetsResult result = checked(s, target, reachableOnly);
    EXPECT_EQ(result.targets, expected);
    EXPECT_EQ(result.any, !expected.empty());
    EXPECT_EQ(result.rngPosition, s.state.rng.position());
}

// The member a pick of one from `from` takes: the next random number scaled by the count, counted
// from the first party member up (RandBit, battle/battle_main.asm:13495-13515).
BattlerSet picked(std::uint8_t position, BattlerSet from) {
    const int index = rngByte(static_cast<std::uint8_t>(position + 1)) * from.count() / 256;
    int seen = 0;
    for (int slot = 0; slot < battle::kBattlerSlots; ++slot) {
        if (from.has(BattlerSlot::fromIndex(slot)) && seen++ == index) {
            return BattlerSet::of(BattlerSlot::fromIndex(slot));
        }
    }
    return {};
}

// `target` picks one of `from` with one random number, cut or not as given.
void expectPick(AiTarget target, bool reachableOnly, BattlerSet from) {
    const AiScenario s = aiBattle();
    const AiTargetsResult result = checked(s, target, reachableOnly);
    EXPECT_EQ(result.targets, picked(s.state.rng.position(), from));
    EXPECT_EQ(result.rngPosition, static_cast<std::uint8_t>(s.state.rng.position() + 1));
}

// --- the sides -----------------------------------------------------------------------------------

TEST(AiTargetsTest, SelfIsTheScriptsOwnMonster) {
    expectTargets(AiTargetSelector::SELF, false, BattlerSet::of(kSelf));
    expectTargets(AiTargetSelector::SELF, true, BattlerSet::of(kSelf));
}

TEST(AiTargetsTest, TheMonstersAreEveryMonsterInTheBattleOrThoseInReach) {
    expectTargets(AiTargetSelector::ALL_MONSTERS, false, kInTheBattle.monstersOnly());
    expectTargets(AiTargetSelector::ALL_MONSTERS, true, kReachable.monstersOnly());
}

TEST(AiTargetsTest, TheOtherMonstersLeaveOutTheScriptsOwn) {
    expectTargets(AiTargetSelector::ALL_OTHER_MONSTERS, false, setOf(kReflecting, kFallen, kHidden, kJumping));
    expectTargets(AiTargetSelector::ALL_OTHER_MONSTERS, true, BattlerSet::of(kReflecting));
}

TEST(AiTargetsTest, TheCharactersAreEveryPartyMemberOrThoseInReach) {
    expectTargets(AiTargetSelector::ALL_CHARS, false, setOf(kTerra, kLocke, kEdgar));
    expectTargets(AiTargetSelector::ALL_CHARS, true, BattlerSet::of(kTerra));
}

TEST(AiTargetsTest, AllTargetsAreBothSides) {
    expectTargets(AiTargetSelector::ALL_TARGETS, false, kInTheBattle);
    expectTargets(AiTargetSelector::ALL_TARGETS, true, kReachable);
}

TEST(AiTargetsTest, NothingNamesNoOne) {
    expectTargets(AiTargetSelector::NOTHING, false, {});
}

TEST(AiTargetsTest, ARandomMemberOfASideIsOnePick) {
    expectPick(AiTargetSelector::RAND_CHAR, false, setOf(kTerra, kLocke, kEdgar));
    expectPick(AiTargetSelector::RAND_MONSTER, false, kInTheBattle.monstersOnly());
    expectPick(AiTargetSelector::RAND_OTHER_MONSTER, false, setOf(kReflecting, kFallen, kHidden, kJumping));
    expectPick(AiTargetSelector::RAND_MONSTER, true, kReachable.monstersOnly());
}

TEST(AiTargetsTest, APickFromNoOneDrawsNothing) {
    // With Terra off the field no party member is in reach: Locke is dead and Edgar held in Seize.
    AiScenario s = aiBattle();
    s.state.at(kTerra).present = false;
    const AiTargetsResult result = checked(s, AiTargetSelector::RAND_CHAR, true);
    EXPECT_TRUE(result.targets.empty());
    EXPECT_FALSE(result.any);
    EXPECT_EQ(result.rngPosition, s.state.rng.position());
}

// --- the dead and the reflecting -------------------------------------------------------------------

TEST(AiTargetsTest, TheDeadAreFoundByTheirStatusWhateverTheCut) {
    for (const bool reachableOnly : {false, true}) {
        expectTargets(AiTargetSelector::ALL_DEAD_CHARS, reachableOnly, BattlerSet::of(kLocke));
        expectTargets(AiTargetSelector::ALL_DEAD_MONSTERS, reachableOnly, BattlerSet::of(kFallen));
    }
    expectPick(AiTargetSelector::RAND_DEAD_CHAR, true, BattlerSet::of(kLocke));
    expectPick(AiTargetSelector::RAND_DEAD_MONSTER, false, BattlerSet::of(kFallen));
}

TEST(AiTargetsTest, TheReflectingAreFoundByTheirStatusWhateverTheCut) {
    for (const bool reachableOnly : {false, true}) {
        expectTargets(AiTargetSelector::ALL_REFLECT_CHARS, reachableOnly, BattlerSet::of(kEdgar));
        expectTargets(AiTargetSelector::ALL_REFLECT_MONSTERS, reachableOnly, BattlerSet::of(kReflecting));
    }
    expectPick(AiTargetSelector::RAND_REFLECT_CHAR, true, BattlerSet::of(kEdgar));
    expectPick(AiTargetSelector::RAND_REFLECT_MONSTER, false, BattlerSet::of(kReflecting));
}

TEST(AiTargetsTest, ADeadBattlerNotInTheBattleIsStillFound) {
    // The dead are read from every slot's statuses, held or empty.
    AiScenario s = aiBattle();
    s.state.at(kEmptyMonsterSlot).status.set(StatusId::DEAD);
    expectTargets(AiTargetSelector::ALL_DEAD_MONSTERS, true, setOf(kFallen, kEmptyMonsterSlot), s);
}

// --- one battler ---------------------------------------------------------------------------------

TEST(AiTargetsTest, APartyMemberByWhoItIsIsTheSlotItIsIn) {
    expectTargets(CharacterPropId::TERRA, true, BattlerSet::of(kTerra));
    // Found whatever the cut: dead, or held in Seize.
    expectTargets(CharacterPropId::LOCKE, true, BattlerSet::of(kLocke));
    expectTargets(CharacterPropId::EDGAR, true, BattlerSet::of(kEdgar));
    expectTargets(CharacterPropId::BANON, false, {});
}

TEST(AiTargetsTest, AMonsterSlotIsItsMonsterWhereverItIs) {
    expectTargets(AiTargetSelector::MONSTER_SLOT_1, true, BattlerSet::of(kSelf));
    expectTargets(AiTargetSelector::MONSTER_SLOT_3, true, BattlerSet::of(kFallen));
    expectTargets(AiTargetSelector::MONSTER_SLOT_4, true, BattlerSet::of(kHidden));
    expectTargets(AiTargetSelector::MONSTER_SLOT_5, false, {});
    expectTargets(AiTargetSelector::MONSTER_SLOT_6, true, BattlerSet::of(kJumping));
}

TEST(AiTargetsTest, APartySlotIsItsMemberWhileOnTheField) {
    expectTargets(AiTargetSelector::CHAR_SLOT_1, true, BattlerSet::of(kTerra));
    expectTargets(AiTargetSelector::CHAR_SLOT_2, true, BattlerSet::of(kLocke));
    expectTargets(AiTargetSelector::CHAR_SLOT_3, false, BattlerSet::of(kEdgar));
    expectTargets(AiTargetSelector::CHAR_SLOT_4, false, {});
}

TEST(AiTargetsTest, ThePreviousAttackerIsTheLastToAimAtItUnlessItIsStillToBeAnswered) {
    expectTargets(AiTargetSelector::PREV_ATTACKER, true, BattlerSet::of(kTerra));
    AiScenario waiting = aiBattle();
    waiting.state.at(kSelf).retaliationPending = true;
    expectTargets(AiTargetSelector::PREV_ATTACKER, false, {}, waiting);
    AiScenario none = aiBattle();
    none.state.at(kSelf).lastAttacker.reset();
    expectTargets(AiTargetSelector::PREV_ATTACKER, false, {}, none);
}

TEST(AiTargetsTest, TheTargettingMarkIsUsedUpAsItIsRead) {
    const AiScenario s = aiBattle();
    const AiTargetsResult result = checked(s, AiTargetSelector::TARGETTING_TARGET, true);
    EXPECT_EQ(result.targets, BattlerSet::of(kEdgar));
    EXPECT_EQ(result.targetingTarget, std::nullopt);
}

TEST(AiTargetsTest, ATargettingMarkOnADeadBattlerIsUsedUpAndNamesNoOne) {
    AiScenario s = aiBattle();
    s.state.at(kSelf).targetingTarget = kLocke;
    const AiTargetsResult result = checked(s, AiTargetSelector::TARGETTING_TARGET, false);
    EXPECT_TRUE(result.targets.empty());
    EXPECT_EQ(result.targetingTarget, std::nullopt);
    AiScenario unmarked = aiBattle();
    unmarked.state.at(kSelf).targetingTarget.reset();
    expectTargets(AiTargetSelector::TARGETTING_TARGET, false, {}, unmarked);
}

// --- a random target -------------------------------------------------------------------------------

TEST(AiTargetsTest, ARandomTargetIsEveryoneElseOnOneFaceOfTheCoinAndOneOfThemOnTheOther) {
    const BattlerSet others = kInTheBattle.without(BattlerSet::of(kSelf));
    int everyone = 0;
    int one = 0;
    for (int position = 0; position < 256; position += 37) {
        AiScenario s = aiBattle();
        s.state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
        const AiTargetsResult result = checked(s, AiTargetSelector::RAND_TARGET, false);
        const bool heads = (rngByte(static_cast<std::uint8_t>(position + 1)) & 1) != 0;
        if (heads) {
            EXPECT_EQ(result.targets, others);
            EXPECT_EQ(result.rngPosition, static_cast<std::uint8_t>(position + 1));
            ++everyone;
        } else {
            EXPECT_EQ(result.targets, picked(static_cast<std::uint8_t>(position + 1), others));
            EXPECT_EQ(result.rngPosition, static_cast<std::uint8_t>(position + 2));
            ++one;
        }
    }
    EXPECT_GT(everyone, 0);
    EXPECT_GT(one, 0);
}

// --- the cut and the reach flag ----------------------------------------------------------------------

TEST(AiTargetsTest, ABattlerAwayFromTheFieldIsInReachOnceTheLastCommandAimedAtItsOwnAttacker) {
    AiScenario s = aiBattle();
    s.state.awayFromTheFieldUnreachable = false;
    expectTargets(AiTargetSelector::ALL_MONSTERS, true, setOf(kSelf, kReflecting, kJumping), s);
}

TEST(AiTargetsTest, AMonsterItsAiHasMadeUntargetableIsOutOfReach) {
    AiScenario s = aiBattle();
    s.state.aiUntargetable = BattlerSet::of(kReflecting);
    expectTargets(AiTargetSelector::ALL_TARGETS, true, setOf(kTerra, kSelf), s);
    expectTargets(AiTargetSelector::ALL_TARGETS, false, kInTheBattle, s);
}

TEST(AiTargetsTest, ABattlerNotCountedInTheBattleIsOutOfReach) {
    AiScenario s = aiBattle();
    s.state.countedInBattle.remove(kTerra);
    expectTargets(AiTargetSelector::ALL_CHARS, true, {}, s);
}

}  // namespace
}  // namespace ostinato::test
