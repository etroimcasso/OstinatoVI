// The small font's glyph sheet from the cartridge and the eight palettes the battle draws it through.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "assets/cartridge.h"
#include "data/cartridge_color.h"
#include "data/rom_regions.h"
#include "text/small_font.h"
#include "vanilla_rom.h"

namespace ostinato::text {
namespace {

class SmallFontTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    static test::IngestedCartridge* cartridge_;
};

test::IngestedCartridge* SmallFontTest::cartridge_ = nullptr;

// 256 glyphs on sixteen rows; the digit zero (glyph $B4) has ink and the blank glyph ($FF) none.
TEST_F(SmallFontTest, TheSheetHoldsEveryGlyph) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    ASSERT_EQ(content.smallFont.size(), kSmallFontBytes);
    const IndexedImage sheet = buildSmallFontSheet(content.smallFont);
    EXPECT_EQ(sheet.width, 128);
    EXPECT_EQ(sheet.height, 128);
    const auto hasInk = [&](std::size_t glyph) {
        const int x0 = static_cast<int>(glyph % 16) * 8;
        const int y0 = static_cast<int>(glyph / 16) * 8;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if (sheet.at(x0 + x, y0 + y) != 0) {
                    return true;
                }
            }
        }
        return false;
    };
    EXPECT_TRUE(hasInk(0xB4));
    EXPECT_FALSE(hasInk(0xFF));
    for (const std::uint8_t pixel : sheet.pixels) {
        ASSERT_LT(pixel, 4);
    }
}

// Colors 0-15 become palettes 0-3 with the player's color at palette 0's third entry, palette 4 is
// black, and colors 20-31 become palettes 5-7.
TEST_F(SmallFontTest, ThePalettesLayOutAsTheBattleLoadsThem) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    ASSERT_EQ(content.smallFontColors.size(), kSmallFontColorCount * 2);
    const CartridgeColor white{0x7FFF};
    const SmallFontPalettes palettes = smallFontPalettes(content.smallFontColors, white);
    for (std::size_t color = 0; color < 16; ++color) {
        if (color == kFontColorEntry) {
            EXPECT_EQ(palettes[0][3], white);
            continue;
        }
        EXPECT_EQ(palettes[color / 4][color % 4], colorAt(content.smallFontColors.data(), static_cast<unsigned>(color)))
            << color;
    }
    for (const CartridgeColor& color : palettes[4]) {
        EXPECT_EQ(color, CartridgeColor{});
    }
    for (std::size_t color = 20; color < 32; ++color) {
        EXPECT_EQ(palettes[color / 4][color % 4], colorAt(content.smallFontColors.data(), static_cast<unsigned>(color)))
            << color;
    }
}

}  // namespace
}  // namespace ostinato::text
