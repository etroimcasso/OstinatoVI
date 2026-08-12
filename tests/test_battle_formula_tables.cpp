// Full-corpus tests of the battle formula-support and mapping tables and the
// character level-progression cluster. The byte-equivalence tests assert EVERY
// modeled row carries the ROM value for its column, and that the rows the port
// does not model (the dance/background/item-type padding) still hold their known
// ROM bytes. The semantic tests exercise the accessors, the version-forked MP
// curve, the signed evade and spell-order offsets, and the decodes the wrappers
// expose.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "data/battle_tables.h"
#include "data/level_up.h"

#include "ostinato/ai_script_command.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/dance_id.h"
#include "ostinato/game_version.h"
#include "ostinato/item_type.h"
#include "ostinato/level_mod.h"

#include "fixtures/battle_formula_tables_expected.h"
#include "fixtures/level_up_expected.h"

namespace {

using ostinato::AiScriptCommand;
using ostinato::AttackId;
using ostinato::BattleBackgroundId;
using ostinato::BattleCommandId;
using ostinato::DanceId;
using ostinato::GameVersion;
using ostinato::ItemType;
using ostinato::LevelMod;

namespace fx = ostinato::test;

// --- probability ladders -----------------------------------------------------

TEST(DanceStepThresholds, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kDanceStepThresholds.size(), fx::kExpectedDanceRate.size());
    for (std::size_t i = 0; i < ostinato::kDanceStepThresholds.size(); ++i) {
        EXPECT_EQ(ostinato::kDanceStepThresholds[i].index, i);
        EXPECT_EQ(ostinato::kDanceStepThresholds[i].threshold.value(),
                  fx::kExpectedDanceRate[i]) << "threshold " << i;
    }
}

// The ladder rises, which is what makes the cumulative walk meaningful.
TEST(DanceStepThresholds, RiseMonotonically) {
    for (std::size_t i = 1; i < ostinato::kDanceStepThresholds.size(); ++i) {
        EXPECT_LT(ostinato::kDanceStepThresholds[i - 1].threshold.value(),
                  ostinato::kDanceStepThresholds[i].threshold.value());
    }
}

TEST(RandomBitRates, AreByteIdenticalToRom) {
    std::size_t at = 0;
    for (const auto& row : ostinato::kRandomBitRates) {
        for (const auto& weight : row.weights) {
            ASSERT_LT(at, fx::kExpectedRandBitRate.size());
            EXPECT_EQ(weight.value(), fx::kExpectedRandBitRate[at])
                << "weight " << at;
            ++at;
        }
    }
    EXPECT_EQ(at, fx::kExpectedRandBitRate.size());
}

// --- equipment evade ---------------------------------------------------------

// The boosts are signed; the ROM stores them as two's-complement words.
TEST(EquipEvadeBoost, MatchesRomAsSignedWords) {
    ASSERT_EQ(ostinato::kEquipEvadeBoost.size(), fx::kExpectedEquipEvade.size());
    for (std::size_t i = 0; i < ostinato::kEquipEvadeBoost.size(); ++i) {
        EXPECT_EQ(ostinato::kEquipEvadeBoost[i].index, i);
        const auto raw =
            static_cast<std::uint16_t>(ostinato::kEquipEvadeBoost[i].boost);
        EXPECT_EQ(raw, fx::kExpectedEquipEvade[i]) << "boost " << i;
    }
    EXPECT_EQ(ostinato::equipEvadeBoost(5), 50);
    EXPECT_EQ(ostinato::equipEvadeBoost(10), -50);
}

// --- final battle chain ------------------------------------------------------

TEST(FinalBattleChain, NamesTheKefkaTierFormations) {
    ASSERT_EQ(ostinato::kFinalBattleFormations.size(),
              fx::kExpectedFinalBattleId.size());
    for (std::size_t i = 0; i < ostinato::kFinalBattleFormations.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint16_t>(ostinato::kFinalBattleFormations[i]),
                  fx::kExpectedFinalBattleId[i]) << "formation " << i;
    }
    EXPECT_EQ(ostinato::kFinalBattleFormations.back(),
              ostinato::FormationId::FINAL_KEFKA);
}

TEST(FinalBattleScroll, IsByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kFinalBattleScroll.size(),
              fx::kExpectedFinalBattleScroll.size());
    for (std::size_t i = 0; i < ostinato::kFinalBattleScroll.size(); ++i) {
        EXPECT_EQ(ostinato::kFinalBattleScroll[i].index, i);
        EXPECT_EQ(ostinato::kFinalBattleScroll[i].scroll,
                  fx::kExpectedFinalBattleScroll[i]) << "scroll " << i;
    }
}

