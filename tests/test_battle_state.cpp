// The battle core's types: battle slots and sets of them, the ATB gauge, the battle's random
// numbers and the player's battle settings. The expected values come from docs/contracts/
// battle_core.md, which cites the line of the original each one is read from.

#include <gtest/gtest.h>

#include "battle/battle_rng.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/rng_table.h"

namespace ostinato::test {
namespace {

using battle::AtbGauge;
using battle::BattleConfig;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleRng;

TEST(BattlerSlot, PartyComesFirstThenMonsters) {
    EXPECT_EQ(BattlerSlot::character(0).index(), 0);
    EXPECT_EQ(BattlerSlot::character(3).index(), 3);
    EXPECT_EQ(BattlerSlot::monster(0).index(), 4);
    EXPECT_EQ(BattlerSlot::monster(5).index(), 9);
    EXPECT_TRUE(BattlerSlot::character(3).isCharacter());
    EXPECT_TRUE(BattlerSlot::monster(0).isMonster());
    EXPECT_EQ(BattlerSlot::monster(5).position(), 5);
    EXPECT_EQ(BattlerSlot::fromIndex(6), BattlerSlot::monster(2));
}

TEST(BattlerSet, IsTheTwoBytesOfATargetList) {
    // The party's byte holds a bit per position and the monsters' byte likewise ($b8/$b9).
    BattlerSet set = BattlerSet::of(BattlerSlot::character(1));
    set.add(BattlerSlot::monster(2));
    EXPECT_EQ(set.characters.bits, 0x02);
    EXPECT_EQ(set.monsters.bits, 0x04);
    EXPECT_EQ(set.count(), 2);
    EXPECT_TRUE(set.has(BattlerSlot::monster(2)));
    EXPECT_FALSE(set.has(BattlerSlot::monster(1)));
    set.remove(BattlerSlot::character(1));
    EXPECT_EQ(set, BattlerSet::of(BattlerSlot::monster(2)));
    EXPECT_EQ(BattlerSet::allCharacters().characters.bits, 0x0F);
    EXPECT_EQ(BattlerSet::allMonsters().monsters.bits, 0x3F);
    EXPECT_EQ(BattlerSet::allCharacters().count() + BattlerSet::allMonsters().count(), 10);
}

TEST(BattlerSet, CombinesAndSubtracts) {
    const BattlerSet a = BattlerSet::of(BattlerSlot::character(0)) | BattlerSet::of(BattlerSlot::monster(0));
    const BattlerSet b = BattlerSet::of(BattlerSlot::monster(0)) | BattlerSet::of(BattlerSlot::monster(1));
    EXPECT_EQ(a & b, BattlerSet::of(BattlerSlot::monster(0)));
    EXPECT_EQ(a.without(b), BattlerSet::of(BattlerSlot::character(0)));
    EXPECT_EQ((a | b).count(), 3);
    EXPECT_EQ(a.charactersOnly(), BattlerSet::of(BattlerSlot::character(0)));
    EXPECT_EQ(a.monstersOnly(), BattlerSet::of(BattlerSlot::monster(0)));
    EXPECT_TRUE(BattlerSet{}.empty());
}

TEST(AtbGauge, FillingPastTheTopReadsFullAndKeepsThePartStep) {
    // actioncounter, battle_main.asm:2758-2767: a 16-bit add; on the carry the high byte is
    // zeroed and the low byte stays.
    AtbGauge gauge{.counter = 0xFF80};
    EXPECT_FALSE(gauge.isFull());
    EXPECT_TRUE(gauge.fill(0x0100));
    EXPECT_TRUE(gauge.isFull());
    EXPECT_EQ(gauge.counter, 0x0080);

    AtbGauge rising{.counter = 0x0100};
    EXPECT_FALSE(rising.fill(360));
    EXPECT_EQ(rising.counter, 0x0100 + 360);
}

TEST(AtbGauge, TheMeterReadsOneBelowTheHighByte) {
    // UpdateCounterGfxBuf, :15153-15155: the gauge shown is the high byte less one.
    EXPECT_EQ((AtbGauge{.counter = 0x0000}).fillLevel(), 255);
    EXPECT_EQ((AtbGauge{.counter = 0x0100}).fillLevel(), 0);
    EXPECT_EQ((AtbGauge{.counter = 0x8000}).fillLevel(), 127);
}

TEST(AtbGauge, EmptyNearlyFullAndAfterATurn) {
    AtbGauge gauge{.counter = 0x0042};
    gauge.empty();                  // DisableATB, :1475-1476
    EXPECT_EQ(gauge.counter, 0x0142);
    gauge.setNearlyFull();          // RemoveAllActions, :1592-1594
    EXPECT_EQ(gauge.counter, 0xFF42);
    gauge.stepBackAfterTurn();      // ExecAction, :280-282: one step from full stays
    EXPECT_EQ(gauge.counter, 0xFF42);
    AtbGauge full{.counter = 0x0042};
    full.stepBackAfterTurn();       // a full gauge starts again from empty
    EXPECT_EQ(full.counter, 0x0142);
}

TEST(BattleRng, ReadsTheNextTableEntry) {
    // Rand, :12378-12384: the position moves on, then the entry is read.
    BattleRng rng(41);
    EXPECT_EQ(rng.next(), rngByte(42));
    EXPECT_EQ(rng.position(), 42);
    rng.advance();
    EXPECT_EQ(rng.position(), 43);
}

TEST(BattleRng, BelowScalesTheEntry) {
    // RandA, :12390-12404: the entry times n, high byte.
    BattleRng rng(7);
    const std::uint8_t entry = rngByte(8);
    EXPECT_EQ(rng.below(20), static_cast<std::uint8_t>((entry * 20) >> 8));
    BattleRng zero(7);
    EXPECT_EQ(zero.below(0), 0);
    EXPECT_EQ(zero.position(), 8);
}

TEST(BattleRng, CoinIsTheLowBit) {
    BattleRng rng(99);
    EXPECT_EQ(rng.coin(), (rngByte(100) & 1) != 0);
}

TEST(BattleRng, PickFromCountsThePartyFirst) {
    // RandBit, :13495-13515: count the set bits, pick k with RandA, walk up from the lowest bit.
    const BattlerSet set = BattlerSet::of(BattlerSlot::character(2)) | BattlerSet::of(BattlerSlot::monster(0)) |
                           BattlerSet::of(BattlerSlot::monster(4));
    for (int start = 0; start < 256; ++start) {
        BattleRng rng(static_cast<std::uint8_t>(start));
        const int k = (rngByte(static_cast<std::uint8_t>(start + 1)) * 3) >> 8;
        const BattlerSlot expected[] = {BattlerSlot::character(2), BattlerSlot::monster(0), BattlerSlot::monster(4)};
        ASSERT_EQ(rng.pickFrom(set), expected[k]) << "start " << start;
    }
}

TEST(BattleRng, PickFromAnEmptySetDrawsNothing) {
    BattleRng rng(10);
    EXPECT_FALSE(rng.pickFrom(BattlerSet{}).has_value());
    EXPECT_EQ(rng.position(), 10);
    BattleRng one(10);
    EXPECT_EQ(one.pickFrom(BattlerSet::of(BattlerSlot::monster(3))), BattlerSlot::monster(3));
    EXPECT_EQ(one.position(), 11);
}

TEST(BattleConfig, DefaultsAreTheNewGameSettings) {
    // menu_sram.asm:103-104 writes %00101010: wait mode, battle speed setting 2; $1d4e is 0, so
    // the gauge shows.
    const BattleConfig config;
    EXPECT_TRUE(config.waitMode);
    EXPECT_EQ(config.speedSetting, 2);
    EXPECT_TRUE(config.gaugeShown);
}

TEST(BattleConfig, MonsterSpeedFactorIs255LessTwentyFourPerStep) {
    // InitBattle, :6114-6120: (setting * 24) inverted.
    for (std::uint8_t setting = 0; setting < 6; ++setting) {
        const BattleConfig config{.speedSetting = setting};
        EXPECT_EQ(config.monsterSpeedFactor(), 255 - 24 * setting);
    }
}

}  // namespace
}  // namespace ostinato::test
