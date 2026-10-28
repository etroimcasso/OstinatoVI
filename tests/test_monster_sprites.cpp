// How big each monster's sprite is: the boxes each formation layout gives its slots, compared with
// their generated fixture, and the stencils read from the cartridge.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "data/formations.h"
#include "data/monster_sprites.h"
#include "fixtures/monster_sprites_expected.h"
#include "ostinato/monster_id.h"
#include "vanilla_rom.h"

namespace ostinato {
namespace {

// --- the boxes ----------------------------------------------------------------------------------

TEST(MonsterSpriteBoxes, EveryBoxMatchesTheFixture) {
    const auto boxes = monsterSpriteBoxes();
    ASSERT_EQ(boxes.size(), test::kExpectedMonsterSpriteBoxes.size());
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const auto& want = test::kExpectedMonsterSpriteBoxes[i];
        EXPECT_EQ(boxes[i].layout, want.layout) << "row " << i;
        EXPECT_EQ(boxes[i].slot, want.slot) << "row " << i;
        EXPECT_EQ(boxes[i].width, want.width) << "row " << i;
        EXPECT_EQ(boxes[i].height, want.height) << "row " << i;
    }
    EXPECT_EQ(boxes.back().layout + 1u, test::kExpectedMonsterSpriteLayoutCount);
}

TEST(MonsterSpriteBoxes, ABoxIsFoundByLayoutAndSlot) {
    // Layout 1 gives its first two slots 8×8 and its last four 4×4.
    EXPECT_EQ(monsterSpriteBox(1, 0), (MonsterSpriteSize{.width = 8, .height = 8}));
    EXPECT_EQ(monsterSpriteBox(1, 5), (MonsterSpriteSize{.width = 4, .height = 4}));
    // Layout 6 is one 16×16 box; it has no second slot.
    EXPECT_EQ(monsterSpriteBox(6, 0), (MonsterSpriteSize{.width = 16, .height = 16}));
    EXPECT_FALSE(monsterSpriteBox(6, 1).has_value());
    // There is no fourteenth layout.
    EXPECT_FALSE(monsterSpriteBox(13, 0).has_value());
}

// A formation never puts a monster in a slot its layout has no box for — reinforcements included.
TEST(MonsterSpriteBoxes, EveryFilledSlotOfEveryFormationHasABox) {
    int filled = 0;
    for (const FormationEntry& entry : formations()) {
        for (std::size_t slot = 0; slot < 6; ++slot) {
            if (entry.record.slotEmpty(slot)) {
                continue;
            }
            ++filled;
            EXPECT_TRUE(monsterSpriteBox(entry.record.vramMap(), slot).has_value())
                << "formation " << static_cast<int>(entry.id) << " slot " << slot;
        }
    }
    EXPECT_GT(filled, 0);
}

// --- the stencils, synthetic -------------------------------------------------------------------

TEST(MonsterStencil, ASmallStencilReadsOneByteARowLeftmostTileInTheTopBit) {
    const std::array<std::uint8_t, 8> bytes = {0x80, 0x01, 0, 0, 0, 0, 0, 0};
    const MonsterStencil stencil{bytes, false};
    ASSERT_TRUE(stencil.valid());
    EXPECT_EQ(stencil.side(), 8);
    EXPECT_TRUE(stencil.filled(0, 0));
    EXPECT_FALSE(stencil.filled(0, 1));
    EXPECT_TRUE(stencil.filled(1, 7));
    EXPECT_FALSE(stencil.filled(1, 8));   // outside the square
    EXPECT_FALSE(stencil.filled(-1, 0));
    EXPECT_EQ(stencil.extent(), (MonsterSpriteSize{.width = 8, .height = 2}));
}

TEST(MonsterStencil, ALargeStencilReadsTwoBytesARowTheSecondHoldingColumnsEightOn) {
    std::array<std::uint8_t, 32> bytes{};
    bytes[0] = 0x80;  // row 0, column 0
    bytes[3] = 0x80;  // row 1, column 8
    bytes[5] = 0x01;  // row 2, column 15
    const MonsterStencil stencil{bytes, true};
    ASSERT_TRUE(stencil.valid());
    EXPECT_EQ(stencil.side(), 16);
    EXPECT_TRUE(stencil.filled(0, 0));
    EXPECT_TRUE(stencil.filled(1, 8));
    EXPECT_FALSE(stencil.filled(1, 7));
    EXPECT_TRUE(stencil.filled(2, 15));
    EXPECT_EQ(stencil.extent(), (MonsterSpriteSize{.width = 16, .height = 3}));
}

// The height stops at the first empty row; tiles below it count for neither measure.
TEST(MonsterStencil, TheExtentStopsAtTheFirstEmptyRow) {
    const std::array<std::uint8_t, 8> bytes = {0xC0, 0, 0xFF, 0, 0, 0, 0, 0};
    EXPECT_EQ((MonsterStencil{bytes, false}.extent()), (MonsterSpriteSize{.width = 2, .height = 1}));
    const std::array<std::uint8_t, 8> topEmpty = {0, 0xFF, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ((MonsterStencil{topEmpty, false}.extent()), (MonsterSpriteSize{}));
}

TEST(MonsterStencil, ASpanOfTheWrongSizeIsAnEmptyStencil) {
    const std::array<std::uint8_t, 8> eight{};
    const std::array<std::uint8_t, 32> thirtyTwo{};
    EXPECT_FALSE((MonsterStencil{thirtyTwo, false}.valid()));
    EXPECT_FALSE((MonsterStencil{eight, true}.valid()));
    EXPECT_EQ((MonsterStencil{eight, true}.side()), 0);
    EXPECT_EQ((MonsterStencil{eight, true}.extent()), (MonsterSpriteSize{}));
}

TEST(MonsterSprites, AnEmptyViewHasNoStencils) {
    const MonsterSprites sprites;
    EXPECT_FALSE(sprites.valid());
    EXPECT_EQ(sprites.graphicsCount(), 0u);
    EXPECT_FALSE(sprites.stencil(MonsterId::GUARD).valid());
}

// --- the stencils, from the cartridge -----------------------------------------------------------

class MonsterSpritesCartridgeTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = test::ingestVanilla(); }

