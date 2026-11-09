// A character's level-up and the esper's level-up bonus (docs/contracts/battle_espers.md, "The
// level-up" and "The esper's bonus"): the level goes up before the gains are read; each of the
// seventeen bonuses — the percentages as 256ths with a least amount of 1, "+100%" with none, LV_30
// and LV_50 doing nothing, the stat bonuses +1 for the odd ones and +2 for the even ones within the
// stat's byte and up to 128;
// the HP and MP caps; and every esper's bonus over the whole table. Every expected value is worked
// out by hand from the curves (src/data/generated/level_up_data.inc).

#include <gtest/gtest.h>

#include <cstdint>

#include "battle/leveling.h"
#include "data/character.h"
#include "data/esper_properties.h"
#include "data/level_up.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_bonus.h"
#include "ostinato/esper_id.h"
#include "ostinato/game_version.h"

namespace ostinato::test {
namespace {

using battle::applyEsperBonus;
using battle::levelUp;
using battle::LevelUpGains;
using game::CharacterRecord;

// A record at `level` with 1,000 HP, 100 MP and every stat at 50.
CharacterRecord recordAt(std::uint8_t level) {
    CharacterRecord record;
    record.level = level;
    record.maxHp = 1000;
    record.maxMp = 100;
    record.vigor = 50;
    record.speed = 50;
    record.stamina = 50;
    record.magicPower = 50;
    return record;
}

// The gains on reaching level 70: 162 HP and, on the US cartridges' curve, 8 MP.
constexpr LevelUpGains kLevel70Gains{.hp = 162, .mp = 8};

LevelUpGains bonused(EsperBonus bonus, LevelUpGains gains = kLevel70Gains) {
    CharacterRecord record = recordAt(69);
    applyEsperBonus(bonus, gains, record);
    return gains;
}

// --- the level-up --------------------------------------------------------------------------------

TEST(Leveling, TheLevelGoesUpAndItsGainsAreTheNewLevels) {
    // Level 9 gains 26 HP and level 10 gains 27, so a level-up from 9 reads level 10's.
    CharacterRecord record = recordAt(9);
    levelUp(record, GameVersion::US_1_1);
    EXPECT_EQ(record.level, 10);
    EXPECT_EQ(record.maxHp, 1027);
    EXPECT_EQ(record.maxMp, 108);
}

TEST(Leveling, TheMpGainedFollowsTheVersionsCurve) {
    // Level 10 gains 8 MP on the US cartridges and 13 on the Japanese one.
    CharacterRecord us = recordAt(9);
    CharacterRecord jp = recordAt(9);
    levelUp(us, GameVersion::US_1_0);
    levelUp(jp, GameVersion::JP_1_0);
    EXPECT_EQ(us.maxMp, 108);
    EXPECT_EQ(jp.maxMp, 113);
    EXPECT_EQ(us.maxHp, jp.maxHp);
}

TEST(Leveling, WithNoEsperTheGainsAreTheCurves) {
    CharacterRecord record = recordAt(69);
    levelUp(record, GameVersion::US_1_1);
    EXPECT_EQ(record.maxHp, 1162);
    EXPECT_EQ(record.maxMp, 108);
    EXPECT_EQ(record.vigor, 50);
}

TEST(Leveling, AnEsperWithNoBonusAddsNothing) {
    CharacterRecord record = recordAt(69);
    record.esper = EsperId::SHIVA;
    levelUp(record, GameVersion::US_1_1);
    EXPECT_EQ(record.maxHp, 1162);
    EXPECT_EQ(record.maxMp, 108);
}

TEST(Leveling, TheEquippedEspersBonusIsApplied) {
    // Bahamut: +50% HP, 162 + 81.
    CharacterRecord record = recordAt(69);
    record.esper = EsperId::BAHAMUT;
    levelUp(record, GameVersion::US_1_1);
    EXPECT_EQ(record.maxHp, 1243);
    EXPECT_EQ(record.maxMp, 108);
}

TEST(Leveling, MaximumHpStopsAt9999) {
    CharacterRecord record = recordAt(69);
    record.maxHp = 9900;
    levelUp(record, GameVersion::US_1_1);
    EXPECT_EQ(record.maxHp, 9999);
    record.maxHp = 9999;
    record.level = 69;
    record.esper = EsperId::BAHAMUT;
    levelUp(record, GameVersion::US_1_1);
    EXPECT_EQ(record.maxHp, 9999);
}

TEST(Leveling, MaximumMpStopsAt999) {
    CharacterRecord record = recordAt(69);
    record.maxMp = 995;
    levelUp(record, GameVersion::US_1_1);
    EXPECT_EQ(record.maxMp, 999);
}

TEST(Leveling, ALevel98CharacterReaches99) {
    // Level 99 gains 88 HP and 13 MP.
    CharacterRecord record = recordAt(98);
    levelUp(record, GameVersion::US_1_1);
    EXPECT_EQ(record.level, kMaxLevel);
    EXPECT_EQ(record.maxHp, 1088);
    EXPECT_EQ(record.maxMp, 113);
}

// --- the esper's bonus ---------------------------------------------------------------------------

// The percentages are 256ths: 162 × 26 / 256 = 16, 162 × 78 / 256 = 49 (where 30 % would be 48.6),
// 162 × 128 / 256 = 81.
TEST(Leveling, TheHpPercentagesAreTwoHundredFiftySixths) {
    EXPECT_EQ(bonused(EsperBonus::HP_10).hp, 178);
    EXPECT_EQ(bonused(EsperBonus::HP_30).hp, 211);
    EXPECT_EQ(bonused(EsperBonus::HP_50).hp, 243);
    for (const EsperBonus bonus : {EsperBonus::HP_10, EsperBonus::HP_30, EsperBonus::HP_50}) {
        EXPECT_EQ(bonused(bonus).mp, 8);
    }
}

// 8 × 26 / 256 = 0, raised to 1; 8 × 78 / 256 = 2; 8 × 128 / 256 = 4.
TEST(Leveling, TheMpPercentagesAreTwoHundredFiftySixths) {
    EXPECT_EQ(bonused(EsperBonus::MP_10).mp, 9);
    EXPECT_EQ(bonused(EsperBonus::MP_30).mp, 10);
    EXPECT_EQ(bonused(EsperBonus::MP_50).mp, 12);
    for (const EsperBonus bonus : {EsperBonus::MP_10, EsperBonus::MP_30, EsperBonus::MP_50}) {
        EXPECT_EQ(bonused(bonus).hp, 162);
    }
}

TEST(Leveling, APercentageBonusAddsAtLeastOne) {
    EXPECT_EQ(bonused(EsperBonus::HP_10, {.hp = 9, .mp = 4}).hp, 10);
    EXPECT_EQ(bonused(EsperBonus::HP_30, {.hp = 3, .mp = 4}).hp, 4);
    EXPECT_EQ(bonused(EsperBonus::HP_50, {.hp = 1, .mp = 4}).hp, 2);
    EXPECT_EQ(bonused(EsperBonus::MP_30, {.hp = 11, .mp = 3}).mp, 4);
}

TEST(Leveling, PlusOneHundredPercentDoublesTheHpGain) {
    const LevelUpGains gains = bonused(EsperBonus::HP_100);
    EXPECT_EQ(gains.hp, 324);
    EXPECT_EQ(gains.mp, 8);
}

TEST(Leveling, Lv30Lv50AndNoneChangeNothing) {
    for (const EsperBonus bonus : {EsperBonus::LV_30, EsperBonus::LV_50, EsperBonus::NONE}) {
        CharacterRecord record = recordAt(69);
        LevelUpGains gains = kLevel70Gains;
        applyEsperBonus(bonus, gains, record);
        EXPECT_EQ(gains.hp, 162);
        EXPECT_EQ(gains.mp, 8);
        EXPECT_EQ(record.vigor, 50);
        EXPECT_EQ(record.speed, 50);
        EXPECT_EQ(record.stamina, 50);
        EXPECT_EQ(record.magicPower, 50);
    }
}

struct StatBonusCase {
    EsperBonus bonus;
    int vigor;
    int speed;
    int stamina;
    int magicPower;
};

TEST(Leveling, EachStatBonusRaisesItsOwnStatByOneOrTwo) {
    for (const StatBonusCase& c : {
             StatBonusCase{EsperBonus::STRENGTH_1, 51, 50, 50, 50},
             StatBonusCase{EsperBonus::STRENGTH_2, 52, 50, 50, 50},
             StatBonusCase{EsperBonus::SPEED_1, 50, 51, 50, 50},
             StatBonusCase{EsperBonus::SPEED_2, 50, 52, 50, 50},
             StatBonusCase{EsperBonus::STAMINA_1, 50, 50, 51, 50},
             StatBonusCase{EsperBonus::STAMINA_2, 50, 50, 52, 50},
             StatBonusCase{EsperBonus::MAGPWR_1, 50, 50, 50, 51},
             StatBonusCase{EsperBonus::MAGPWR_2, 50, 50, 50, 52},
         }) {
        CharacterRecord record = recordAt(69);
        LevelUpGains gains = kLevel70Gains;
        applyEsperBonus(c.bonus, gains, record);
        EXPECT_EQ(record.vigor, c.vigor) << static_cast<int>(c.bonus);
        EXPECT_EQ(record.speed, c.speed) << static_cast<int>(c.bonus);
        EXPECT_EQ(record.stamina, c.stamina) << static_cast<int>(c.bonus);
        EXPECT_EQ(record.magicPower, c.magicPower) << static_cast<int>(c.bonus);
        EXPECT_EQ(gains.hp, 162);
        EXPECT_EQ(gains.mp, 8);
    }
}

TEST(Leveling, AStatBonusStopsAt128) {
    const auto raised = [](EsperBonus bonus, std::uint8_t vigor) {
        CharacterRecord record = recordAt(69);
        record.vigor = vigor;
        LevelUpGains gains = kLevel70Gains;
        applyEsperBonus(bonus, gains, record);
        return static_cast<int>(record.vigor);
    };
    EXPECT_EQ(raised(EsperBonus::STRENGTH_2, 126), 128);
    EXPECT_EQ(raised(EsperBonus::STRENGTH_2, 127), 128);
    EXPECT_EQ(raised(EsperBonus::STRENGTH_1, 127), 128);
    EXPECT_EQ(raised(EsperBonus::STRENGTH_1, 128), 128);
    EXPECT_EQ(raised(EsperBonus::STRENGTH_2, 128), 128);
}

// The bonus adds within the stat's byte before the limit: 255 + 1 and 254 + 2 come round to 0 and
// 255 + 2 to 1, each kept; 254 + 1 and 253 + 2 are 255, held to 128.
TEST(Leveling, AStatBonusAddsWithinTheStatsByte) {
    const auto raised = [](EsperBonus bonus, std::uint8_t vigor) {
        CharacterRecord record = recordAt(69);
        record.vigor = vigor;
        LevelUpGains gains = kLevel70Gains;
        applyEsperBonus(bonus, gains, record);
        return static_cast<int>(record.vigor);
    };
    EXPECT_EQ(raised(EsperBonus::STRENGTH_1, 255), 0);
    EXPECT_EQ(raised(EsperBonus::STRENGTH_2, 254), 0);
    EXPECT_EQ(raised(EsperBonus::STRENGTH_2, 255), 1);
    EXPECT_EQ(raised(EsperBonus::STRENGTH_1, 254), 128);
    EXPECT_EQ(raised(EsperBonus::STRENGTH_2, 253), 128);
}

// A record's stats are written in two places: the bonus, which never stores more than 128, and the
// event command that sets a record from one of the 64 character property records. With no
// property record above 128, the wrap is never met in play.
TEST(Leveling, NoCharacterPropertyRecordHoldsAStatAbove128) {
    const auto records = characterBaseStats();
    ASSERT_EQ(records.size(), 64u);
    for (const CharacterBaseStatsEntry& entry : records) {
        const int id = static_cast<int>(entry.id);
        EXPECT_LE(entry.record.strength, 128) << id;
        EXPECT_LE(entry.record.agility, 128) << id;
        EXPECT_LE(entry.record.stamina, 128) << id;
        EXPECT_LE(entry.record.magicPower, 128) << id;
    }
}

// Every esper equipped for a level-up from 69: its bonus lands on the gains or a stat as its row says.
TEST(Leveling, EveryEspersBonusLandsWhereItsRowSays) {
    for (const EsperPropertiesEntry& entry : kEsperProperties) {
        CharacterRecord record = recordAt(69);
        record.esper = entry.id;
        levelUp(record, GameVersion::US_1_1);
        int hp = 1162;
        int mp = 108;
        int vigor = 50;
        int speed = 50;
        int stamina = 50;
        int magicPower = 50;
        switch (entry.record.bonus) {
            case EsperBonus::HP_10: hp = 1178; break;
            case EsperBonus::HP_30: hp = 1211; break;
            case EsperBonus::HP_50: hp = 1243; break;
            case EsperBonus::MP_10: mp = 109; break;
            case EsperBonus::MP_30: mp = 110; break;
            case EsperBonus::MP_50: mp = 112; break;
            case EsperBonus::HP_100: hp = 1324; break;
            case EsperBonus::STRENGTH_1: vigor = 51; break;
            case EsperBonus::STRENGTH_2: vigor = 52; break;
            case EsperBonus::SPEED_1: speed = 51; break;
            case EsperBonus::SPEED_2: speed = 52; break;
            case EsperBonus::STAMINA_1: stamina = 51; break;
            case EsperBonus::STAMINA_2: stamina = 52; break;
            case EsperBonus::MAGPWR_1: magicPower = 51; break;
            case EsperBonus::MAGPWR_2: magicPower = 52; break;
            case EsperBonus::LV_30:
            case EsperBonus::LV_50:
            case EsperBonus::NONE: break;
        }
        const int id = static_cast<int>(entry.id);
        EXPECT_EQ(record.level, 70) << id;
        EXPECT_EQ(record.maxHp, hp) << id;
        EXPECT_EQ(record.maxMp, mp) << id;
        EXPECT_EQ(record.vigor, vigor) << id;
        EXPECT_EQ(record.speed, speed) << id;
        EXPECT_EQ(record.stamina, stamina) << id;
        EXPECT_EQ(record.magicPower, magicPower) << id;
    }
}

}  // namespace
}  // namespace ostinato::test
