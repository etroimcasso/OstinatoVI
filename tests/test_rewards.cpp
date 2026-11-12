// What a won battle gives the party (docs/contracts/battle_rewards.md), each rule worked out by hand
// from the original: the fallen monsters' experience and gold and the Veldt's rule; each ally's share
// with the remainder dropped and the divider's answer for no allies; experience added in three bytes
// and held to the cap; the experience the next level needs; the natural spells, SwdTechs and Blitzes a
// level teaches over the whole tables; the spells equipment teaches with the Cursed Shield's count;
// the lores, the dance, the Morph supply and the rages.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/rewards.h"
#include "data/battle_backgrounds.h"
#include "data/battle_tables.h"
#include "data/item_properties.h"
#include "data/level_up.h"
#include "data/natural_magic.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/dance_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;
using game::SpellProgress;

// A monster in `position` that fell with `status`, worth `experience` and `gold`.
void fallen(BattleState& state, int position, StatusId status, std::uint16_t experience, std::uint16_t gold) {
    battle::Battler& monster = state.at(BattlerSlot::monster(position));
    monster.status.set(status);
    monster.experience = experience;
    monster.gold = gold;
}

std::uint8_t& percent(SpellProgress& spells, AttackId spell) {
    return spells.progress[static_cast<std::size_t>(spell)];
}

// The experience a record at `level` needs for its next level, from the table.
std::uint32_t needed(std::uint8_t level) {
    std::uint32_t steps = 0;
    for (int at = kFirstProgressionLevel; at <= level + 1; ++at) {
        steps += levelUpExp(static_cast<std::uint8_t>(at));
    }
    return steps * 8;
}

// The first item that teaches a spell, and one that teaches nothing.
ItemId firstTeachingItem() {
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.record.spellLearnRate != 0) {
            return entry.id;
        }
    }
    return ItemId::EMPTY;
}

ItemId firstNonTeachingItem() {
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.record.spellLearnRate == 0 && entry.id != ItemId::CURSED_SHLD && entry.id != ItemId::EMPTY) {
            return entry.id;
        }
    }
    return ItemId::EMPTY;
}

// --- the switches the win reads --------------------------------------------------------------------

TEST(Rewards, MagicPointsAreEarnedBySwitch67AndTheShieldsCountIsVariable12) {
    EXPECT_EQ(game::kMagicPointsEarnedSwitch, 67);  // $3ebc & $08: variable 8, bit 3
    game::BattleSwitches switches;
    switches.setCursedShieldBattles(200);
    EXPECT_EQ(switches.variables[12], 200);  // $3ec0
    EXPECT_EQ(switches.cursedShieldBattles(), 200);
    switches.set(game::kMagicPointsEarnedSwitch);
    EXPECT_EQ(switches.variables[8], 0x08);
}

// --- experience and gold ----------------------------------------------------------------------------

TEST(Rewards, DeadPetrifiedAndZombieMonstersGiveTheirExperienceAndGold) {
    BattleState state;
    fallen(state, 0, StatusId::DEAD, 100, 30);
    fallen(state, 3, StatusId::PETRIFY, 50, 20);
    fallen(state, 5, StatusId::ZOMBIE, 7, 1);
    const battle::BattleSpoils spoils = battle::spoilsOf(state);
    EXPECT_EQ(spoils.experience, 157u);
    EXPECT_EQ(spoils.gold, 51u);
}

TEST(Rewards, AMonsterThatFledOrStandsGivesNothing) {
    BattleState state;
    fallen(state, 0, StatusId::DEAD, 100, 30);
    // Fled: gone with no fallen status. Standing: Condemned is not a fall.
    state.at(BattlerSlot::monster(1)).experience = 900;
    state.at(BattlerSlot::monster(1)).gold = 900;
    fallen(state, 2, StatusId::CONDEMNED, 800, 800);
    const battle::BattleSpoils spoils = battle::spoilsOf(state);
    EXPECT_EQ(spoils.experience, 100u);
    EXPECT_EQ(spoils.gold, 30u);
}