    static const MonsterSprites& sprites() { return cartridge_.content->monsterSprites; }
};

TEST_F(MonsterSpritesCartridgeTest, TheCartridgeHoldsEveryGraphicAndBothStencilFamilies) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    ASSERT_TRUE(sprites().valid());
    EXPECT_EQ(sprites().graphicsCount(), test::kExpectedMonsterGfxPropCount);
}

// Every graphic, the monsters' and the ones past them, names a stencil the cartridge holds, and no
// stencil has an empty top row or a filled row below an empty one — so every extent is the
// sprite's whole bounding box.
TEST_F(MonsterSpritesCartridgeTest, EveryGraphicHasAStencilFilledFromTheTopWithoutGaps) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    for (std::size_t graphic = 0; graphic < test::kExpectedMonsterGfxPropCount; ++graphic) {
        const MonsterStencil stencil = sprites().stencil(static_cast<MonsterId>(graphic));
        ASSERT_TRUE(stencil.valid()) << "graphic " << graphic;
        const MonsterSpriteSize extent = stencil.extent();
        EXPECT_GE(extent.width, 1) << "graphic " << graphic;
        EXPECT_GE(extent.height, 1) << "graphic " << graphic;
        for (int row = extent.height; row < stencil.side(); ++row) {
            for (int column = 0; column < stencil.side(); ++column) {
                EXPECT_FALSE(stencil.filled(row, column)) << "graphic " << graphic << " row " << row;
            }
        }
    }
}

TEST_F(MonsterSpritesCartridgeTest, HandTracedStencils) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    // Guard: E0 F0 F0 F0 70 E0 00 00 — four tiles at the widest, six rows.
    EXPECT_EQ(sprites().stencil(MonsterId::GUARD).side(), 8);
    EXPECT_EQ(sprites().stencil(MonsterId::GUARD).extent(), (MonsterSpriteSize{.width = 4, .height = 6}));
    // Lobo: C0 FC FC FC 9C 7C 00 00 — six by six.
    EXPECT_EQ(sprites().stencil(MonsterId::LOBO).extent(), (MonsterSpriteSize{.width = 6, .height = 6}));
    // Short Arm, a large stencil: its fourth row 0xFF60 reaches column 10; eight rows.
    EXPECT_EQ(sprites().stencil(MonsterId::SHORT_ARM).side(), 16);
    EXPECT_EQ(sprites().stencil(MonsterId::SHORT_ARM).extent(), (MonsterSpriteSize{.width = 11, .height = 8}));
    // Atma fills its whole 16×16 square.
    EXPECT_EQ(sprites().stencil(MonsterId::ATMA).extent(), (MonsterSpriteSize{.width = 16, .height = 16}));
}

TEST_F(MonsterSpritesCartridgeTest, ASpriteIsCutDownToItsBox) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    // Short Arm stands in a 4×4 box in the final battle.
    EXPECT_EQ(sprites().size(MonsterId::SHORT_ARM, {.width = 4, .height = 4}),
              (MonsterSpriteSize{.width = 4, .height = 4}));
    // In a box bigger than it, a sprite keeps its own size.
    EXPECT_EQ(sprites().size(MonsterId::GUARD, {.width = 8, .height = 8}),
              (MonsterSpriteSize{.width = 4, .height = 6}));
    // Each measure is cut on its own.
    EXPECT_EQ(sprites().size(MonsterId::SHORT_ARM, {.width = 12, .height = 4}),
              (MonsterSpriteSize{.width = 11, .height = 4}));
}

TEST_F(MonsterSpritesCartridgeTest, TheGhostTrainAlwaysStandsSixteenByFourteen) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    // Its stencil is a single tile; the size is set apart from it, and from the box.
    EXPECT_EQ(sprites().stencil(MonsterId::GHOSTTRAIN).extent(), (MonsterSpriteSize{.width = 1, .height = 1}));
    EXPECT_EQ(sprites().size(MonsterId::GHOSTTRAIN, {.width = 8, .height = 8}),
              (MonsterSpriteSize{.width = 16, .height = 14}));
}

// Across every formation, the box cuts a sprite narrower in 27 slots and shorter in 9 — all of
// them bosses built from several monsters.
TEST_F(MonsterSpritesCartridgeTest, TheBoxesCutDownTwentySevenSlotsAcrossAndNineDown) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    int narrower = 0;
    int shorter = 0;
    for (const FormationEntry& entry : formations()) {
        for (std::size_t slot = 0; slot < 6; ++slot) {
            if (entry.record.slotEmpty(slot)) {
                continue;
            }
            const MonsterSpriteSize box = *monsterSpriteBox(entry.record.vramMap(), slot);
            const MonsterSpriteSize extent = sprites().stencil(entry.record.monsterId(slot)).extent();
            narrower += extent.width > box.width ? 1 : 0;
            shorter += extent.height > box.height ? 1 : 0;
        }
    }
    EXPECT_EQ(narrower, 27);
    EXPECT_EQ(shorter, 9);
}

}  // namespace
}  // namespace ostinato
