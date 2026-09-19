// Tests for the game's counting limits. Every limit is compared against the
// generated fixture, and the properties the original's own checks depend on —
// each fits the three bytes those checks read, and the gil and step limits are
// the same number — are pinned.
#include <cstdint>

#include <gtest/gtest.h>

#include "ostinato/game_limits.h"

#include "fixtures/game_limits_expected.h"

namespace {

using namespace ostinato;

TEST(GameLimits, EveryLimitMatchesTheContract) {
#define OSTINATO_CHECK_LIMIT(name, expected) EXPECT_EQ(name, std::uint32_t{expected});
    OSTINATO_LIMIT_EXPECTED(OSTINATO_CHECK_LIMIT)
#undef OSTINATO_CHECK_LIMIT
}

// The original compares and clamps each limit a byte at a time over three bytes
// (player.asm:699-707, event.asm:3191, battle_main.asm:16079), so none of them
// may need a fourth.
TEST(GameLimits, EveryLimitFitsThreeBytes) {
#define OSTINATO_CHECK_WIDTH(name, expected) EXPECT_LE(name, 0xFFFFFFu);
    OSTINATO_LIMIT_EXPECTED(OSTINATO_CHECK_WIDTH)
#undef OSTINATO_CHECK_WIDTH
}

TEST(GameLimits, TracedValues) {
    EXPECT_EQ(kMaxGil, 9999999u);
    EXPECT_EQ(kMaxSteps, 9999999u);
    EXPECT_EQ(kMaxExperience, 15000000u);
    // Gil and steps stop at the same seven-nines figure; experience does not.
    EXPECT_EQ(kMaxGil, kMaxSteps);
    EXPECT_GT(kMaxExperience, kMaxGil);
}

}  // namespace
