// Full-corpus test of the per-battle magic-points table (PLAN phase-1.B D3).
// Asserts all 512 entries match the ROM (no subset) — identity and value —
// and that the accessor reads by battle index. The original's >= 512 guard is
// consumer reward logic (a later battle phase), deliberately absent here.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "data/battle_magic_points.h"

#include "fixtures/battle_magic_points_expected.h"

namespace {

TEST(BattleMagicPoints, AllEntriesMatchRom) {
    ASSERT_EQ(ostinato::kBattleMagicPoints.size(),
              ostinato::test::kBattleMagicPointsExpected.size());
    for (std::size_t i = 0; i < ostinato::kBattleMagicPoints.size(); ++i) {
        EXPECT_EQ(ostinato::kBattleMagicPoints[i].battleIndex,
                  ostinato::test::kBattleMagicPointsExpected[i].battleIndex)
            << "battle index " << i;
        EXPECT_EQ(ostinato::kBattleMagicPoints[i].magicPoints,
                  ostinato::test::kBattleMagicPointsExpected[i].magicPoints)
            << "battle index " << i;
    }
}

TEST(BattleMagicPoints, AccessorReadsByBattleIndex) {
    EXPECT_EQ(ostinato::magicPointsForBattle(0),
              ostinato::test::kBattleMagicPointsExpected[0].magicPoints);
    EXPECT_EQ(ostinato::magicPointsForBattle(256),
              ostinato::test::kBattleMagicPointsExpected[256].magicPoints);
    EXPECT_EQ(ostinato::magicPointsForBattle(511),
              ostinato::test::kBattleMagicPointsExpected[511].magicPoints);
}

}  // namespace