// --- throw / slot ------------------------------------------------------------

TEST(ThrowToolsConversions, MatchRomColumns) {
    ASSERT_EQ(ostinato::kThrowToolsConversions.size(),
              fx::kExpectedThrowToolsItem.size());
    for (std::size_t i = 0; i < ostinato::kThrowToolsConversions.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kThrowToolsConversions[i].item),
                  fx::kExpectedThrowToolsItem[i]) << "item " << i;
        EXPECT_EQ(ostinato::kThrowToolsConversions[i].attackOffset,
                  fx::kExpectedThrowToolsOffset[i]) << "offset " << i;
    }
}

TEST(SlotOutcomes, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kSlotOutcomes.size(), fx::kExpectedSlotAttack.size());
    for (std::size_t i = 0; i < ostinato::kSlotOutcomes.size(); ++i) {
        EXPECT_EQ(ostinato::kSlotOutcomes[i].index, i);
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kSlotOutcomes[i].attack),
                  fx::kExpectedSlotAttack[i]) << "outcome " << i;
    }
    // The esper outcome is the NONE sentinel, not a real attack.
    EXPECT_EQ(ostinato::kSlotOutcomes[3].attack, AttackId::NONE);
}

// The joker-doom outcomes target every character or every monster.
TEST(JokerDoomTargets, SelectWholeSides) {
    ASSERT_EQ(ostinato::kJokerDoomTargets.size(), fx::kExpectedJokerTarget.size());
    for (std::size_t i = 0; i < ostinato::kJokerDoomTargets.size(); ++i) {
        EXPECT_EQ(ostinato::kJokerDoomTargets[i].index, i);
        EXPECT_EQ(ostinato::kJokerDoomTargets[i].targets.bits,
                  fx::kExpectedJokerTarget[i]) << "target mask " << i;
    }
    const auto characters = ostinato::kJokerDoomTargets[0].targets;
    for (std::uint8_t slot = 0; slot < 4; ++slot) {
        EXPECT_TRUE(characters.has(slot)) << "character slot " << int{slot};
    }
    EXPECT_FALSE(characters.has(4));

    const auto monsters = ostinato::kJokerDoomTargets[1].targets;
    for (std::uint8_t slot = 0; slot < 6; ++slot) {
        EXPECT_TRUE(monsters.has(slot)) << "monster slot " << int{slot};
    }
    EXPECT_FALSE(monsters.has(6));
}

// --- dance / background ------------------------------------------------------

TEST(DanceBackgrounds, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kDanceBackgrounds.size(), 8u);
    for (std::size_t i = 0; i < ostinato::kDanceBackgrounds.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kDanceBackgrounds[i].dance), i);
        EXPECT_EQ(
            static_cast<std::uint8_t>(ostinato::kDanceBackgrounds[i].background),
            fx::kExpectedDanceBG[i]) << "dance background " << i;
    }
    // The two ROM rows past the named dances are unused padding, not modeled.
    ASSERT_EQ(fx::kExpectedDanceBG.size(), 10u);
    for (std::size_t pad = 8; pad < 10; ++pad) {
        EXPECT_EQ(fx::kExpectedDanceBG[pad],
                  static_cast<std::uint8_t>(BattleBackgroundId::FOREST_WOR));
    }
}

TEST(BackgroundDances, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kBackgroundDances.size(), 56u);
    for (std::size_t i = 0; i < ostinato::kBackgroundDances.size(); ++i) {
        EXPECT_EQ(
            static_cast<std::uint8_t>(ostinato::kBackgroundDances[i].background), i);
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kBackgroundDances[i].dance),
                  fx::kExpectedBattleBGDance[i]) << "background dance " << i;
    }
    // The ROM run is 64 rows; the 8 past the named backgrounds are padding.
    ASSERT_EQ(fx::kExpectedBattleBGDance.size(), 64u);
    for (std::size_t pad = 56; pad < 64; ++pad) {
        EXPECT_EQ(fx::kExpectedBattleBGDance[pad],
                  static_cast<std::uint8_t>(DanceId::DUSK_REQUIUM));
    }
}

// Wind Song switches to the WoB field, and that background pairs back to Wind
// Song — dancing it there keeps the background.
TEST(DanceBackgrounds, PairBackConsistently) {
    EXPECT_EQ(ostinato::danceBackground(DanceId::WIND_SONG),
              BattleBackgroundId::FIELD_WOB);
    EXPECT_EQ(ostinato::backgroundDance(BattleBackgroundId::FIELD_WOB),
              DanceId::WIND_SONG);
    EXPECT_EQ(ostinato::danceBackground(DanceId::SNOWMAN_JAZZ),
              BattleBackgroundId::SNOWFIELDS);
    EXPECT_EQ(ostinato::backgroundDance(BattleBackgroundId::SNOWFIELDS),
              DanceId::SNOWMAN_JAZZ);
}

