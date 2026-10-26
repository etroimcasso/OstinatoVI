// The party's battle spell lists: the shared layout, each member's own copy and its MP costs, and
// which spells can be cast. Every expectation names the line of battle/battle_main.asm it is read
// from.

#include <gtest/gtest.h>

#include <set>

#include "battle/party_setup.h"
#include "battle/spell_lists.h"
#include "data/attack_properties.h"
#include "data/battle_tables.h"
#include "battle/actions.h"
#include "battle_test_support.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;

const BattlerSlot kFirst = BattlerSlot::character(0);
const BattlerSlot kSecond = BattlerSlot::character(1);

void learn(BattleState& state, CharacterPropId actor, std::initializer_list<AttackId> spells) {
    for (AttackId spell : spells) {
        state.party.spells[static_cast<std::size_t>(actor)].progress[static_cast<std::size_t>(spell)] =
            game::SpellProgress::kSpellLearned;
    }
}

std::optional<AttackId> at(const BattleState& state, BattlerSlot slot, std::size_t place) {
    const auto& entry = state.at(slot).spells.magic[place];
    return entry ? std::optional<AttackId>(entry->spell) : std::nullopt;
}

// A party placed and loaded, its members knowing what the test taught them first.
BattleState loaded(BattleState state, std::uint8_t magicOrder = 2, game::PlayTime saved = {}) {
    state.config.magicOrder = magicOrder;
    battle::placeParty(state, {});
    battle::loadParty(state, {}, saved);
    return state;
}

TEST(SpellLayout, RowsOfThreeWithNoKnownSpellAreLeftOut) {
    // InitSpellList :14101-14148 under setting 2, whose offsets are all 0.
    BattleState state = battleWithParty({CharacterPropId::CELES});
    learn(state, CharacterPropId::CELES, {AttackId::FIRE, AttackId::SCAN, AttackId::CURE});
    state = loaded(state);
    EXPECT_EQ(at(state, kFirst, 0), AttackId::FIRE);
    EXPECT_EQ(at(state, kFirst, 1), std::nullopt);
    EXPECT_EQ(at(state, kFirst, 3), AttackId::SCAN);
    EXPECT_EQ(at(state, kFirst, 6), AttackId::CURE);
    for (std::size_t place = 7; place < game::kLearnableSpells; ++place) {
        EXPECT_EQ(at(state, kFirst, place), std::nullopt) << place;
    }
}

TEST(SpellLayout, TheOrderSettingMovesEachBand) {
    // :14105-14121: setting 0 puts white magic first, then black, then effect.
    BattleState state = battleWithParty({CharacterPropId::CELES});
    learn(state, CharacterPropId::CELES, {AttackId::FIRE, AttackId::SCAN, AttackId::CURE});
    const BattleState first = loaded(state, 0);
    EXPECT_EQ(at(first, kFirst, 0), AttackId::CURE);
    EXPECT_EQ(at(first, kFirst, 3), AttackId::FIRE);
    EXPECT_EQ(at(first, kFirst, 6), AttackId::SCAN);

    // Setting 1: black +30, effect -15, white -45.
    const BattleState second = loaded(state, 1);
    EXPECT_EQ(at(second, kFirst, 0), AttackId::CURE);
    EXPECT_EQ(at(second, kFirst, 3), AttackId::SCAN);
    EXPECT_EQ(at(second, kFirst, 6), AttackId::FIRE);
}

TEST(SpellLayout, EverySettingPlacesEachSpellInItsOwnPlace) {
    // The three offset tables send the 54 spells to 54 distinct places in 0-53 under every setting.
    for (std::uint8_t setting = 0; setting < kBlackMagicOrder.size(); ++setting) {
        std::set<int> places;
        for (int spell = 0; spell < static_cast<int>(game::kLearnableSpells); ++spell) {
            const auto& band = spell < static_cast<int>(AttackId::SCAN)   ? kBlackMagicOrder
                               : spell < static_cast<int>(AttackId::CURE) ? kEffectMagicOrder
                                                                         : kWhiteMagicOrder;
            const int place = spell + band[setting].offset;
            EXPECT_GE(place, 0);
            EXPECT_LT(place, static_cast<int>(game::kLearnableSpells));
            places.insert(place);
        }
        EXPECT_EQ(places.size(), game::kLearnableSpells) << static_cast<int>(setting);
    }
}

TEST(SpellLists, EachMemberKeepsTheirOwnSpellsAtThePartysPlaces) {
    // :14150-14196 builds every member the whole list; ValidateSpellList :14248-14284 keeps a
    // member's own.
    BattleState state = battleWithParty({CharacterPropId::CELES, CharacterPropId::TERRA});
    learn(state, CharacterPropId::CELES, {AttackId::FIRE});
    learn(state, CharacterPropId::TERRA, {AttackId::ICE});
    state = loaded(state);
    EXPECT_EQ(at(state, kFirst, 0), AttackId::FIRE);
    EXPECT_EQ(at(state, kFirst, 1), std::nullopt);
    EXPECT_EQ(at(state, kSecond, 0), std::nullopt);
    EXPECT_EQ(at(state, kSecond, 1), AttackId::ICE);
    EXPECT_EQ(state.at(kFirst).spellCount, 1);
}