TEST(Rewards, OnTheVeldtMonstersGiveGoldAndNoExperience) {
    BattleState state;
    state.onTheVeldt = true;
    fallen(state, 0, StatusId::DEAD, 100, 30);
    fallen(state, 1, StatusId::DEAD, 200, 40);
    const battle::BattleSpoils spoils = battle::spoilsOf(state);
    EXPECT_EQ(spoils.experience, 0u);
    EXPECT_EQ(spoils.gold, 70u);
}

TEST(Rewards, SixMonstersAtTheirMostSumPastTwoBytes) {
    BattleState state;
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        fallen(state, position, StatusId::DEAD, 65535, 65535);
    }
    EXPECT_EQ(battle::spoilsOf(state).experience, 393210u);
    EXPECT_EQ(battle::spoilsOf(state).gold, 393210u);
}

TEST(Rewards, EachAllysShareDropsTheRemainder) {
    EXPECT_EQ(battle::experienceShare(300, 3), 100u);
    EXPECT_EQ(battle::experienceShare(301, 4), 75u);
    EXPECT_EQ(battle::experienceShare(3, 4), 0u);
    EXPECT_EQ(battle::experienceShare(0x123456, 1), 0x123456u);
    EXPECT_EQ(battle::experienceShare(0xFFFFFF, 2), 0x7FFFFFu);
    EXPECT_EQ(battle::experienceShare(393210, 3), 131070u);
}

TEST(Rewards, AShareForNoAlliesIsTheDividersAnswer) {
    // Both steps of the console's divide answer 65,535 over 0.
    EXPECT_EQ(battle::experienceShare(1000, 0), 0xFFFFFFu);
    EXPECT_EQ(battle::experienceShare(0, 0), 0xFFFFFFu);
}

TEST(Rewards, ExperienceIsAddedBelowTheCap) {
    game::CharacterRecord record;
    record.experience = 1000;
    battle::addExperience(record, 234);
    EXPECT_EQ(record.experience, 1234u);
}

