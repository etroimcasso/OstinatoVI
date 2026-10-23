// The dialog font as the cartridge holds it — the width table, the glyph artwork, and the choice
// arrow — and the two sheets built from them.
//
// All three are cartridge content, so nothing here is compared against a stored copy: the widths
// and the arrow are read where the region table places them and checked against the values the
// upstream source states, and the glyph sheet is checked pixel by pixel against the cartridge's own
// artwork for every glyph. The bit order is pinned separately on hand-made artwork.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "data/rom_regions.h"
#include "ostinato/rom_asset.h"
#include "text/font.h"
#include "text/layout.h"
#include "vanilla_rom.h"

namespace ostinato::text {
namespace {

// The English cartridge: glyph codes $00-$FF have widths, $20-$FF have artwork.
constexpr std::size_t kEnglishWidths = 256;
constexpr std::size_t kEnglishGlyphs = 224;
constexpr std::size_t kJapaneseWidths = 1280;
constexpr std::size_t kJapaneseGlyphs = 1248;

// The English letter A (tools/char_table/dialog_en.json: 0x20).
constexpr std::uint16_t kLetterA = 0x20;

class DialogFontTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = test::ingestVanilla(); }

    static const DialogFont& font() { return cartridge_.content->dialogFont; }

    // Whether the cartridge's artwork for `slot` has ink at (x, row), read the way the format is
    // documented: the row is a 16-bit word, low byte first, leftmost pixel in bit 15.
    static bool artInk(std::span<const std::uint8_t> art, std::size_t slot, int x, int row) {
        const std::size_t at = slot * kGlyphArtBytes + static_cast<std::size_t>(row) * 2;
        const unsigned word = art[at] | (static_cast<unsigned>(art[at + 1]) << 8);
        return (word & (0x8000u >> x)) != 0;
    }
};

// --- where the width table and the arrow live --------------------------------------------------

// The width table has no file of its own; it is the gap between the two font files, in both
// languages.
TEST(DialogFontRegions, WidthTableFillsTheGapBetweenTheFonts) {
    for (const Language language : {Language::EN, Language::JP}) {
        const auto small = romRegion(RomAsset::SMALL_FONT, language);
        const auto widths = romRegion(RomAsset::FONT_WIDTH, language);
        const auto large = romRegion(RomAsset::LARGE_FONT, language);
        ASSERT_TRUE(small && widths && large);
        EXPECT_EQ(small->at + small->size, widths->at);
        EXPECT_EQ(widths->at + widths->size, large->at);
    }
    EXPECT_EQ(romRegion(RomAsset::FONT_WIDTH, Language::EN)->size, kEnglishWidths);
    EXPECT_EQ(romRegion(RomAsset::FONT_WIDTH, Language::JP)->size, kJapaneseWidths);
}

// The Japanese field code moves the arrow by an amount only assembling the source would settle,
// and no Japanese cartridge is available to check a derived address against. The row is left out
// rather than guessed.
TEST(DialogFontRegions, NoJapaneseArrowAddressIsClaimed) {
    EXPECT_TRUE(romRegion(RomAsset::DLG_CURSOR_GFX, Language::EN).has_value());
    EXPECT_FALSE(romRegion(RomAsset::DLG_CURSOR_GFX, Language::JP).has_value());
    EXPECT_EQ(romRegion(RomAsset::DLG_CURSOR_GFX, Language::EN)->size,
              static_cast<std::uint32_t>(kCursorRows));
}

// --- the widths --------------------------------------------------------------------------------

TEST_F(DialogFontTest, WidthsAreTheCartridgesTable) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const FontMetrics& metrics = font().metrics;
    ASSERT_EQ(metrics.size(), kEnglishWidths);

    const auto table = test::romSlice(cartridge_.image, RomAsset::FONT_WIDTH);
    ASSERT_EQ(table.size(), kEnglishWidths);
    for (std::size_t glyph = 0; glyph < kEnglishWidths; ++glyph) {
        EXPECT_EQ(metrics.width(static_cast<std::uint16_t>(glyph)), table[glyph]) << glyph;
    }
}

