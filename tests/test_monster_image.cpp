// Every monster's picture decoded from the cartridge through its stencil, the bytes its tiles take,
// a tile traced by hand, and how many palettes a formation needs.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <set>

#include "assets/cartridge.h"
#include "assets/tile_planes.h"
#include "data/formations.h"
#include "data/monster_image.h"
#include "data/monster_properties.h"
#include "data/rom_regions.h"
#include "ostinato/monster_id.h"
#include "vanilla_rom.h"

namespace ostinato {
namespace {

class MonsterImageTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    static test::IngestedCartridge* cartridge_;
};

test::IngestedCartridge* MonsterImageTest::cartridge_ = nullptr;

// Every monster decodes at its stencil's full extent: its tiles take exactly as many bytes as its
// filled cells need, and its picture is as big as the extent says.
TEST_F(MonsterImageTest, EveryMonsterDecodesThroughItsStencil) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    ASSERT_TRUE(content.monsterImages.valid());
    int threePlanes = 0;
    const std::size_t monsterCount = monsterProperties().size();
    for (std::size_t i = 0; i < monsterCount; ++i) {
        const auto monster = static_cast<MonsterId>(i);
        const MonsterStencil stencil = content.monsterSprites.stencil(monster);
        ASSERT_TRUE(stencil.valid()) << i;
        const MonsterSpriteSize extent = stencil.extent();
        const MonsterImage image = content.monsterImages.image(monster, extent);
        EXPECT_EQ(image.pixels.width, extent.width * assets::kTilePx) << i;
        EXPECT_EQ(image.pixels.height, extent.height * assets::kTilePx) << i;
        std::size_t filled = 0;
        for (int row = 0; row < extent.height; ++row) {
            for (int column = 0; column < extent.width; ++column) {
                filled += stencil.filled(row, column) ? 1 : 0;
            }
        }
        EXPECT_EQ(image.tileBytesRead, filled * (image.threePlanes ? 24u : 32u)) << i;
        threePlanes += image.threePlanes ? 1 : 0;
        // A three-plane monster's pixels never reach the upper eight colors.
        if (image.threePlanes) {
            for (const std::uint8_t pixel : image.pixels.pixels) {
                ASSERT_LT(pixel, 8) << i;
            }
        }
    }
    EXPECT_GT(threePlanes, 0);
    EXPECT_LT(threePlanes, static_cast<int>(monsterCount));
}

// An empty stencil cell shows nothing: a monster's picture has a see-through pixel wherever its
// stencil is empty.
TEST_F(MonsterImageTest, EmptyCellsAreSeeThrough) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    for (std::size_t i = 0; i < monsterProperties().size(); ++i) {
        const auto monster = static_cast<MonsterId>(i);
        const MonsterStencil stencil = content.monsterSprites.stencil(monster);
        const MonsterSpriteSize extent = stencil.extent();
        const MonsterImage image = content.monsterImages.image(monster, extent);
        for (int row = 0; row < extent.height; ++row) {
            for (int column = 0; column < extent.width; ++column) {
                if (stencil.filled(row, column)) {
                    continue;
                }
                for (int y = 0; y < assets::kTilePx; ++y) {
                    for (int x = 0; x < assets::kTilePx; ++x) {
                        ASSERT_EQ(image.pixels.at(column * assets::kTilePx + x, row * assets::kTilePx + y), 0)
                            << i;
                    }
                }
            }
        }
    }
}

// The first monster's first tile, traced from the cartridge: the graphics word gives the tiles'
// offset into the family, and the first tile's first row decodes from its planes' first bytes.
TEST_F(MonsterImageTest, TheFirstTileIsReadWhereTheGraphicsWordPoints) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    const auto monster = MonsterId::GUARD;
    const std::span<const std::uint8_t> record = content.monsterSprites.properties(monster);
    ASSERT_EQ(record.size(), 5u);
    const unsigned word = record[0] | record[1] << 8;
    const std::size_t offset = static_cast<std::size_t>(word & 0x7FFF) * 8;
    const bool threePlanes = (word & 0x8000) != 0;
    const std::vector<std::uint8_t> graphics = test::romSlice(cartridge_->image, RomAsset::MONSTER_GFX_TRIMMED);
    ASSERT_LT(offset + 32, graphics.size());

    const MonsterStencil stencil = content.monsterSprites.stencil(monster);
    const MonsterImage image = content.monsterImages.image(monster, stencil.extent());
    EXPECT_EQ(image.threePlanes, threePlanes);
    // The first filled cell of the top row takes the first tile.
    int firstColumn = 0;
    while (!stencil.filled(0, firstColumn)) {
        ++firstColumn;
    }
    for (int x = 0; x < assets::kTilePx; ++x) {
        const unsigned bit = 7u - static_cast<unsigned>(x);
        unsigned expected = ((graphics[offset] >> bit) & 1u) | ((graphics[offset + 1] >> bit) & 1u) << 1;
        if (threePlanes) {
            expected |= ((graphics[offset + 16] >> bit) & 1u) << 2;
        } else {
            expected |= ((graphics[offset + 16] >> bit) & 1u) << 2 | ((graphics[offset + 17] >> bit) & 1u) << 3;
        }
        EXPECT_EQ(image.pixels.at(firstColumn * assets::kTilePx + x, 0), expected) << x;
    }
}

// The Ghost Train stands 16 x 14 whatever its stencil says, and decodes at that size.
TEST_F(MonsterImageTest, TheGhostTrainDecodesAtItsFixedSize) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    const MonsterSpriteSize size = content.monsterSprites.size(MonsterId::GHOSTTRAIN, {.width = 16, .height = 16});
    EXPECT_EQ(size, (MonsterSpriteSize{.width = 16, .height = 14}));
    const MonsterImage image = content.monsterImages.image(MonsterId::GHOSTTRAIN, size);
    EXPECT_EQ(image.pixels.width, 128);
    EXPECT_EQ(image.pixels.height, 112);
}

// How many distinct palettes the monsters of one formation need. The battle loads at most three;
// a fourth would read as the first. No shipped formation reaches a fourth.
TEST_F(MonsterImageTest, NoFormationNeedsAFourthMonsterPalette) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::IngestedContent& content = *cartridge_->content;
    std::size_t most = 0;
    for (const FormationEntry& entry : formations()) {
        std::set<std::uint16_t> palettes;
        for (std::size_t slot = 0; slot < 6; ++slot) {
            if (!entry.record.slotEmpty(slot)) {
                palettes.insert(content.monsterImages.paletteIndex(entry.record.monsterId(slot)));
            }
        }
        most = std::max(most, palettes.size());
        EXPECT_LE(palettes.size(), 3u) << "formation " << static_cast<int>(entry.id);
    }
    EXPECT_EQ(most, 3u);
}

}  // namespace
}  // namespace ostinato