TEST(SpellLists, GogoKeepsEverySpellAndUmaroKeepsNothing) {
    // AddToSpellList_00 :14289-14295, AddToSpellList_01 :14279-14284.
    BattleState state = battleWithParty({CharacterPropId::CELES, CharacterPropId::GOGO, CharacterPropId::UMARO});
    learn(state, CharacterPropId::CELES, {AttackId::FIRE, AttackId::CURE});
    state.party.loresKnown.set(0);
    state = loaded(state);
    const BattlerSlot gogo = kSecond;
    const BattlerSlot umaro = BattlerSlot::character(2);
    EXPECT_EQ(at(state, gogo, 0), AttackId::FIRE);
    EXPECT_EQ(at(state, gogo, 3), AttackId::CURE);
    EXPECT_EQ(state.at(gogo).spellCount, 2);
    EXPECT_TRUE(state.at(gogo).spells.lores[0].has_value());
    EXPECT_EQ(at(state, umaro, 0), std::nullopt);
    EXPECT_FALSE(state.at(umaro).spells.lores[0].has_value());
    EXPECT_EQ(state.at(umaro).spellCount, 0);
}

TEST(SpellLists, LoresStandInTheirOwnPlacesForEveryoneWithASpellListAndAreNotCounted) {
    // :14060-14079: lore n at its own place; ValidateSpellList keeps them for the first twelve.
    BattleState state = battleWithParty({CharacterPropId::STRAGO, CharacterPropId::LOCKE});
    state.party.loresKnown.set(0);
    state.party.loresKnown.set(14);
    state = loaded(state);
    EXPECT_EQ(state.loresKnown, 2);
    ASSERT_TRUE(state.at(kFirst).spells.lores[0].has_value());
    EXPECT_EQ(state.at(kFirst).spells.lores[0]->spell, AttackId::CONDEMNED);
    EXPECT_EQ(state.at(kFirst).spells.lores[14]->spell, AttackId::STEP_MINE);
    EXPECT_FALSE(state.at(kFirst).spells.lores[1].has_value());
    EXPECT_TRUE(state.at(kSecond).spells.lores[0].has_value());
    EXPECT_EQ(state.at(kFirst).spellCount, 0);
}

TEST(SpellLists, StepMineCostsOnePerHalfHourOfTheTimeLastSaved) {
    // :14187-14192: hours x 2, plus one from the half hour, of the play time at the last save.
    BattleState state = battleWithParty({CharacterPropId::STRAGO});
    state.party.loresKnown.set(14);
    EXPECT_EQ(loaded(state, 2, {.hours = 3, .minutes = 29}).at(kFirst).spells.lores[14]->mpCost, 6);
    EXPECT_EQ(loaded(state, 2, {.hours = 3, .minutes = 30}).at(kFirst).spells.lores[14]->mpCost, 7);
    EXPECT_EQ(loaded(state, 2, {.hours = 0, .minutes = 0}).at(kFirst).spells.lores[14]->mpCost, 0);
}

TEST(SpellLists, AGoldHairpinHalvesCostsRoundingUpAndAnEconomizerMakesThemOne) {
    // CalcMPCost :14321-14336.
    // A spell with an odd cost, so that halving it has something to round.
    AttackId odd = AttackId::FIRE;
    for (std::size_t spell = 0; spell < game::kLearnableSpells; ++spell) {
        if (getAttackProperties(static_cast<AttackId>(spell)).mpCost % 2 == 1) {
            odd = static_cast<AttackId>(spell);
            break;
        }
    }
    const int cost = getAttackProperties(odd).mpCost;
    ASSERT_EQ(cost % 2, 1);
    BattleState state = battleWithParty({CharacterPropId::CELES});
    learn(state, CharacterPropId::CELES, {odd});
    const auto costOf = [&](const BattleState& s) { return s.at(kFirst).spells.find(odd)->mpCost; };
    EXPECT_EQ(costOf(loaded(state)), cost);

    state.party.characters[0].relic1 = ItemId::GOLD_HAIRPIN;
    EXPECT_EQ(costOf(loaded(state)), (cost + 1) / 2);

    state.party.characters[0].relic2 = ItemId::ECONOMIZER;
    EXPECT_EQ(costOf(loaded(state)), 1);
}

