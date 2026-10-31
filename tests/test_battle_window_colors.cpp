// The colors a battle's windows draw in, read back from battleWindowColors: each channel of the
// field's window colors raised by eight 31sts and stopped at full strength, checked by hand and
// over every wallpaper palette the cartridge holds.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "retropp/palette.h"

#include "ostinato/wallpaper_id.h"
#include "text/window_art.h"
#include "vanilla_rom.h"

namespace ostinato::text {
namespace {

// A five-bit channel as the window colors carry it, in eight bits.
std::uint8_t expanded(unsigned fiveBit) {
    return static_cast<std::uint8_t>((fiveBit * 255 + 15) / 31);
}

retropp::Rgba8 colorOf(unsigned r, unsigned g, unsigned b) {
    return {.r = expanded(r), .g = expanded(g), .b = expanded(b)};
}

TEST(BattleWindowColors, EachChannelRisesByEightAndStopsAtFullStrength) {
    WindowColors field{};
    field[0] = colorOf(0, 0, 0);
    field[1] = colorOf(10, 20, 23);
    field[2] = colorOf(24, 30, 31);
    field[3] = colorOf(31, 1, 16);
    const WindowColors battle = battleWindowColors(field);
    EXPECT_EQ(battle[0], colorOf(8, 8, 8));
    EXPECT_EQ(battle[1], colorOf(18, 28, 31));
    EXPECT_EQ(battle[2], colorOf(31, 31, 31));
    EXPECT_EQ(battle[3], colorOf(31, 9, 24));
}

class BattleWindowColorsCartridgeTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = test::ingestVanilla(); }

    static const WindowArt& art() { return cartridge_.content->windowArt; }
};

// Every wallpaper's battle colors, worked out from the palette's own color words.
TEST_F(BattleWindowColorsCartridgeTest, EveryWallpaperIsItsPaletteRaisedByEight) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    for (std::size_t w = 0; w < kWallpaperCount; ++w) {
        const auto wallpaper = static_cast<WallpaperId>(w);
        const WindowColors battle = battleWindowColors(defaultWindowColors(art(), wallpaper));
        for (std::size_t i = 0; i < kWindowColorCount; ++i) {
            const std::size_t at = w * kWallpaperPaletteBytes + (i + 1) * kColorBytes;
            const unsigned word = static_cast<unsigned>(art().palettes[at]) |
                                  static_cast<unsigned>(art().palettes[at + 1]) << 8;
            const auto raised = [](unsigned channel) { return channel + 8 < 31 ? channel + 8 : 31; };
            EXPECT_EQ(battle[i], colorOf(raised(word & 0x1F), raised((word >> 5) & 0x1F),
                                         raised((word >> 10) & 0x1F)))
                << "wallpaper " << w << " color " << i + 1;
        }
    }
}

}  // namespace
}  // namespace ostinato::text
