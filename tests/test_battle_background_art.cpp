// Every battle background decoded from the cartridge: its blocks the sizes the loader transfers,
// its tilemap one map, every cell naming a palette the background has and a tile a block it loads
// holds, and its colors.
#include <gtest/gtest.h>

#include <array>
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

// The three backgrounds whose pictures reach outside their own art, and how far. Background 49
// shows cells naming tiles its blocks' art never decoded, where the previous block's leftovers
// stand in the shared buffer; background 50 shows two cells naming tiles no block covers at all.
constexpr auto kBackgroundWithStaleTails = static_cast<BattleBackgroundId>(49);
constexpr int kStaleTailCells = 251;
constexpr auto kBackgroundWithOrphanCells = static_cast<BattleBackgroundId>(50);
constexpr std::array<unsigned, 2> kOrphanTiles{652, 653};

// Every block a record names is loaded, in order, and taken as 128 tiles — 256 for a double-width
// first block; a record with an empty first slot still loads its second and third. Every tilemap
// decodes to one map, and every shown cell names one of the background's three palettes. Every
// shown cell names a tile a loaded block holds, except two in background 50; and every shown cell
// reaches a tile its block's own art decoded, except 251 in background 49, which show the previous
// block's leftovers.
TEST_F(BattleBackgroundArtTest, EveryBackgroundDecodesToItsBlocksAndMap) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    ASSERT_TRUE(content.battleBackgrounds.valid());
    ASSERT_TRUE(content.routines != nullptr);
    int shortBlocks = 0;
    int emptyFirstSlots = 0;
    for (const BattleBackgroundPropertiesEntry& entry : battleBackgroundTable()) {
        const BattleBackgroundPicture picture =
            content.battleBackgrounds.load(entry.id, *content.routines);
        const BattleBackgroundProperties& record = entry.record;
        const int id = static_cast<int>(entry.id);

        // Which blocks load, and how many tiles each is taken as.
        const bool firstLoaded = !record.graphics1.isEmpty();
        const bool firstDouble = firstLoaded && record.graphics1.isDoubleWidth();
        if (firstDouble) {
            EXPECT_EQ(record.graphics2, BattleBackgroundGraphicsId::NONE) << id;
        }
        emptyFirstSlots += firstLoaded ? 0 : 1;
        const std::array<bool, 3> loaded{firstLoaded, record.graphics2 != BattleBackgroundGraphicsId::NONE,
                                         record.graphics3 != BattleBackgroundGraphicsId::NONE};
        const std::array<std::size_t, 3> taken{firstDouble ? kBackgroundDoubleBlockTiles : kBackgroundBlockTiles,
                                               kBackgroundBlockTiles, kBackgroundBlockTiles};
        std::size_t expectedTiles = 0;
        for (std::size_t block = 0; block < 3; ++block) {
            if (!loaded[block]) {
                EXPECT_FALSE(picture.blockFirstTile[block].has_value()) << id << " block " << block;
                continue;
            }
            EXPECT_EQ(picture.blockFirstTile[block], std::optional<std::size_t>{expectedTiles})
                << id << " block " << block;
            EXPECT_EQ(picture.blockTiles[block], taken[block]) << id << " block " << block;
            EXPECT_GT(picture.blockOwnTiles[block], 0u) << id << " block " << block;
            if (picture.blockOwnTiles[block] < taken[block]) {
                ++shortBlocks;
            }
            expectedTiles += taken[block];
        }
        EXPECT_EQ(picture.tileCount(), expectedTiles) << id;

        // The shown cells.
        int orphanCells = 0;
        std::set<unsigned> orphanTiles;
        int staleTailCells = 0;
        for (std::size_t cell = 0; cell < static_cast<std::size_t>(kBackgroundColumns * kBackgroundRowsShown);
             ++cell) {
            const BackgroundCell& c = picture.cells[cell];
            EXPECT_GE(c.palette(), kBackgroundFirstPalette) << id << " cell " << cell;
            EXPECT_LT(c.palette(), kBackgroundFirstPalette + kBackgroundPalettes) << id << " cell " << cell;
            const std::optional<std::size_t> tile = picture.sheetTile(c.tile());
            if (!tile) {
                ++orphanCells;
                orphanTiles.insert(c.tile());
                continue;
            }
            for (std::size_t block = 0; block < 3; ++block) {
                if (!picture.blockFirstTile[block] || *tile < *picture.blockFirstTile[block]) {
                    continue;
                }
                const std::size_t within = *tile - *picture.blockFirstTile[block];
                if (within < picture.blockTiles[block] && within >= picture.blockOwnTiles[block]) {
                    ++staleTailCells;
                }
            }
        }
        if (entry.id == kBackgroundWithOrphanCells) {
            EXPECT_EQ(orphanCells, static_cast<int>(kOrphanTiles.size()));
            EXPECT_EQ(orphanTiles, (std::set<unsigned>{kOrphanTiles.begin(), kOrphanTiles.end()}));
        } else {
            EXPECT_EQ(orphanCells, 0) << id;
        }
        EXPECT_EQ(staleTailCells, entry.id == kBackgroundWithStaleTails ? kStaleTailCells : 0) << id;

        bool anyColor = false;
        for (const CartridgeColor& color : picture.colors) {
            anyColor = anyColor || color.word != 0;
        }
        EXPECT_TRUE(anyColor) << id;
    }
    // Three records leave their first slot empty and build from the later blocks; many blocks' art
    // is shorter than the transfer that takes it.
    EXPECT_EQ(emptyFirstSlots, 3);
    EXPECT_GT(shortBlocks, 0);
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
