// The dialog box's artwork and colors, read from the cartridge, checked against
// docs/contracts/dialog_window.md § The picture.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <set>

#include "ostinato/wallpaper_id.h"
#include "ostinato/window_tile.h"
#include "text/window_art.h"
#include "vanilla_rom.h"

namespace ostinato::text {
namespace {

class WindowArtTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = test::ingestVanilla(); }

    static const WindowArt& art() { return cartridge_.content->windowArt; }
};

constexpr std::array<WallpaperId, kWallpaperCount> kAllWallpapers = {
    WallpaperId::WALLPAPER_0, WallpaperId::WALLPAPER_1, WallpaperId::WALLPAPER_2,
    WallpaperId::WALLPAPER_3, WallpaperId::WALLPAPER_4, WallpaperId::WALLPAPER_5,
    WallpaperId::WALLPAPER_6, WallpaperId::WALLPAPER_7,
};

TEST_F(WindowArtTest, TheCartridgeHoldsEightWallpapersAndEightPalettes) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    EXPECT_EQ(art().tiles.size(), kWallpaperCount * kWallpaperBytes);
    EXPECT_EQ(art().palettes.size(), kWallpaperCount * kWallpaperPaletteBytes);
}

TEST_F(WindowArtTest, TheSheetIsOneRowOfTwentyEightTiles) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const IndexedImage sheet = buildWindowSheet(art(), WallpaperId::WALLPAPER_0);
    EXPECT_EQ(sheet.width, static_cast<int>(kWindowTileCount) * kWindowTilePx);
    EXPECT_EQ(sheet.height, kWindowTilePx);
    EXPECT_EQ(sheet.pixels.size(),
              static_cast<std::size_t>(sheet.width) * static_cast<std::size_t>(sheet.height));
}

// Every pixel of every wallpaper is one of the seven colors: 1 to 7, never 0.
TEST_F(WindowArtTest, EveryWallpaperUsesValuesOneToSevenOnly) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    for (const WallpaperId wallpaper : kAllWallpapers) {
        const IndexedImage sheet = buildWindowSheet(art(), wallpaper);
        std::set<int> seen;
        for (const std::uint8_t pixel : sheet.pixels) {
            seen.insert(pixel);
        }
        EXPECT_EQ(seen.count(0), 0u) << "wallpaper " << static_cast<int>(wallpaper);
        EXPECT_LE(*seen.rbegin(), static_cast<int>(kWindowColorCount))
            << "wallpaper " << static_cast<int>(wallpaper);
    }
}

// A pixel's value is its first three bit planes, leftmost pixel in each byte's top bit.
TEST_F(WindowArtTest, APixelIsItsFirstThreePlanes) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const IndexedImage sheet = buildWindowSheet(art(), WallpaperId::WALLPAPER_3);
    const std::size_t tile = static_cast<std::size_t>(WindowTile::CORNER_TOP_LEFT);
    const std::size_t base = 3 * kWallpaperBytes + tile * kWindowTileBytes;
    for (int y = 0; y < kWindowTilePx; ++y) {
        for (int x = 0; x < kWindowTilePx; ++x) {
            const unsigned bit = 7u - static_cast<unsigned>(x);
            const auto row = static_cast<std::size_t>(y) * 2;
            const unsigned want = ((art().tiles[base + row] >> bit) & 1u) |
                                  ((art().tiles[base + row + 1] >> bit) & 1u) << 1 |
                                  ((art().tiles[base + 16 + row] >> bit) & 1u) << 2;
            EXPECT_EQ(sheet.at(static_cast<int>(tile) * kWindowTilePx + x, y), want)
                << "(" << x << ", " << y << ")";
        }
    }
}

// The default colors are colors 1 to 7 of the wallpaper's palette, each 5-bit channel spread over
// 0-255.
TEST_F(WindowArtTest, DefaultColorsAreColorsOneToSevenOfThePalette) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    for (const WallpaperId wallpaper : kAllWallpapers) {
        const WindowColors colors = defaultWindowColors(art(), wallpaper);
        const std::size_t base = static_cast<std::size_t>(wallpaper) * kWallpaperPaletteBytes;
        for (std::size_t i = 0; i < kWindowColorCount; ++i) {
            const std::size_t at = base + (i + 1) * kColorBytes;
            const unsigned word = art().palettes[at] | art().palettes[at + 1] << 8;
            const auto spread = [](unsigned c) {
                return static_cast<std::uint8_t>((c * 255 + 15) / 31);
            };
            EXPECT_EQ(colors[i].r, spread(word & 0x1F)) << i;
            EXPECT_EQ(colors[i].g, spread((word >> 5) & 0x1F)) << i;
            EXPECT_EQ(colors[i].b, spread((word >> 10) & 0x1F)) << i;
            EXPECT_EQ(colors[i].a, 255);
        }
    }
}

// Full and empty channels reach exactly 255 and 0.
TEST(WindowArt, ChannelsSpreadToTheFullRange) {
    const std::array<std::uint8_t, kWallpaperPaletteBytes> palette = [] {
        std::array<std::uint8_t, kWallpaperPaletteBytes> bytes{};
        bytes[2] = 0x1F;  // color 1: red full
        bytes[4] = 0xE0;  // color 2: green full (low three bits)
        bytes[5] = 0x03;  //          (high two bits)
        bytes[7] = 0x7C;  // color 3: blue full
        return bytes;
    }();
    const WindowArt art{.palettes = palette};
    const WindowColors colors = defaultWindowColors(art, WallpaperId::WALLPAPER_0);
    EXPECT_EQ(colors[0].r, 255);
    EXPECT_EQ(colors[0].g, 0);
    EXPECT_EQ(colors[1].g, 255);
    EXPECT_EQ(colors[1].r, 0);
    EXPECT_EQ(colors[2].b, 255);
    EXPECT_EQ(colors[3].r, 0);
}

}  // namespace
}  // namespace ostinato::text