// --- AI script ---------------------------------------------------------------

TEST(AiCommandSizes, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kAiCommandSizes.size(), fx::kExpectedAICmdSize.size());
    const auto first =
        static_cast<std::uint8_t>(ostinato::kAiCommandSizes.front().command);
    for (std::size_t i = 0; i < ostinato::kAiCommandSizes.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kAiCommandSizes[i].command),
                  first + i);
        EXPECT_EQ(ostinato::kAiCommandSizes[i].size, fx::kExpectedAICmdSize[i])
            << "command size " << i;
    }
    // The three script terminators are a lone byte; a use-attack is four.
    EXPECT_EQ(ostinato::aiCommandSize(AiScriptCommand::END_OF_SCRIPT), 1u);
    EXPECT_EQ(ostinato::aiCommandSize(AiScriptCommand::END_IF), 1u);
    EXPECT_EQ(ostinato::aiCommandSize(AiScriptCommand::USE_ATTACK), 4u);
}

TEST(AiCommandsForAttack, MatchRomColumns) {
    ASSERT_EQ(ostinato::kAiCommandsForAttack.size(),
              fx::kExpectedAttackForAI.size());
    for (std::size_t i = 0; i < ostinato::kAiCommandsForAttack.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kAiCommandsForAttack[i].attack),
                  fx::kExpectedAttackForAI[i]) << "attack " << i;
        EXPECT_EQ(
            static_cast<std::uint8_t>(ostinato::kAiCommandsForAttack[i].command),
            fx::kExpectedCmdForAI[i]) << "command " << i;
    }
    EXPECT_EQ(ostinato::kAiCommandsForAttack.front().command,
              BattleCommandId::SUMMON);
}

// --- item types --------------------------------------------------------------

TEST(ItemTypeBattleFlags, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kItemTypeBattleFlags.size(), 7u);
    for (std::size_t i = 0; i < ostinato::kItemTypeBattleFlags.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kItemTypeBattleFlags[i].type), i);
        EXPECT_EQ(ostinato::kItemTypeBattleFlags[i].flags.bits,
                  fx::kExpectedItemTypeMask[i]) << "item type flags " << i;
    }
    // The eighth ROM row is out-of-enum padding, not modeled.
    ASSERT_EQ(fx::kExpectedItemTypeMask.size(), 8u);
    EXPECT_EQ(fx::kExpectedItemTypeMask[7], 0x00);
}

TEST(ItemTypeBattleFlags, ExposeTheShiftedMerge) {
    const auto tool = ostinato::itemTypeBattleFlags(ItemType::TOOL);
    EXPECT_EQ(tool.bits, 0xA0);
    EXPECT_EQ(tool.mergedBits(), 0x40);
    EXPECT_TRUE(tool.skipsEquippableCheck());
    EXPECT_FALSE(ostinato::itemTypeBattleFlags(ItemType::WEAPON)
                     .skipsEquippableCheck());
    EXPECT_EQ(ostinato::itemTypeBattleFlags(ItemType::CONSUMABLE).bits, 0x00);
}

// --- spell order -------------------------------------------------------------

TEST(MagicOrderOffsets, MatchRomAsSignedBytes) {
    struct Band {
        const std::array<ostinato::MagicOrderOffsetEntry, 6>* table;
        const std::array<std::uint8_t, 6>* expected;
        const char* name;
    };
    const Band bands[] = {
        { &ostinato::kBlackMagicOrder, &fx::kExpectedBlackMagicOrder, "black" },
        { &ostinato::kEffectMagicOrder, &fx::kExpectedEffectMagicOrder, "effect" },
        { &ostinato::kWhiteMagicOrder, &fx::kExpectedWhiteMagicOrder, "white" },
    };
    for (const auto& band : bands) {
        for (std::size_t i = 0; i < band.table->size(); ++i) {
            EXPECT_EQ((*band.table)[i].setting, i);
            const auto raw = static_cast<std::uint8_t>((*band.table)[i].offset);
            EXPECT_EQ(raw, (*band.expected)[i]) << band.name << " offset " << i;
        }
    }
}

