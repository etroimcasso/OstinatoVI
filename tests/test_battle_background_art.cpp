// Every battle background decoded from the cartridge: its blocks the sizes the loader transfers,
// its tilemap one map, every cell naming a palette the background has and a tile a block it loads
// holds, and its colors.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>

#include "assets/cartridge.h"
#include "data/battle_background_art.h"
#include "data/battle_backgrounds.h"
#include "ostinato/battle_background_id.h"
#include "vanilla_rom.h"

namespace ostinato {
namespace {

class BattleBackgroundArtTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    static test::IngestedCartridge* cartridge_;
};

test::IngestedCartridge* BattleBackgroundArtTest::cartridge_ = nullptr;

// Every block a record loads is taken as its 64 or 128 tiles, every tilemap decodes to one map, and
// every cell names one of the background's three palettes and a tile a loaded block holds. No
// shown cell reaches past the tiles a block's own art decoded to.
TEST_F(BattleBackgroundArtTest, EveryBackgroundDecodesToItsBlocksAndMap) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    ASSERT_TRUE(content.battleBackgrounds.valid());
    ASSERT_TRUE(content.routines != nullptr);
    int shortBlocks = 0;
    int cellsPastOwnArt = 0;
    for (const BattleBackgroundPropertiesEntry& entry : battleBackgroundTable()) {
        const BattleBackgroundPicture picture =
            content.battleBackgrounds.load(entry.id, *content.routines);
        const BattleBackgroundProperties& record = entry.record;

        std::size_t expectedTiles = 0;
        if (!record.graphics1.isEmpty()) {
            expectedTiles += record.graphics1.isDoubleWidth() ? kBackgroundDoubleBlockTiles
                                                              : kBackgroundBlockTiles;
            EXPECT_EQ(picture.blockFirstTile[0], std::optional<std::size_t>{0});
        } else {
            EXPECT_FALSE(picture.blockFirstTile[0].has_value());
        }
        if (!record.graphics1.isDoubleWidth() && record.graphics2 != BattleBackgroundGraphicsId::NONE) {
            EXPECT_EQ(picture.blockFirstTile[1], std::optional<std::size_t>{expectedTiles});
            expectedTiles += kBackgroundBlockTiles;
        } else {
            EXPECT_FALSE(picture.blockFirstTile[1].has_value());
        }
        if (record.graphics3 != BattleBackgroundGraphicsId::NONE) {
            EXPECT_EQ(picture.blockFirstTile[2], std::optional<std::size_t>{expectedTiles});
            expectedTiles += kBackgroundBlockTiles;
        } else {
            EXPECT_FALSE(picture.blockFirstTile[2].has_value());
        }
        EXPECT_EQ(picture.tileCount(), expectedTiles) << static_cast<int>(entry.id);
        for (std::size_t block = 0; block < 3; ++block) {
            if (!picture.blockFirstTile[block]) {
                continue;
            }
            const std::size_t taken = block == 0 && record.graphics1.isDoubleWidth()
                                          ? kBackgroundDoubleBlockTiles
                                          : kBackgroundBlockTiles;
            EXPECT_GT(picture.blockOwnTiles[block], 0u) << static_cast<int>(entry.id) << " block " << block;
            if (picture.blockOwnTiles[block] < taken) {
                ++shortBlocks;
            }
        }

        for (std::size_t cell = 0; cell < picture.cells.size(); ++cell) {
            const BackgroundCell& c = picture.cells[cell];
            EXPECT_GE(c.palette(), kBackgroundFirstPalette) << static_cast<int>(entry.id) << " cell " << cell;
            EXPECT_LT(c.palette(), kBackgroundFirstPalette + kBackgroundPalettes)
                << static_cast<int>(entry.id) << " cell " << cell;
            // Only the rows the battlefield shows have to name a loaded tile; the rows below are
            // never on screen unless the background scrolls.
            if (cell / kBackgroundColumns >= static_cast<std::size_t>(kBackgroundRowsShown)) {
                continue;
            }
            const std::optional<std::size_t> tile = picture.sheetTile(c.tile());
            EXPECT_TRUE(tile.has_value())
                << static_cast<int>(entry.id) << " cell " << cell << " tile " << c.tile() << " blocks "
                << picture.blockOwnTiles[0] << "/" << picture.blockOwnTiles[1] << "/"
                << picture.blockOwnTiles[2];
            if (!tile) {
                continue;
            }
            for (std::size_t block = 0; block < 3; ++block) {
                if (picture.blockFirstTile[block] && *tile >= *picture.blockFirstTile[block] &&
                    *tile < *picture.blockFirstTile[block] + (block == 0 && record.graphics1.isDoubleWidth()
                                                                  ? kBackgroundDoubleBlockTiles
                                                                  : kBackgroundBlockTiles)) {
                    if (*tile - *picture.blockFirstTile[block] >= picture.blockOwnTiles[block]) {
                        ++cellsPastOwnArt;
                    }
                }
            }
        }
        bool anyColor = false;
        for (const CartridgeColor& color : picture.colors) {
            anyColor = anyColor || color.word != 0;
        }
        EXPECT_TRUE(anyColor) << static_cast<int>(entry.id);
    }
    // Some blocks' art is shorter than the transfer that takes it, but nothing on screen reaches
    // the tiles past the art.
    EXPECT_GT(shortBlocks, 0);
    EXPECT_EQ(cellsPastOwnArt, 0);
}