TEST_F(DialogFontTest, WidthsHoldTheValuesTheSourceStates) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const FontMetrics& metrics = font().metrics;
    // gfx/font_gfx.asm:16-31.
    EXPECT_EQ(metrics.width(0x00), 12);          // a control code
    EXPECT_EQ(metrics.width(kLetterA), 8);       // row $20
    EXPECT_EQ(metrics.width(kSpaceGlyph), 5);    // row $70, last entry
    EXPECT_EQ(metrics.width(0x9F), 5);           // row $90, last entry
    EXPECT_EQ(metrics.width(0xFE), 12);          // row $F0
    EXPECT_EQ(metrics.width(kWideSpaceGlyph), 6);
}

// --- the arrow ---------------------------------------------------------------------------------

TEST_F(DialogFontTest, ArrowIsTheCartridgesArt) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const CursorArt& arrow = font().cursor;
    ASSERT_TRUE(arrow.loaded());

    // field/text.asm:1185-1196: a right-pointing arrow, one pixel at its tip rows, six at its
    // widest, starting two pixels in, and a blank last row.
    std::array<int, kCursorRows> inkPerRow{};
    for (int y = 0; y < kCursorRows; ++y) {
        for (int x = 0; x < kCursorWidth; ++x) {
            inkPerRow[static_cast<std::size_t>(y)] += arrow.ink(x, y) ? 1 : 0;
        }
        EXPECT_FALSE(arrow.ink(0, y));
        EXPECT_FALSE(arrow.ink(1, y));
    }
    EXPECT_EQ(inkPerRow, (std::array<int, kCursorRows>{1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0}));
    EXPECT_TRUE(arrow.ink(2, 0));
    EXPECT_TRUE(arrow.ink(7, 5));
}

// --- the glyph artwork --------------------------------------------------------------------------

TEST(DialogFontGlyphs, EveryCodeWithAWidthFromTheFirstDrawnOneHasArtwork) {
    const std::vector<std::uint8_t> english(kEnglishWidths);
    const std::vector<std::uint8_t> japanese(kJapaneseWidths);
    EXPECT_EQ((DialogFont{.metrics = FontMetrics{english}}.glyphCount()), kEnglishGlyphs);
    EXPECT_EQ((DialogFont{.metrics = FontMetrics{japanese}}.glyphCount()), kJapaneseGlyphs);
    EXPECT_EQ(DialogFont{}.glyphCount(), 0u);
}

TEST(DialogFontGlyphs, SlotsCountFromTheFirstDrawnGlyph) {
    EXPECT_EQ(glyphSlot(kFirstDrawnGlyph), 0u);
    EXPECT_EQ(glyphSlot(kLetterA), 0u);
    EXPECT_EQ(glyphSlot(kWideSpaceGlyph), kEnglishGlyphs - 1);
}

TEST_F(DialogFontTest, CartridgeHoldsArtworkForEveryGlyph) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    EXPECT_EQ(font().glyphCount(), kEnglishGlyphs);
    EXPECT_GE(font().glyphArt.size(), kEnglishGlyphs * kGlyphArtBytes);
}

// --- the glyph sheet ----------------------------------------------------------------------------

// One glyph of hand-made artwork with a single bit set in row 0.
std::vector<std::uint8_t> oneBit(std::uint8_t low, std::uint8_t high) {
    std::vector<std::uint8_t> art(kGlyphArtBytes, 0);
    art[0] = low;
    art[1] = high;
    return art;
}

// The pixels with ink in row 0 of the first cell.
std::vector<int> inkInFirstRow(const IndexedImage& sheet) {
    std::vector<int> xs;
    for (int x = 0; x < kGlyphCellPx; ++x) {
        if (sheet.at(x, 0) == kInkPixel) {
            xs.push_back(x);
        }
    }
    return xs;
}

TEST(GlyphSheet, TheHighBitOfTheSecondByteIsTheLeftmostPixel) {
    EXPECT_EQ(inkInFirstRow(buildGlyphSheet(oneBit(0x00, 0x80), 1)), std::vector<int>{0});
    EXPECT_EQ(inkInFirstRow(buildGlyphSheet(oneBit(0x00, 0x01), 1)), std::vector<int>{7});
    EXPECT_EQ(inkInFirstRow(buildGlyphSheet(oneBit(0x80, 0x00), 1)), std::vector<int>{8});
    EXPECT_EQ(inkInFirstRow(buildGlyphSheet(oneBit(0x01, 0x00), 1)), std::vector<int>{15});
}

