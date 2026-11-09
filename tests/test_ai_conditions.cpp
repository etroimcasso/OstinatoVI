// What a monster's AI script tests (battle/ai_conditions.h, docs/contracts/battle_ai.md): every one of
// the conditions a script can hold, true and false, over one fixed battle (tests/ai_battle.h) — the
// targets each leaves, the restriction two of them lift, the random number the status tests draw, the
// test on the monster itself for a monster and for a party member with a script, and the count of
// monster names the one-kind test reads. Each answer is worked out by hand from the original, and the
// port and the cartridge's own condition routine (run on the oracle) must both give it.

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>

#include "ai_battle.h"
#include "battle/ai_conditions.h"
#include "battle/battle_core.h"
#include "data/ai_scripts.h"
#include "game/battle_switches.h"
#include "ostinato/ai_target_selector.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/element.h"
#include "ostinato/element_set.h"
#include "ostinato/formation_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"

namespace ostinato::test {
namespace {

using battle::BattlerSet;
using trace::AiConditionResult;
using trace::AiScenario;

// The run's targets before a condition is tested, so a condition that leaves them shows.
const BattlerSet kTargetsBefore = setOf(kTerra, kReflecting);

AiScenario conditionBattle() {
    AiScenario s = aiBattle();
    s.run.targets = kTargetsBefore;
    s.run.startingTargets = kTargetsBefore;
    return s;
}

// The port's and the cartridge's answer for `condition` over `s`; they must agree.
AiConditionResult checked(const AiScenario& s, const AiCondition& condition) {
    AiConditionResult guest;
    withAiOracle([&](trace::DamageOracle& oracle) { guest = oracle.aiCondition(s, condition); });
    const AiConditionResult port = trace::portAiCondition(s, condition);
    EXPECT_EQ(trace::differences(port, guest), "");
    return port;
}

// `condition` holds or fails as `holds` says and leaves `targets`, drawing nothing.
void expectCondition(const AiCondition& condition, bool holds, BattlerSet targets = kTargetsBefore,
                     const AiScenario& s = conditionBattle()) {
    const AiConditionResult result = checked(s, condition);
    EXPECT_EQ(result.holds, holds);
    EXPECT_EQ(result.targets, targets);
    EXPECT_EQ(result.restricted, s.run.restricted);
    EXPECT_EQ(result.rngPosition, s.state.rng.position());
}

// The same battle with the script run by Terra, a party member a scripted battle gave one.
AiScenario partyMemberScript() {
    AiScenario s = conditionBattle();
    s.actor = kTerra;
    s.state.at(kTerra).aiScript = static_cast<std::uint16_t>(kAiScriptCount - 1);
    return s;
}

// --- what last hit it -----------------------------------------------------------------------------

TEST(AiConditionsTest, NeverHolds) {
    expectCondition(ai::Never{}, false);
}

TEST(AiConditionsTest, AHitByOneOfTwoCommandsAimsAtWhoeverMadeIt) {
    AiScenario s = conditionBattle();
    s.state.at(kSelf).lastHit.command = battle::Hit<BattleCommandId>{.by = kEdgar, .with = BattleCommandId::MAGIC};
    expectCondition(ai::HitByCommand{BattleCommandId::FIGHT, BattleCommandId::MAGIC}, true, BattlerSet::of(kEdgar), s);
    expectCondition(ai::HitByCommand{BattleCommandId::FIGHT, BattleCommandId::ITEM}, false, kTargetsBefore, s);
    expectCondition(ai::HitByCommand{BattleCommandId::MAGIC, BattleCommandId::MAGIC}, false);
}

TEST(AiConditionsTest, AHitByOneOfTwoSpellsAimsAtWhoeverCastIt) {
    AiScenario s = conditionBattle();
    s.state.at(kSelf).lastHit.spell = battle::Hit<AttackId>{.by = kLocke, .with = AttackId::ICE};
    expectCondition(ai::HitByAttack{AttackId::ICE, AttackId::FIRE}, true, BattlerSet::of(kLocke), s);
    expectCondition(ai::HitByAttack{AttackId::FIRE, AttackId::FIRE}, false, kTargetsBefore, s);
}

TEST(AiConditionsTest, AHitByOneOfTwoItemsAimsAtWhoeverUsedIt) {
    AiScenario s = conditionBattle();
    s.state.at(kSelf).lastHit.item = battle::Hit<ItemId>{.by = kTerra, .with = ItemId::POTION};
    expectCondition(ai::HitByItem{ItemId::TONIC, ItemId::POTION}, true, BattlerSet::of(kTerra), s);
    expectCondition(ai::HitByItem{ItemId::TONIC, ItemId::TONIC}, false, kTargetsBefore, s);
}

TEST(AiConditionsTest, AHitWithOneOfTheElementsAimsAtWhoeverMadeIt) {
    AiScenario s = conditionBattle();
    s.state.at(kSelf).lastHit.element =
        battle::Hit<ElementSet>{.by = kEdgar, .with = ElementSet::of(Element::FIRE, Element::WIND)};
    expectCondition(ai::HitByElement{ElementSet::of(Element::WIND)}, true, BattlerSet::of(kEdgar), s);
    expectCondition(ai::HitByElement{ElementSet::of(Element::ICE, Element::WATER)}, false, kTargetsBefore, s);
}

TEST(AiConditionsTest, BeingDamagedAimsAtWhoeverDidIt) {
    AiScenario s = conditionBattle();
    s.state.at(kSelf).lastDamagedBy = kLocke;
    expectCondition(ai::Damaged{}, true, BattlerSet::of(kLocke), s);
    expectCondition(ai::Damaged{}, false);
}

// --- the target test and what it reads -------------------------------------------------------------

TEST(AiConditionsTest, ATestOnTheMonsterItselfLeavesItAsItsOwnTarget) {
    expectCondition(ai::TargetValid{AiTargetSelector::SELF}, true, BattlerSet::of(kSelf));
}

TEST(AiConditionsTest, ATestOnAPartyMemberItselfLeavesItNoTarget) {
    expectCondition(ai::TargetValid{AiTargetSelector::SELF}, true, {}, partyMemberScript());
}

TEST(AiConditionsTest, ATestOnATargetThatNamesNoOneFailsAndLeavesTheTargets) {
    expectCondition(ai::TargetValid{CharacterPropId::BANON}, false);
    expectCondition(ai::TargetValid{AiTargetSelector::MONSTER_SLOT_5}, false);
    // Whole sides: the fallen and those held are named.
    expectCondition(ai::TargetValid{AiTargetSelector::ALL_CHARS}, true, setOf(kTerra, kLocke, kEdgar));
}

TEST(AiConditionsTest, HpAtMostReadsTheTargetInTheHighestSlot) {
    // The monster itself: 1,000 HP.
    expectCondition(ai::HpAtMost{.target = AiTargetSelector::SELF, .hp = 1024}, true, BattlerSet::of(kSelf));
    expectCondition(ai::HpAtMost{.target = AiTargetSelector::SELF, .hp = 896}, false, BattlerSet::of(kSelf));
    // The value itself counts.
    AiScenario atTheLine = conditionBattle();
    atTheLine.state.at(kSelf).hp = 1024;
    expectCondition(ai::HpAtMost{.target = AiTargetSelector::SELF, .hp = 1024}, true, BattlerSet::of(kSelf), atTheLine);
    // The party: Edgar is read, not Terra, though Terra's 500 HP is under the line.
    AiScenario s = conditionBattle();
    s.state.at(kEdgar).hp = 700;
    expectCondition(ai::HpAtMost{.target = AiTargetSelector::ALL_CHARS, .hp = 640}, false,
                    setOf(kTerra, kLocke, kEdgar), s);
    expectCondition(ai::HpAtMost{.target = AiTargetSelector::ALL_CHARS, .hp = 768}, true,
                    setOf(kTerra, kLocke, kEdgar), s);
}

TEST(AiConditionsTest, MpAtMostIncludesTheValue) {
    expectCondition(ai::MpAtMost{.target = AiTargetSelector::SELF, .mp = 10}, true, BattlerSet::of(kSelf));
    expectCondition(ai::MpAtMost{.target = AiTargetSelector::SELF, .mp = 9}, false, BattlerSet::of(kSelf));
}

TEST(AiConditionsTest, ALevelTestIncludesTheValueAndItsOppositeHoldsWhenNoOneIsNamed) {
    expectCondition(ai::LevelAtLeast{.target = AiTargetSelector::SELF, .level = 10}, true, BattlerSet::of(kSelf));
    expectCondition(ai::LevelAtLeast{.target = AiTargetSelector::SELF, .level = 11}, false, BattlerSet::of(kSelf));
    expectCondition(ai::LevelBelow{.target = AiTargetSelector::SELF, .level = 11}, true, BattlerSet::of(kSelf));
    expectCondition(ai::LevelBelow{.target = CharacterPropId::BANON, .level = 1}, true);
}

TEST(AiConditionsTest, AWeaknessTestSharesAnElement) {
    AiScenario s = conditionBattle();
    s.state.at(kSelf).elementsWeak = ElementSet::of(Element::FIRE);
    expectCondition(ai::WeakTo{.target = AiTargetSelector::SELF, .elements = ElementSet::of(Element::FIRE, Element::ICE)},
                    true, BattlerSet::of(kSelf), s);
    expectCondition(ai::WeakTo{.target = AiTargetSelector::SELF, .elements = ElementSet::of(Element::ICE)}, false,
                    BattlerSet::of(kSelf), s);
}

// --- statuses ------------------------------------------------------------------------------------

TEST(AiConditionsTest, AStatusTestDrawsTheTargetFromThoseWithTheStatus) {
    const AiScenario s = conditionBattle();
    const AiConditionResult result = checked(s, ai::HasStatus{.target = AiTargetSelector::ALL_CHARS, .status = StatusId::DEAD});
    EXPECT_TRUE(result.holds);
    EXPECT_EQ(result.targets, BattlerSet::of(kLocke));
    // A pick of one still draws.
    EXPECT_EQ(result.rngPosition, static_cast<std::uint8_t>(s.state.rng.position() + 1));
}

TEST(AiConditionsTest, AStatusNoOneHasFailsWithNoTargetAndNoDraw) {
    expectCondition(ai::HasStatus{.target = AiTargetSelector::ALL_CHARS, .status = StatusId::PETRIFY}, false, {});
    expectCondition(ai::NoStatus{.target = AiTargetSelector::ALL_CHARS, .status = StatusId::PETRIFY}, true, {});
}

TEST(AiConditionsTest, ALaterStatusIsLookedForOnlyAmongTheAlive) {
    // Reflect is a status from Dance on: Locke, dead, is not looked at though he has it.
    AiScenario s = conditionBattle();
    s.state.at(kLocke).status.set(StatusId::REFLECT);
    const AiConditionResult result =
        checked(s, ai::HasStatus{.target = AiTargetSelector::ALL_CHARS, .status = StatusId::REFLECT});
    EXPECT_TRUE(result.holds);
    EXPECT_EQ(result.targets, BattlerSet::of(kEdgar));
}

TEST(AiConditionsTest, NoStatusFailsWhenSomeoneHasItAndHasStillDrawn) {
    const AiScenario s = conditionBattle();
    const AiConditionResult result = checked(s, ai::NoStatus{.target = AiTargetSelector::ALL_CHARS, .status = StatusId::DEAD});
    EXPECT_FALSE(result.holds);
    EXPECT_EQ(result.targets, BattlerSet::of(kLocke));
    EXPECT_EQ(result.rngPosition, static_cast<std::uint8_t>(s.state.rng.position() + 1));
}

TEST(AiConditionsTest, NoStatusHoldsWhenTheTargetNamesNoOne) {
    expectCondition(ai::NoStatus{.target = CharacterPropId::BANON, .status = StatusId::DEAD}, true);
}

// --- the battle's numbers --------------------------------------------------------------------------

TEST(AiConditionsTest, TheTimersAreComparedAtHalf) {
    AiScenario s = conditionBattle();
    s.state.at(kSelf).turnTimer = 21;
    s.state.battleCounter = 30;
    expectCondition(ai::HalfOwnTimerAtLeast{10}, true, kTargetsBefore, s);
    expectCondition(ai::HalfOwnTimerAtLeast{11}, false, kTargetsBefore, s);
    expectCondition(ai::HalfBattleTimerAtLeast{15}, true, kTargetsBefore, s);
    expectCondition(ai::HalfBattleTimerAtLeast{16}, false, kTargetsBefore, s);
}

TEST(AiConditionsTest, AVariableIsAtLeastOrBelowAValue) {
    AiScenario s = conditionBattle();
    s.state.battleVariables[2] = 5;
    s.state.at(kSelf).aiVariable = 3;
    expectCondition(ai::VariableAtLeast{.variable = AiVariable::battle(2), .value = 5}, true, kTargetsBefore, s);
    expectCondition(ai::VariableBelow{.variable = AiVariable::battle(2), .value = 5}, false, kTargetsBefore, s);
    expectCondition(ai::VariableBelow{.variable = AiVariable::battle(2), .value = 6}, true, kTargetsBefore, s);
    expectCondition(ai::VariableAtLeast{.variable = AiVariable::own(), .value = 4}, false, kTargetsBefore, s);
}

TEST(AiConditionsTest, ASwitchIsABitOfAVariableOrABattleSwitch) {
    AiScenario s = conditionBattle();
    s.state.battleVariables[2] = 5;
    s.state.at(kSelf).aiVariable = 3;
    s.state.switches.set(game::kGauObtainedSwitch);
    expectCondition(ai::SwitchSet{AiSwitch::battleVariable(2, 2)}, true, kTargetsBefore, s);
    expectCondition(ai::SwitchSet{AiSwitch::battleVariable(2, 1)}, false, kTargetsBefore, s);
    expectCondition(ai::SwitchClear{AiSwitch::battleVariable(2, 1)}, true, kTargetsBefore, s);
    expectCondition(ai::SwitchSet{AiSwitch::own(1)}, true, kTargetsBefore, s);
    expectCondition(ai::SwitchSet{AiSwitch::battleSwitch(game::kGauObtainedSwitch)}, true, kTargetsBefore, s);
    expectCondition(ai::SwitchClear{AiSwitch::battleSwitch(game::kGauObtainedSwitch + 1)}, true, kTargetsBefore, s);
}

TEST(AiConditionsTest, ThePartyAndMonsterCountsIncludeTheValue) {
    // Two party members and three enemies still fighting.
    expectCondition(ai::PartyAtLeast{2}, true);
    expectCondition(ai::PartyAtLeast{3}, false);
    expectCondition(ai::MonstersAtMost{3}, true);
    expectCondition(ai::MonstersAtMost{2}, false);
}

TEST(AiConditionsTest, GauNotRecruitedHoldsUntilGauJoins) {
    expectCondition(ai::GauNotRecruited{}, true);
    AiScenario s = conditionBattle();
    s.state.gauRecruited = true;
    expectCondition(ai::GauNotRecruited{}, false, kTargetsBefore, s);
}

TEST(AiConditionsTest, TheFormationIsTheOneBeingFought) {
    AiScenario s = conditionBattle();
    s.state.formation = static_cast<FormationId>(424);
    expectCondition(ai::FormationIs{static_cast<FormationId>(424)}, true, kTargetsBefore, s);
    expectCondition(ai::FormationIs{static_cast<FormationId>(425)}, false, kTargetsBefore, s);
}

// --- the monsters ----------------------------------------------------------------------------------

TEST(AiConditionsTest, OneKindOfMonsterIsOneNameListedOrNone) {
    for (const std::uint8_t names : std::initializer_list<std::uint8_t>{0, 1, 2, 4}) {
        AiScenario s = conditionBattle();
        s.state.monsterNamesListed = names;
        expectCondition(ai::OneKindOfMonster{}, names <= 1, kTargetsBefore, s);
    }
}

TEST(AiConditionsTest, MonstersAliveIsEveryOneNamed) {
    expectCondition(ai::MonstersAlive{AiMonsters::of(AiTargetSelector::MONSTER_SLOT_1, AiTargetSelector::MONSTER_SLOT_2)},
                    true);
    expectCondition(ai::MonstersAlive{AiMonsters::of(AiTargetSelector::MONSTER_SLOT_1, AiTargetSelector::MONSTER_SLOT_3)},
                    false);
    expectCondition(ai::MonstersAlive{AiMonsters::self()}, true);
    // Every slot: there are only six monsters, so the two bits past them are never alive.
    expectCondition(ai::MonstersAlive{AiMonsters::all()}, false);
    // A party member's own slot is no monster's: every one of none is alive.
    expectCondition(ai::MonstersAlive{AiMonsters::self()}, true, kTargetsBefore, partyMemberScript());
}

TEST(AiConditionsTest, MonstersDeadAreHeldAndNotAliveAndTheTestLiftsTheRestriction) {
    AiScenario s = conditionBattle();
    s.run.restricted = true;
    const auto dead = [&](AiMonsters monsters, bool holds) {
        const AiConditionResult result = checked(s, ai::MonstersDead{monsters});
        EXPECT_EQ(result.holds, holds);
        EXPECT_FALSE(result.restricted);
    };
    dead(AiMonsters::of(AiTargetSelector::MONSTER_SLOT_3, AiTargetSelector::MONSTER_SLOT_4), true);
    dead(AiMonsters::of(AiTargetSelector::MONSTER_SLOT_3, AiTargetSelector::MONSTER_SLOT_5), false);
    dead(AiMonsters::self(), false);
}

TEST(AiConditionsTest, InSlotIsTheMonstersOwnSlot) {
    expectCondition(ai::InSlot{AiMonsters::of(AiTargetSelector::MONSTER_SLOT_1, AiTargetSelector::MONSTER_SLOT_4)}, true);
    expectCondition(ai::InSlot{AiMonsters::of(AiTargetSelector::MONSTER_SLOT_2)}, false);
    expectCondition(ai::InSlot{AiMonsters::of(AiTargetSelector::MONSTER_SLOT_1)}, false, kTargetsBefore,
                    partyMemberScript());
}

TEST(AiConditionsTest, AlwaysHoldsAndLiftsTheRestriction) {
    AiScenario s = conditionBattle();
    s.run.restricted = true;
    const AiConditionResult result = checked(s, ai::Always{});
    EXPECT_TRUE(result.holds);
    EXPECT_FALSE(result.restricted);
    // No other condition lifts it.
    EXPECT_TRUE(checked(s, ai::PartyAtLeast{0}).restricted);
}

// --- the names listed --------------------------------------------------------------------------------

TEST(AiConditionsTest, TheNamesListedCountEachNameOfTheMonstersStillFighting) {
    battle::BattleState state;
    const auto monster = [&](int position, MonsterId name, bool hidden = false) {
        battle::Battler& battler = state.at(battle::BattlerSlot::monster(position));
        battler.present = true;
        battler.monster = name;
        battler.nameGroup = name;
        battler.nameHidden = hidden;
    };
    monster(0, MonsterId::LOBO);
    monster(1, MonsterId::LOBO);
    monster(2, MonsterId::GUARD);
    monster(3, MonsterId::EARTHGUARD, true);
    battle::refreshRunDifficulty(state);
    EXPECT_EQ(state.monsterNamesListed, 2);
    // A fallen monster's name is not listed.
    state.at(battle::BattlerSlot::monster(2)).status.set(StatusId::DEAD);
    battle::refreshRunDifficulty(state);
    EXPECT_EQ(state.monsterNamesListed, 1);
}

TEST(AiConditionsTest, TheNamesListedAreAtMostFour) {
    battle::BattleState state;
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        battle::Battler& battler = state.at(battle::BattlerSlot::monster(position));
        battler.present = true;
        battler.monster = static_cast<MonsterId>(position);
        battler.nameGroup = static_cast<MonsterId>(position);
    }
    battle::refreshRunDifficulty(state);
    EXPECT_EQ(state.monsterNamesListed, battle::kMonsterNameLines);
}

}  // namespace
}  // namespace ostinato::test