TEST(Rewards, ExperienceStopsAtTheCap) {
    game::CharacterRecord record;
    record.experience = kMaxExperience - 1000;
    battle::addExperience(record, 1000);
    EXPECT_EQ(record.experience, kMaxExperience);
    battle::addExperience(record, 1);
    EXPECT_EQ(record.experience, kMaxExperience);
    record.experience = kMaxExperience - 5;
    battle::addExperience(record, 10'000);
    EXPECT_EQ(record.experience, kMaxExperience);
}

TEST(Rewards, ASumPastThreeBytesKeepsItsLowThreeBytes) {
    game::CharacterRecord record;
    record.experience = kMaxExperience;
    battle::addExperience(record, 0x300000);  // 18,145,728 in three bytes is 1,368,512
    EXPECT_EQ(record.experience, 1'368'512u);
    record.experience = 0xFFFFFF;
    battle::addExperience(record, 1);
    EXPECT_EQ(record.experience, 0u);
}

TEST(Rewards, ACharacterAt99StillGainsExperience) {
    game::CharacterRecord record;
    record.level = kMaxLevel;
    record.experience = 3'000'000;
    battle::addExperience(record, 500);
    EXPECT_EQ(record.experience, 3'000'500u);
    EXPECT_EQ(record.level, kMaxLevel);
}

// --- the next level ----------------------------------------------------------------------------------

TEST(Rewards, TheNextLevelNeedsEightTimesTheStepsUpToIt) {
    game::CharacterRecord record;
    record.level = 1;
    EXPECT_EQ(needed(1), 32u);  // the step to level 2 is 4
    record.experience = 31;
    EXPECT_FALSE(battle::earnedNextLevel(record));
    record.experience = 32;
    EXPECT_TRUE(battle::earnedNextLevel(record));
    record.level = 2;
    EXPECT_EQ(needed(2), 96u);  // 4 + 8
    record.experience = 95;
    EXPECT_FALSE(battle::earnedNextLevel(record));
    record.experience = 96;
    EXPECT_TRUE(battle::earnedNextLevel(record));
}

TEST(Rewards, EveryLevelBelow99NeedsExactlyItsTotal) {
    for (int level = 1; level < kMaxLevel; ++level) {
        game::CharacterRecord record;
        record.level = static_cast<std::uint8_t>(level);
        record.experience = needed(record.level) - 1;
        EXPECT_FALSE(battle::earnedNextLevel(record)) << level;
        record.experience = needed(record.level);
        EXPECT_TRUE(battle::earnedNextLevel(record)) << level;
        EXPECT_LT(needed(record.level), 1u << 24) << level;
    }
}

TEST(Rewards, Level99NeverLevelsUp) {
    game::CharacterRecord record;
    record.level = kMaxLevel;
    record.experience = kMaxExperience;
    EXPECT_FALSE(battle::earnedNextLevel(record));
}

// --- what a level teaches ----------------------------------------------------------------------------

template <std::size_t N>
void checkNaturalMagic(CharacterPropId actor, const std::array<NaturalMagicSlot, N>& table) {
    for (int level = 1; level <= kMaxLevel; ++level) {
        game::Party party;
        SpellProgress& spells = party.spells[static_cast<std::size_t>(actor)];
        for (std::uint8_t& learned : spells.progress) {
            learned = 40;
        }
        const battle::AbilitiesLearned learned =
            battle::learnAbilitiesAtLevel(party, actor, static_cast<std::uint8_t>(level));
        EXPECT_FALSE(learned.swdtech || learned.blitz);
        SpellProgress expected;
        for (std::uint8_t& value : expected.progress) {
            value = 40;
        }
        for (const NaturalMagicSlot& row : table) {
            if (row.record.level == level) {
                percent(expected, row.record.spell) = SpellProgress::kSpellJustLearned;
            }
        }
        EXPECT_EQ(spells.progress, expected.progress) << static_cast<int>(actor) << " at " << level;
    }
}

TEST(Rewards, TerraAndCelesLearnEachNaturalSpellAtExactlyItsLevel) {
    checkNaturalMagic(CharacterPropId::TERRA, kNaturalMagicTerra);
    checkNaturalMagic(CharacterPropId::CELES, kNaturalMagicCeles);
}

TEST(Rewards, ANaturalSpellAlreadyLearnedStaysLearned) {
    game::Party party;
    SpellProgress& spells = party.spells[static_cast<std::size_t>(CharacterPropId::TERRA)];
    percent(spells, AttackId::FIRE_2) = SpellProgress::kSpellLearned;
    battle::learnAbilitiesAtLevel(party, CharacterPropId::TERRA, 22);
    EXPECT_EQ(percent(spells, AttackId::FIRE_2), SpellProgress::kSpellLearned);
    percent(spells, AttackId::FIRE_2) = 99;
    battle::learnAbilitiesAtLevel(party, CharacterPropId::TERRA, 22);
    EXPECT_EQ(percent(spells, AttackId::FIRE_2), SpellProgress::kSpellJustLearned);
    battle::learnAbilitiesAtLevel(party, CharacterPropId::TERRA, 22);
    EXPECT_EQ(percent(spells, AttackId::FIRE_2), SpellProgress::kSpellJustLearned);
}

void checkTechniques(CharacterPropId actor, BattleCommandId command) {
    const auto table = abilityLearnLevels(command);
    for (int level = 1; level <= kMaxLevel; ++level) {
        game::Party party;
        const battle::AbilitiesLearned learned =
            battle::learnAbilitiesAtLevel(party, actor, static_cast<std::uint8_t>(level));
        std::uint8_t expected = 0;
        for (std::size_t slot = 0; slot < table.size(); ++slot) {
            if (table[slot].level == level) {
                expected = static_cast<std::uint8_t>(1u << slot);
                break;
            }
        }
        const AbilityLearnedSet& known =
            command == BattleCommandId::BUSHIDO ? party.swdtechsKnown : party.blitzesKnown;
        EXPECT_EQ(known.bits, expected) << level;
        const bool newly = command == BattleCommandId::BUSHIDO ? learned.swdtech : learned.blitz;
        EXPECT_EQ(newly, expected != 0) << level;
        EXPECT_FALSE(command == BattleCommandId::BUSHIDO ? learned.blitz : learned.swdtech);
    }
}

TEST(Rewards, CyanLearnsEachSwdTechAtItsLevel) {
    checkTechniques(CharacterPropId::CYAN, BattleCommandId::BUSHIDO);
}

TEST(Rewards, SabinLearnsEachBlitzAtItsLevel) {
    checkTechniques(CharacterPropId::SABIN, BattleCommandId::BLITZ);
}

TEST(Rewards, ATechniqueAlreadyKnownIsNotLearnedAgain) {
    game::Party party;
    party.swdtechsKnown = AbilityLearnedSet{0x04};  // Slash, at 12
    EXPECT_FALSE(battle::learnAbilitiesAtLevel(party, CharacterPropId::CYAN, 12).swdtech);
    EXPECT_EQ(party.swdtechsKnown.bits, 0x04);
    party.blitzesKnown = AbilityLearnedSet{0x02};  // Aurabolt, at 6
    EXPECT_FALSE(battle::learnAbilitiesAtLevel(party, CharacterPropId::SABIN, 6).blitz);
    EXPECT_EQ(party.blitzesKnown.bits, 0x02);
}

TEST(Rewards, AnyoneElseLearnsNothingFromALevel) {
    for (const CharacterPropId actor : {CharacterPropId::LOCKE, CharacterPropId::GOGO, CharacterPropId::MOG}) {
        game::Party party;
        const game::Party before = party;
        for (int level = 1; level <= kMaxLevel; ++level) {
            const battle::AbilitiesLearned learned =
                battle::learnAbilitiesAtLevel(party, actor, static_cast<std::uint8_t>(level));
            EXPECT_FALSE(learned.swdtech || learned.blitz);
        }
        EXPECT_EQ(party.swdtechsKnown.bits, before.swdtechsKnown.bits);
        EXPECT_EQ(party.blitzesKnown.bits, before.blitzesKnown.bits);
        for (std::size_t actorSpells = 0; actorSpells < game::kSpellLearners; ++actorSpells) {
            EXPECT_EQ(party.spells[actorSpells].progress, before.spells[actorSpells].progress);
        }
    }
}

// --- the spells equipment teaches ------------------------------------------------------------------

TEST(Rewards, EveryItemWithALearnRateTeachesOneOfTheLearnableSpells) {
    int teaching = 0;
    for (const ItemPropertiesEntry& entry : itemPropertiesEn()) {
        if (entry.record.spellLearnRate != 0) {
            ++teaching;
            EXPECT_LT(static_cast<std::size_t>(entry.record.spellLearned), game::kLearnableSpells)
                << static_cast<int>(entry.id);
        }
    }
    EXPECT_GT(teaching, 0);
}

TEST(Rewards, EachEquipmentSlotTeachesItsItemsSpell) {
    const ItemId teacher = firstTeachingItem();
    const ItemProperties& item = getItemProperties(teacher);
    ItemId game::CharacterRecord::* const slots[] = {
        &game::CharacterRecord::weapon, &game::CharacterRecord::shield, &game::CharacterRecord::helmet,
        &game::CharacterRecord::armor,  &game::CharacterRecord::relic1, &game::CharacterRecord::relic2};
    for (ItemId game::CharacterRecord::* slot : slots) {
        game::CharacterRecord record;
        record.*slot = teacher;
        SpellProgress spells;
        game::BattleSwitches switches;
        const battle::EquipmentLearning learning = battle::learnFromEquipment(record, spells, switches, 1);
        EXPECT_TRUE(learning.progressed);
        EXPECT_FALSE(learning.curseDispelled);
        EXPECT_EQ(percent(spells, item.spellLearned),
                  item.spellLearnRate >= 100 ? SpellProgress::kSpellJustLearned : item.spellLearnRate);
    }
}

TEST(Rewards, EmptySlotsAndItemsThatTeachNothingChangeNothing) {
    game::CharacterRecord record;
    record.weapon = firstNonTeachingItem();
    SpellProgress spells;
    game::BattleSwitches switches;
    const battle::EquipmentLearning learning = battle::learnFromEquipment(record, spells, switches, 10);
    EXPECT_FALSE(learning.progressed);
    EXPECT_EQ(spells.progress, SpellProgress{}.progress);
}

TEST(Rewards, NoMagicPointsStillCountAsLearningFurther) {
    game::CharacterRecord record;
    record.relic2 = firstTeachingItem();
    SpellProgress spells;
    game::BattleSwitches switches;
    EXPECT_TRUE(battle::learnFromEquipment(record, spells, switches, 0).progressed);
    EXPECT_EQ(spells.progress, SpellProgress{}.progress);
}

TEST(Rewards, TheCursedShieldCountsTheBattlesWonWithIt) {
    game::CharacterRecord record;
    record.shield = ItemId::CURSED_SHLD;
    SpellProgress spells;
    game::BattleSwitches switches;
    EXPECT_FALSE(battle::learnFromEquipment(record, spells, switches, 5).curseDispelled);
    EXPECT_EQ(switches.cursedShieldBattles(), 1);
    switches.setCursedShieldBattles(254);
    EXPECT_FALSE(battle::learnFromEquipment(record, spells, switches, 5).curseDispelled);
    EXPECT_EQ(switches.cursedShieldBattles(), 255);
    EXPECT_EQ(record.shield, ItemId::CURSED_SHLD);
}

TEST(Rewards, The256thWinLiftsTheCurseAndThePaladinShieldTeachesAtOnce) {
    game::CharacterRecord record;
    record.shield = ItemId::CURSED_SHLD;
    SpellProgress spells;
    game::BattleSwitches switches;
    switches.setCursedShieldBattles(255);
    const battle::EquipmentLearning learning = battle::learnFromEquipment(record, spells, switches, 1);
    EXPECT_TRUE(learning.curseDispelled);
    EXPECT_EQ(switches.cursedShieldBattles(), 0);
    EXPECT_EQ(record.shield, ItemId::PALADIN_SHLD);
    const ItemProperties& paladin = getItemProperties(ItemId::PALADIN_SHLD);
    ASSERT_NE(paladin.spellLearnRate, 0);
    EXPECT_TRUE(learning.progressed);
    EXPECT_EQ(percent(spells, paladin.spellLearned), paladin.spellLearnRate);
}

TEST(Rewards, TwoSlotsHoldingTheCursedShieldCountTwice) {
    game::CharacterRecord record;
    record.shield = ItemId::CURSED_SHLD;
    record.helmet = ItemId::CURSED_SHLD;
    SpellProgress spells;
    game::BattleSwitches switches;
    switches.setCursedShieldBattles(254);
    const battle::EquipmentLearning learning = battle::learnFromEquipment(record, spells, switches, 1);
    EXPECT_TRUE(learning.curseDispelled);
    EXPECT_EQ(switches.cursedShieldBattles(), 0);
    // The shield's slot reaches 255; the helmet's comes round to 0 and is the one that changes.
    EXPECT_EQ(record.shield, ItemId::CURSED_SHLD);
    EXPECT_EQ(record.helmet, ItemId::PALADIN_SHLD);
}

// --- lores, the dance, the Morph supply, the rages ------------------------------------------------

TEST(Rewards, TheLoresSeenAreAddedToTheLoresKnown) {
    game::Party party;
    party.loresKnown.set(0);
    std::bitset<game::kLores> seen;
    seen.set(5);
    seen.set(23);
    battle::learnLores(party, seen);
    EXPECT_TRUE(party.loresKnown.test(0));
    EXPECT_TRUE(party.loresKnown.test(5));
    EXPECT_TRUE(party.loresKnown.test(23));
    EXPECT_EQ(party.loresKnown.count(), 3u);
}

TEST(Rewards, EveryBackgroundHasADance) {
    for (std::size_t background = 0; background < kBattleBackgroundCount; ++background) {
        const DanceId dance = backgroundDance(static_cast<BattleBackgroundId>(background));
        EXPECT_LT(static_cast<std::size_t>(dance), 8u) << background;
    }
}

TEST(Rewards, MogLearnsTheBackgroundsDanceFallenOrNot) {
    for (const bool dead : {false, true}) {
        BattleState state;
        state.at(BattlerSlot::character(2)).actor = CharacterPropId::MOG;
        if (dead) {
            state.at(BattlerSlot::character(2)).status.set(StatusId::DEAD);
        }
        state.background = BattleBackgroundId::FOREST_WOR;
        game::Party party;
        const DanceId dance = backgroundDance(BattleBackgroundId::FOREST_WOR);
        EXPECT_EQ(battle::learnDance(party, state), dance);
        EXPECT_TRUE(party.dancesKnown.test(static_cast<std::size_t>(dance)));
    }
}

TEST(Rewards, ADanceKnownOrNoMogTellsOfNothing) {
    BattleState state;
    state.background = BattleBackgroundId::DESERT_WOB;
    const DanceId dance = backgroundDance(BattleBackgroundId::DESERT_WOB);
    game::Party party;
    EXPECT_EQ(battle::learnDance(party, state), std::nullopt);  // no Mog
    EXPECT_FALSE(party.dancesKnown.any());
    state.at(BattlerSlot::character(0)).actor = CharacterPropId::MOG;
    party.dancesKnown.set(static_cast<std::size_t>(dance));
    EXPECT_EQ(battle::learnDance(party, state), std::nullopt);  // known
    EXPECT_EQ(party.dancesKnown.count(), 1u);
}

TEST(Rewards, TheMorphSupplyGainsTwiceTheMagicPointsHeldTo255) {
    game::Party party;
    party.morphSupply = 10;
    battle::gainMorphSupply(party, 10);
    EXPECT_EQ(party.morphSupply, 30);
    party.morphSupply = 235;
    battle::gainMorphSupply(party, 10);
    EXPECT_EQ(party.morphSupply, 255);
    party.morphSupply = 250;
    battle::gainMorphSupply(party, 10);
    EXPECT_EQ(party.morphSupply, 255);
}

TEST(Rewards, TheDoublingKeepsItsLowByteAndCarriesItsTopBit) {
    game::Party party;
    battle::gainMorphSupply(party, 128);
    EXPECT_EQ(party.morphSupply, 1);
    party.morphSupply = 0;
    battle::gainMorphSupply(party, 200);
    EXPECT_EQ(party.morphSupply, 145);
    party.morphSupply = 111;
    battle::gainMorphSupply(party, 200);
    EXPECT_EQ(party.morphSupply, 255);  // 144 + 1 + 111 = 256
}

TEST(Rewards, EveryMonsterNumberedBelow256TeachesItsRageAliveFallenOrGone) {
    BattleState state;
    state.at(BattlerSlot::monster(0)).monster = static_cast<MonsterId>(12);
    state.at(BattlerSlot::monster(1)).monster = static_cast<MonsterId>(255);
    state.at(BattlerSlot::monster(1)).status.set(StatusId::DEAD);
    state.at(BattlerSlot::monster(3)).monster = static_cast<MonsterId>(256);
    state.at(BattlerSlot::monster(5)).monster = static_cast<MonsterId>(300);
    game::Party party;
    battle::learnRages(party, state);
    EXPECT_TRUE(party.ragesKnown.test(12));
    EXPECT_TRUE(party.ragesKnown.test(255));
    EXPECT_EQ(party.ragesKnown.count(), 2u);
}

}  // namespace
}  // namespace ostinato::test