// Setting 2 leaves every band where it is; setting 0 pulls white magic to the
// front of the list (its band starts at attack $2D).
TEST(MagicOrderOffsets, DescribeTheOrderings) {
    EXPECT_EQ(ostinato::kBlackMagicOrder[2].offset, 0);
    EXPECT_EQ(ostinato::kEffectMagicOrder[2].offset, 0);
    EXPECT_EQ(ostinato::kWhiteMagicOrder[2].offset, 0);

    EXPECT_EQ(0x2D + ostinato::kWhiteMagicOrder[0].offset, 0);
    EXPECT_EQ(0x00 + ostinato::kBlackMagicOrder[0].offset, 9);
}

// --- desperation attacks -----------------------------------------------------

TEST(DesperationAttacks, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kDesperationAttacks.size(),
              fx::kExpectedDesperationAttack.size());
    for (std::size_t i = 0; i < ostinato::kDesperationAttacks.size(); ++i) {
        EXPECT_EQ(
            static_cast<std::uint8_t>(ostinato::kDesperationAttacks[i].character), i);
        EXPECT_EQ(static_cast<std::uint8_t>(ostinato::kDesperationAttacks[i].attack),
                  fx::kExpectedDesperationAttack[i]) << "desperation " << i;
    }
    // Gau and Umaro have none.
    EXPECT_EQ(ostinato::desperationAttack(ostinato::CharacterId::GAU),
              AttackId::NONE);
    EXPECT_EQ(ostinato::desperationAttack(ostinato::CharacterId::UMARO),
              AttackId::NONE);
    EXPECT_EQ(ostinato::desperationAttack(ostinato::CharacterId::TERRA),
              AttackId::RIOT_BLADE);
}

// --- level progression -------------------------------------------------------

TEST(LevelUpExp, IsByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kLevelUpExp.size(), fx::kExpectedLevelUpExp.size());
    for (std::size_t i = 0; i < ostinato::kLevelUpExp.size(); ++i) {
        EXPECT_EQ(ostinato::kLevelUpExp[i].level, i + 2);
        EXPECT_EQ(ostinato::kLevelUpExp[i].exp, fx::kExpectedLevelUpExp[i])
            << "exp step " << i;
    }
    EXPECT_EQ(ostinato::levelUpExp(2), 4u);
    // The top of the ramp is an upstream outlier against its neighbours.
    EXPECT_EQ(ostinato::levelUpExp(99), 11111u);
    EXPECT_EQ(ostinato::levelUpExp(98), 9603u);
}

TEST(LevelUpHp, IsByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kLevelUpHp.size(), fx::kExpectedLevelUpHP.size());
    for (std::size_t i = 0; i < ostinato::kLevelUpHp.size(); ++i) {
        EXPECT_EQ(ostinato::kLevelUpHp[i].level, i + 2);
        EXPECT_EQ(ostinato::kLevelUpHp[i].gain, fx::kExpectedLevelUpHP[i])
            << "hp gain " << i;
    }
    EXPECT_EQ(ostinato::levelUpHp(2), 11u);
    EXPECT_EQ(ostinato::levelUpHp(99), 88u);
}

TEST(LevelUpMp, BothVersionCurvesAreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kLevelUpMpEn.size(), fx::kExpectedLevelUpMPEn.size());
    ASSERT_EQ(ostinato::kLevelUpMpJp.size(), fx::kExpectedLevelUpMPJp.size());
    for (std::size_t i = 0; i < ostinato::kLevelUpMpEn.size(); ++i) {
        EXPECT_EQ(ostinato::kLevelUpMpEn[i].level, i + 2);
        EXPECT_EQ(ostinato::kLevelUpMpEn[i].gain, fx::kExpectedLevelUpMPEn[i])
            << "en mp gain " << i;
        EXPECT_EQ(ostinato::kLevelUpMpJp[i].level, i + 2);
        EXPECT_EQ(ostinato::kLevelUpMpJp[i].gain, fx::kExpectedLevelUpMPJp[i])
            << "jp mp gain " << i;
    }
}

// The MP curve is the one table that differs by version.
TEST(LevelUpMp, IsSelectedByVersion) {
    EXPECT_EQ(ostinato::levelUpMp(GameVersion::US_1_0, 2), 4u);
    EXPECT_EQ(ostinato::levelUpMp(GameVersion::US_1_1, 2), 4u);
    EXPECT_EQ(ostinato::levelUpMp(GameVersion::JP_1_0, 2), 5u);

    EXPECT_EQ(ostinato::levelUpMp(GameVersion::US_1_0).data(),
              ostinato::kLevelUpMpEn.data());
    EXPECT_EQ(ostinato::levelUpMp(GameVersion::JP_1_0).data(),
              ostinato::kLevelUpMpJp.data());
    // The two curves really are different data — they agree at some levels
    // (both gain 13 at level 99), so the difference is a whole-curve property.
    const auto en = ostinato::levelUpMp(GameVersion::US_1_0);
    const auto jp = ostinato::levelUpMp(GameVersion::JP_1_0);
    ASSERT_EQ(en.size(), jp.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < en.size(); ++i) {
        if (en[i].gain != jp[i].gain) {
            ++differing;
        }
    }
    EXPECT_GT(differing, 0u) << "the version curves must not be identical";
}