TEST(GlyphSheet, GlyphsFillRowsOfSixteenCells) {
    // Seventeen glyphs: a full row of cells and one more.
    std::vector<std::uint8_t> art(17 * kGlyphArtBytes, 0);
    art[16 * kGlyphArtBytes + 1] = 0x80;  // glyph 16, row 0, leftmost pixel
    const IndexedImage sheet = buildGlyphSheet(art, 17);
    EXPECT_EQ(sheet.width, kSheetColumns * kGlyphCellPx);
    EXPECT_EQ(sheet.height, 2 * kGlyphCellPx);
    EXPECT_EQ(sheet.at(0, kGlyphCellPx), kInkPixel);
    EXPECT_EQ(sheet.at(kGlyphCellPx, kGlyphCellPx), kBlankPixel);
}

TEST_F(DialogFontTest, SheetMatchesTheCartridgeForEveryGlyph) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const IndexedImage sheet = buildGlyphSheet(font().glyphArt, font().glyphCount());
    ASSERT_EQ(sheet.width, kSheetColumns * kGlyphCellPx);
    ASSERT_EQ(sheet.height, static_cast<int>(kEnglishGlyphs / kSheetColumns) * kGlyphCellPx);

    std::size_t mismatches = 0;
    for (std::size_t slot = 0; slot < kEnglishGlyphs; ++slot) {
        const int cellX = static_cast<int>(slot % kSheetColumns) * kGlyphCellPx;
        const int cellY = static_cast<int>(slot / kSheetColumns) * kGlyphCellPx;
        for (int y = 0; y < kGlyphCellPx; ++y) {
            for (int x = 0; x < kGlyphCellPx; ++x) {
                const bool expected = y < kGlyphRows && artInk(font().glyphArt, slot, x, y);
                const std::uint8_t pixel = sheet.at(cellX + x, cellY + y);
                if (pixel != (expected ? kInkPixel : kBlankPixel)) {
                    ++mismatches;
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0u);
}

// The letter A, traced by hand from its artwork (the cartridge's bytes at $C490C0):
//     .....####.......
//     ....##..##......   x3
//     ....######......
//     ....##..##......   x3
// Its ink starts four pixels into the cell — the pen is the ink's left edge.
TEST_F(DialogFontTest, LetterAMatchesItsTrace) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const IndexedImage sheet = buildGlyphSheet(font().glyphArt, font().glyphCount());
    auto row = [&](int y) {
        std::vector<int> xs;
        for (int x = 0; x < kGlyphCellPx; ++x) {
            if (sheet.at(x, y) == kInkPixel) {
                xs.push_back(x);
            }
        }
        return xs;
    };
    ASSERT_EQ(glyphSlot(kLetterA), 0u);
    EXPECT_EQ(row(0), (std::vector<int>{5, 6, 7, 8}));
    EXPECT_EQ(row(1), (std::vector<int>{4, 5, 8, 9}));
    EXPECT_EQ(row(4), (std::vector<int>{4, 5, 6, 7, 8, 9}));
    EXPECT_EQ(row(7), (std::vector<int>{4, 5, 8, 9}));
    EXPECT_TRUE(row(8).empty());
    EXPECT_EQ(row(1).front(), kGlyphArtLeftOfPen);
}

// --- the arrow sheet ----------------------------------------------------------------------------

TEST_F(DialogFontTest, ArrowSheetIsOneCellOfTheArrow) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const CursorArt& arrow = font().cursor;
    const IndexedImage sheet = buildCursorSheet(arrow);
    ASSERT_EQ(sheet.width, kCursorWidth);
    ASSERT_EQ(sheet.height, kCursorCellHeight);
    for (int y = 0; y < kCursorCellHeight; ++y) {
        for (int x = 0; x < kCursorWidth; ++x) {
            const bool ink = y < kCursorRows && arrow.ink(x, y);
            EXPECT_EQ(sheet.at(x, y), ink ? kInkPixel : kBlankPixel) << x << "," << y;
        }
    }
}

}  // namespace
}  // namespace ostinato::text