// The first background: three single-width blocks, so tiles 0-127, 128-255 and 896-1023 are drawn
// and the numbers between are nobody's.
TEST_F(BattleBackgroundArtTest, TheFieldsTilesLandInThreeBlocks) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    const BattleBackgroundPicture picture =
        content.battleBackgrounds.load(BattleBackgroundId::FIELD_WOB, *content.routines);
    EXPECT_EQ(picture.tileCount(), 3 * kBackgroundBlockTiles);
    EXPECT_EQ(picture.sheetTile(0), std::optional<std::size_t>{0});
    EXPECT_EQ(picture.sheetTile(127), std::optional<std::size_t>{127});
    EXPECT_EQ(picture.sheetTile(128), std::optional<std::size_t>{128});
    EXPECT_EQ(picture.sheetTile(255), std::optional<std::size_t>{255});
    EXPECT_FALSE(picture.sheetTile(256).has_value());
    EXPECT_FALSE(picture.sheetTile(895).has_value());
    EXPECT_EQ(picture.sheetTile(896), std::optional<std::size_t>{256});
    EXPECT_EQ(picture.sheetTile(1023), std::optional<std::size_t>{383});
}

// A double-width first block covers tiles 0-255 and leaves no room for a second block.
TEST_F(BattleBackgroundArtTest, ADoubleWidthFirstBlockCoversTheSecondsNumbers) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    std::optional<BattleBackgroundId> doubled;
    for (const BattleBackgroundPropertiesEntry& entry : battleBackgroundTable()) {
        if (entry.record.graphics1.isDoubleWidth()) {
            doubled = entry.id;
            break;
        }
    }
    ASSERT_TRUE(doubled.has_value());
    const BattleBackgroundPicture picture = content.battleBackgrounds.load(*doubled, *content.routines);
    EXPECT_EQ(picture.sheetTile(255), std::optional<std::size_t>{255});
    EXPECT_FALSE(picture.blockFirstTile[1].has_value());
}

// One background's map graphics block begins near the end of the map graphics and is copied on past
// them, 4,288 bytes into whatever follows, as the loader copies it.
TEST_F(BattleBackgroundArtTest, AMapGraphicsBlockIsCopiedPastTheFamilysEnd) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    const auto id = static_cast<BattleBackgroundId>(38);
    EXPECT_EQ(battleBackgroundProperties(id).graphics2, static_cast<BattleBackgroundGraphicsId>(72));
    const BattleBackgroundPicture picture = content.battleBackgrounds.load(id, *content.routines);
    ASSERT_TRUE(picture.blockFirstTile[1].has_value());
    EXPECT_EQ(picture.blockOwnTiles[1], kBackgroundDoubleBlockTiles);
}

// Three records write a bare 0 where a block is unused; the loader loads block 0 for them, and so
// does the decoder.
TEST_F(BattleBackgroundArtTest, ABareZeroLoadsBlockZero) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    const BattleBackgroundProperties& waterfall = battleBackgroundProperties(BattleBackgroundId::WATERFALL);
    ASSERT_EQ(waterfall.graphics3, BattleBackgroundGraphicsId::TOWN_EXT_1);
    const BattleBackgroundPicture picture =
        content.battleBackgrounds.load(BattleBackgroundId::WATERFALL, *content.routines);
    EXPECT_TRUE(picture.blockFirstTile[2].has_value());
}

}  // namespace
}  // namespace ostinato