TEST(SpellLists, TheEquippedEsperIsTheListsSummon) {
    // ValidateSpellList :14217-14231.
    BattleState state = battleWithParty({CharacterPropId::CELES});
    state.party.characters[0].esper = EsperId::SHIVA;
    state.party.characters[0].relic1 = ItemId::GOLD_HAIRPIN;
    state = loaded(state);
    const battle::SpellList& list = state.at(kFirst).spells;
    EXPECT_EQ(list.esper, EsperId::SHIVA);
    EXPECT_EQ(list.summon.spell, AttackId::SHIVA);
    EXPECT_EQ(list.summon.mpCost, (getAttackProperties(AttackId::SHIVA).mpCost + 1) / 2);
    EXPECT_EQ(list.summon.targeting.bits, getAttackProperties(AttackId::SHIVA).targeting.bits);
}

TEST(CastableSpells, AffordableSpellsAreCastableAndAnImpCastsOnlyImp) {
    // UpdateEnabledMagic :14362-14397 and CheckMagicEnabled :14407-14420.
    BattleState state = battleWithParty({CharacterPropId::CELES});
    learn(state, CharacterPropId::CELES, {AttackId::FIRE, AttackId::IMP, AttackId::ULTIMA});
    state.party.characters[0].esper = EsperId::RAMUH;
    state = loaded(state);
    battle::Battler& celes = state.at(kFirst);
    const int fire = getAttackProperties(AttackId::FIRE).mpCost;
    celes.mp = static_cast<std::uint16_t>(fire);
    battle::refreshCastableSpells(state, kFirst);
    const auto* fireEntry = celes.spells.find(AttackId::FIRE);
    const auto* ultima = celes.spells.find(AttackId::ULTIMA);
    ASSERT_NE(fireEntry, nullptr);
    ASSERT_NE(ultima, nullptr);
    EXPECT_TRUE(fireEntry->castable);
    EXPECT_FALSE(ultima->castable);

    celes.mp = 999;
    celes.status.set(StatusId::IMP);
    battle::refreshCastableSpells(state, kFirst);
    EXPECT_FALSE(celes.spells.find(AttackId::FIRE)->castable);
    EXPECT_TRUE(celes.spells.find(AttackId::IMP)->castable);
    EXPECT_FALSE(celes.spells.summon.castable);
}

TEST(CastableSpells, AnEsperOnceSummonedStaysUncastable) {
    // :14380-14387.
    BattleState state = battleWithParty({CharacterPropId::CELES});
    state.party.characters[0].esper = EsperId::RAMUH;
    state = loaded(state);
    state.at(kFirst).mp = 999;
    battle::refreshCastableSpells(state, kFirst);
    EXPECT_TRUE(state.at(kFirst).spells.summon.castable);
    state.espersSummoned.add(kFirst);
    battle::refreshCastableSpells(state, kFirst);
    EXPECT_FALSE(state.at(kFirst).spells.summon.castable);
}

TEST(CastableSpells, TheMpLimitIsAtMost255) {
    // :14369-14377: a member with 255 MP or more can cast anything costing under 255.
    BattleState state = battleWithParty({CharacterPropId::CELES});
    learn(state, CharacterPropId::CELES, {AttackId::FIRE});
    state = loaded(state);
    battle::Battler& celes = state.at(kFirst);
    celes.mp = 999;
    celes.spells.magic[0]->mpCost = 254;
    battle::refreshCastableSpells(state, kFirst);
    EXPECT_TRUE(celes.spells.magic[0]->castable);
    celes.spells.magic[0]->mpCost = 255;
    battle::refreshCastableSpells(state, kFirst);
    EXPECT_FALSE(celes.spells.magic[0]->castable);
}

TEST(CastableSpells, TheBattleRefreshesThemWhenAnActionLeavesThatPending) {
    // AfterAction2 :1324-1377 consumes the pending update with UpdateEnabledMagic.
    BattleState state = makeBattle();
    const BattlerSlot hero = BattlerSlot::character(0);
    state.at(hero).spells.magic[0] = battle::SpellListEntry{.spell = AttackId::FIRE, .mpCost = 5};
    state.at(hero).mp = 999;
    RecordingExecutor executor;
    executor.onExecute = [hero](BattleState& s, std::optional<BattlerSlot>, const battle::Action&) {
        s.at(hero).pendingUpdates.enabledMagic = true;
    };
    battle::Battler& monster = state.at(BattlerSlot::monster(0));
    monster.gauge.setMeter(0);
    monster.atbStopped = true;
    monster.actionQueued = true;
    battle::addAction(state, BattlerSlot::monster(0), battle::CommandAction{});
    state.actionQueue.push(BattlerSlot::monster(0));
    EXPECT_FALSE(state.at(hero).spells.magic[0]->castable);
    stepUntilIdle(state, executor);
    EXPECT_TRUE(state.at(hero).spells.magic[0]->castable);
}

}  // namespace
}  // namespace ostinato::test
