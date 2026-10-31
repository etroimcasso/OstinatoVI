// Every party member's battle sheets cut from the cartridge: the frames where the frame table puts
// them, the fallen frame as two halves, the mirrored legs, and the sixteen colors.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "assets/cartridge.h"
#include "assets/tile_planes.h"
#include "data/battle_gfx_tables.h"
#include "data/character_battle_sheet.h"
#include "data/rom_regions.h"
#include "ostinato/character_gfx_id.h"
#include "vanilla_rom.h"

namespace ostinato {
namespace {

class CharacterBattleSheetTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    static test::IngestedCartridge* cartridge_;
};

test::IngestedCartridge* CharacterBattleSheetTest::cartridge_ = nullptr;

bool anyInk(const text::IndexedImage& image, int x0, int y0, int width, int height) {
    for (int y = y0; y < y0 + height; ++y) {
        for (int x = x0; x < x0 + width; ++x) {
            if (image.at(x, y) != 0) {
                return true;
            }
        }
    }
    return false;
}

// Every slot's sheet cuts: the standing sheet is 32 cells of 16 x 32 with every standing frame's
// top row blank and ink below it, and the fallen frame has ink in both halves.
TEST_F(CharacterBattleSheetTest, EverySlotCutsItsThirtyTwoFrames) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    ASSERT_TRUE(content.characterSheets.valid());
    for (const CharacterSheetEntry& entry : characterSheets()) {
        const CharacterBattleSheet sheet = content.characterSheets.sheet(entry.graphics);
        EXPECT_EQ(sheet.standing.width, kCharacterFrameWidthPx);
        EXPECT_EQ(sheet.standing.height, kCharacterFrameHeightPx * 32);
        EXPECT_EQ(sheet.fallen.width, kFallenFrameWidthPx);
        EXPECT_EQ(sheet.fallen.height, kFallenFrameHeightPx);
        for (int frame = 1; frame < 32; ++frame) {
            const int top = frame * kCharacterFrameHeightPx;
            EXPECT_FALSE(anyInk(sheet.standing, 0, top, 16, 8))
                << static_cast<int>(entry.graphics) << " frame " << frame;
            EXPECT_TRUE(anyInk(sheet.standing, 0, top + 8, 16, 24))
                << static_cast<int>(entry.graphics) << " frame " << frame;
        }
        EXPECT_FALSE(anyInk(sheet.standing, 0, 0, 16, 32)) << static_cast<int>(entry.graphics);
        EXPECT_TRUE(anyInk(sheet.fallen, 0, 0, 16, 16)) << static_cast<int>(entry.graphics);
        EXPECT_TRUE(anyInk(sheet.fallen, 16, 0, 16, 16)) << static_cast<int>(entry.graphics);
    }
}

// Terra's frame 1 is sheet tiles 0-5 in reading order below a blank row: its first ink row is the
// sheet's first tile's first row, decoded from the cartridge's own bytes.
TEST_F(CharacterBattleSheetTest, AFrameIsCutFromTheSheetTheTableNames) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    const CharacterBattleSheet sheet = content.characterSheets.sheet(CharacterGfxId::TERRA);
    const std::vector<std::uint8_t> family = test::romSlice(cartridge_->image, RomAsset::MAP_SPRITE_GFX);
    const SheetPlace place = characterSheet(CharacterGfxId::TERRA);
    const auto tiles = characterFrameTiles(1);
    ASSERT_EQ(tiles[2], SheetTile::at(0));
    ASSERT_EQ(tiles[3], SheetTile::at(1));
    for (std::size_t t = 2; t < 4; ++t) {
        const std::size_t at = place.offset + static_cast<std::size_t>(tiles[t].raw);
        for (int x = 0; x < 8; ++x) {
            const unsigned bit = 7u - static_cast<unsigned>(x);
            const unsigned expected = ((family[at] >> bit) & 1u) | ((family[at + 1] >> bit) & 1u) << 1 |
                                      ((family[at + 16] >> bit) & 1u) << 2 |
                                      ((family[at + 17] >> bit) & 1u) << 3;
            EXPECT_EQ(sheet.standing.at(static_cast<int>(t - 2) * 8 + x, kCharacterFrameHeightPx + 8),
                      expected)
                << t << " " << x;
        }
    }
}

// Frames 3 and 16 have their leg row mirrored in the cut: frame 3's legs are frame 1's legs
// reversed left to right, because its two leg tiles are frame 1's two swapped and each mirrored.
TEST_F(CharacterBattleSheetTest, TheMirroredLegFramesReverseTheirLegRow) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    const auto one = characterFrameTiles(1);
    const auto three = characterFrameTiles(3);
    ASSERT_EQ(three[6], one[7]);
    ASSERT_EQ(three[7], one[6]);
    const CharacterBattleSheet sheet = content.characterSheets.sheet(CharacterGfxId::TERRA);
    bool anyDifference = false;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int legsRow = 24 + y;
            const std::uint8_t mirrored = sheet.standing.at(x, 3 * kCharacterFrameHeightPx + legsRow);
            const std::uint8_t straight = sheet.standing.at(15 - x, 1 * kCharacterFrameHeightPx + legsRow);
            EXPECT_EQ(mirrored, straight) << x << " " << y;
            anyDifference = anyDifference || sheet.standing.at(x, 3 * kCharacterFrameHeightPx + legsRow) !=
                                                 sheet.standing.at(x, 1 * kCharacterFrameHeightPx + legsRow);
        }
    }
    EXPECT_TRUE(anyDifference);
}

// Sixteen colors: the slot's twelve from its battle palette, then the party slot's four.
TEST_F(CharacterBattleSheetTest, SixteenColorsFromTheBattlePaletteAndTheSlot) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    const auto colors = characterColors(content.characterSheets, CharacterGfxId::TERRA, 2);
    const auto palette = content.characterSheets.paletteColors(CharacterGfxId::TERRA);
    for (std::size_t i = 0; i < 12; ++i) {
        EXPECT_EQ(colors[i], palette[i]) << i;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(colors[12 + i], statusColors(2)[i]) << i;
    }
    // The green soldier draws the soldier's sheet through the first battle palette.
    const auto green = content.characterSheets.paletteColors(CharacterGfxId::SOLDIER, true);
    const auto brown = content.characterSheets.paletteColors(CharacterGfxId::SOLDIER);
    EXPECT_NE(green, brown);
    const std::vector<std::uint8_t> family = test::romSlice(cartridge_->image, RomAsset::BATTLE_CHAR_PAL);
    EXPECT_EQ(green[1], colorAt(family.data(), 1));
}

}  // namespace
}  // namespace ostinato