// --- abilities ---------------------------------------------------------------

TEST(AbilityLearnLevels, AreByteIdenticalToRom) {
    ASSERT_EQ(ostinato::kBushidoLearnLevels.size(),
              fx::kExpectedBushidoLevel.size());
    for (std::size_t i = 0; i < ostinato::kBushidoLearnLevels.size(); ++i) {
        EXPECT_EQ(ostinato::kBushidoLearnLevels[i].level,
                  fx::kExpectedBushidoLevel[i]) << "swdtech level " << i;
        EXPECT_EQ(ostinato::kBlitzLearnLevels[i].level,
                  fx::kExpectedBlitzLevel[i]) << "blitz level " << i;
    }
    // The abilities are named, and they are consecutive from each command's base.
    EXPECT_EQ(ostinato::kBushidoLearnLevels.front().ability, AttackId::DISPATCH);
    EXPECT_EQ(ostinato::kBushidoLearnLevels.back().ability, AttackId::CLEAVE);
    EXPECT_EQ(ostinato::kBlitzLearnLevels.front().ability, AttackId::PUMMEL);
    EXPECT_EQ(ostinato::kBlitzLearnLevels.back().ability, AttackId::BUM_RUSH);
}

TEST(AbilityLearnLevels, AreReachedInOrder) {
    for (std::size_t i = 1; i < ostinato::kBushidoLearnLevels.size(); ++i) {
        EXPECT_LT(ostinato::kBushidoLearnLevels[i - 1].level,
                  ostinato::kBushidoLearnLevels[i].level);
        EXPECT_LT(ostinato::kBlitzLearnLevels[i - 1].level,
                  ostinato::kBlitzLearnLevels[i].level);
    }
    EXPECT_EQ(ostinato::abilityLearnLevels(BattleCommandId::BUSHIDO).data(),
              ostinato::kBushidoLearnLevels.data());
    EXPECT_EQ(ostinato::abilityLearnLevels(BattleCommandId::BLITZ).data(),
              ostinato::kBlitzLearnLevels.data());
}

TEST(LearnedAbilityFlags, AreTheCumulativeRamp) {
    ASSERT_EQ(ostinato::kLearnedAbilityFlags.size(),
              fx::kExpectedLearnAbility.size());
    for (std::size_t i = 0; i < ostinato::kLearnedAbilityFlags.size(); ++i) {
        EXPECT_EQ(ostinato::kLearnedAbilityFlags[i].learnedCount, i);
        EXPECT_EQ(ostinato::kLearnedAbilityFlags[i].abilities.bits,
                  fx::kExpectedLearnAbility[i]) << "learned flags " << i;
        EXPECT_EQ(ostinato::kLearnedAbilityFlags[i].abilities.count(),
                  static_cast<int>(i));
    }
    const auto three = ostinato::kLearnedAbilityFlags[3].abilities;
    EXPECT_TRUE(three.has(0));
    EXPECT_TRUE(three.has(2));
    EXPECT_FALSE(three.has(3));
}

TEST(CharacterLevelModifier, DecodesEachSetting) {
    ASSERT_EQ(ostinato::kCharacterLevelModifiers.size(),
              fx::kExpectedCharLevelMod.size());
    for (std::size_t i = 0; i < ostinato::kCharacterLevelModifiers.size(); ++i) {
        const auto raw =
            static_cast<std::uint8_t>(ostinato::kCharacterLevelModifiers[i].levels);
        EXPECT_EQ(raw, fx::kExpectedCharLevelMod[i]) << "level modifier " << i;
    }
    EXPECT_EQ(ostinato::characterLevelModifier(LevelMod::NORMAL), 0);
    EXPECT_EQ(ostinato::characterLevelModifier(LevelMod::HIGH), 2);
    EXPECT_EQ(ostinato::characterLevelModifier(LevelMod::VERY_HIGH), 5);
    EXPECT_EQ(ostinato::characterLevelModifier(LevelMod::LOW), -3);
}

}  // namespace
