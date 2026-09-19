// Full-corpus tests for the battle background property table. All 56 records
// are compared against the generated fixture (the ROM-assembled values) one
// whole record at a time, independent of the typed rows, so any decode or
// re-emit drift fails loudly. On top of that: both packed slots are decoded against values traced
// from their loaders, the $FF-versus-0 distinction the loaders draw is pinned,
// the named builders are round-tripped, and the table's correspondence with the
// BattleBackgroundId index space is asserted end to end.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/battle_backgrounds.h"
#include "ostinato/battle_background_graphics_id.h"
#include "ostinato/battle_background_graphics_slot.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_background_palette_id.h"
#include "ostinato/battle_background_palette_slot.h"
#include "ostinato/battle_background_tilemap_id.h"

#include "fixtures/battle_bg_prop_expected.h"

namespace {

using namespace ostinato;

// --- full corpus ---------------------------------------------------------------

// memcmp per packed record catches field-order, padding, decomposition and
// flag-folding drift in one assertion; the id check catches a row landing at
// the wrong position.
TEST(BattleBackgrounds, EveryRecordMatchesRom) {
    const auto table = battleBackgroundTable();
    ASSERT_EQ(table.size(), kBattleBackgroundCount);
    ASSERT_EQ(table.size(), test::kExpectedBattleBackgroundEntries.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = test::kExpectedBattleBackgroundEntries[i];
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i) << "row " << i;
        EXPECT_EQ(expected.id, i) << "fixture row " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected.record,
                              sizeof(BattleBackgroundProperties)), 0)
            << "record " << i;
    }
}

TEST(BattleBackgrounds, AccessorReturnsTheRowAtTheIdsPosition) {
    const auto table = battleBackgroundTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto id = static_cast<BattleBackgroundId>(i);
        EXPECT_EQ(std::memcmp(&battleBackgroundProperties(id),
                              &table[i].record,
                              sizeof(BattleBackgroundProperties)), 0)
            << "accessor row " << i;
    }
}

// --- traced records -------------------------------------------------------------

// Record 0 (gfx/battle_bg.asm:55-61): three plain graphics blocks, the same
// tilemap in both slots, a plain palette.
TEST(BattleBackgrounds, FieldWobIsThreePlainBlocks) {
    const auto& record = battleBackgroundProperties(BattleBackgroundId::FIELD_WOB);
    EXPECT_EQ(record.graphics1.id(), BattleBackgroundGraphicsId::FIELD_1);
    EXPECT_FALSE(record.graphics1.isDoubleWidth());
    EXPECT_FALSE(record.graphics1.isEmpty());
    EXPECT_EQ(record.graphics2, BattleBackgroundGraphicsId::FIELD_2);
    EXPECT_EQ(record.graphics3, BattleBackgroundGraphicsId::FIELD_3);
    EXPECT_EQ(record.tilemap1, BattleBackgroundTilemapId::FIELD_WOB);
    EXPECT_EQ(record.tilemap2, BattleBackgroundTilemapId::FIELD_WOB);
    EXPECT_EQ(record.palette.id(), BattleBackgroundPaletteId::FIELD_WOB);
    EXPECT_FALSE(record.palette.hasWavyEffect());
}

// Record 1 (:65-71): the first block is 128 tiles, so the second is the $FF
// sentinel and is not loaded at all.
TEST(BattleBackgrounds, ForestWorUsesADoubleWidthBlock) {
    const auto& record = battleBackgroundProperties(BattleBackgroundId::FOREST_WOR);
    EXPECT_EQ(record.graphics1.id(), BattleBackgroundGraphicsId::FOREST_2);
    EXPECT_TRUE(record.graphics1.isDoubleWidth());
    EXPECT_EQ(record.graphics2, BattleBackgroundGraphicsId::NONE);
    EXPECT_EQ(record.graphics3, BattleBackgroundGraphicsId::FOREST_1);
}

// Record 2 (:75-81): the palette byte carries the wavy-effect flag.
TEST(BattleBackgrounds, DesertWobHasAWavyPalette) {
    const auto& record = battleBackgroundProperties(BattleBackgroundId::DESERT_WOB);
    EXPECT_EQ(record.palette.id(), BattleBackgroundPaletteId::DESERT_WOB);
    EXPECT_TRUE(record.palette.hasWavyEffect());
}

// Record 31 (:365-371) writes a bare 0 for its second and third blocks where
// every other record writes a symbol. 0 is not the $FF sentinel, and
// TfrBattleBGGfx (btlgfx_main.asm:4002-4004) skips only $FF — so these slots
// really do load block 0. Pinned in both directions so neither the value nor
// the "not empty" reading can drift.
TEST(BattleBackgrounds, WaterfallLoadsBlockZeroRatherThanNothing) {
    const auto& record = battleBackgroundProperties(BattleBackgroundId::WATERFALL);
    EXPECT_EQ(record.graphics1.id(), BattleBackgroundGraphicsId::WATERFALL);
    EXPECT_EQ(record.graphics2, BattleBackgroundGraphicsId::TOWN_EXT_1);
    EXPECT_EQ(record.graphics3, BattleBackgroundGraphicsId::TOWN_EXT_1);
    EXPECT_EQ(static_cast<std::uint8_t>(record.graphics2), 0);
    EXPECT_NE(record.graphics2, BattleBackgroundGraphicsId::NONE);
}

// --- slot decodes ----------------------------------------------------------------

// The $FF sentinel is a whole-byte value, not a bit pattern: a slot holding it
// reads empty and reports NONE, while a slot holding 0 does neither.
TEST(BattleBackgrounds, GraphicsSlotSeparatesEmptyFromBlockZero) {
    const BattleBackgroundGraphicsSlot empty{0xFF};
    EXPECT_TRUE(empty.isEmpty());
    EXPECT_EQ(empty.id(), BattleBackgroundGraphicsId::NONE);

    const BattleBackgroundGraphicsSlot zero{0x00};
    EXPECT_FALSE(zero.isEmpty());
    EXPECT_EQ(zero.id(), BattleBackgroundGraphicsId::TOWN_EXT_1);
    EXPECT_FALSE(zero.isDoubleWidth());
}

TEST(BattleBackgrounds, GraphicsSlotBuildersRoundTrip) {
    const auto single =
        BattleBackgroundGraphicsSlot::single(BattleBackgroundGraphicsId::WATERFALL);
    EXPECT_EQ(single.id(), BattleBackgroundGraphicsId::WATERFALL);
    EXPECT_FALSE(single.isDoubleWidth());

    const auto wide =
        BattleBackgroundGraphicsSlot::doubleWidth(BattleBackgroundGraphicsId::FOREST_2);
    EXPECT_EQ(wide.id(), BattleBackgroundGraphicsId::FOREST_2);
    EXPECT_TRUE(wide.isDoubleWidth());
    EXPECT_EQ(wide.bits,
              static_cast<std::uint8_t>(
                  static_cast<std::uint8_t>(BattleBackgroundGraphicsId::FOREST_2)
                  | 0x80));
}

TEST(BattleBackgrounds, PaletteSlotBuildersRoundTrip) {
    const auto plain =
        BattleBackgroundPaletteSlot::plain(BattleBackgroundPaletteId::FIELD_WOB);
    EXPECT_EQ(plain.id(), BattleBackgroundPaletteId::FIELD_WOB);
    EXPECT_FALSE(plain.hasWavyEffect());

    const auto wavy =
        BattleBackgroundPaletteSlot::wavy(BattleBackgroundPaletteId::DESERT_WOB);
    EXPECT_EQ(wavy.id(), BattleBackgroundPaletteId::DESERT_WOB);
    EXPECT_TRUE(wavy.hasWavyEffect());
}

// --- corpus-wide invariants --------------------------------------------------------

// Bit 7 of a record's first graphics byte is a flag, so a block's id must
// still fit the 7-bit space the loader masks it down to. An empty slot is
// exempt: it reports NONE ($FF) rather than the 127 that masking $FF would
// produce, and 127 names no block.
TEST(BattleBackgrounds, EveryPresentGraphicsIdFitsTheSevenBitMask) {
    for (const auto& entry : battleBackgroundTable()) {
        if (entry.record.graphics1.isEmpty()) {
            continue;
        }
        EXPECT_LE(static_cast<std::uint8_t>(entry.record.graphics1.id()), 0x7F)
            << "record " << static_cast<int>(entry.id);
    }
}

// Three backgrounds have no first graphics block: their VRAM $1000 slot is the
// $FF sentinel and only the later blocks load. Pinned by name and by count so
// neither a lost record nor a newly-empty one passes unnoticed.
TEST(BattleBackgrounds, ExactlyThreeRecordsHaveNoFirstGraphicsBlock) {
    int empty = 0;
    for (const auto& entry : battleBackgroundTable()) {
        if (entry.record.graphics1.isEmpty()) {
            ++empty;
        }
    }
    EXPECT_EQ(empty, 3);

    for (const auto id : {BattleBackgroundId::NARSHE_EXT,
                          BattleBackgroundId::TOWN_EXT,
                          BattleBackgroundId::VILLAGE_EXT}) {
        EXPECT_TRUE(battleBackgroundProperties(id).graphics1.isEmpty())
            << "record " << static_cast<int>(id);
    }

    // VILLAGE_EXT (gfx/battle_bg.asm:356-361) still loads both later blocks —
    // an empty first slot does not mean an empty record.
    const auto& village =
        battleBackgroundProperties(BattleBackgroundId::VILLAGE_EXT);
    EXPECT_EQ(village.graphics2, BattleBackgroundGraphicsId::TOWN_EXT_1);
    EXPECT_EQ(village.graphics3, BattleBackgroundGraphicsId::TOWN_EXT_2);
}

// Only the first graphics block and the palette carry a flag. The second and
// third blocks are plain ids, so no record may set bit 7 on them — if one did,
// the loader would index a block that does not exist.
TEST(BattleBackgrounds, OnlyTheFlaggedBytesUseBitSeven) {
    for (const auto& entry : battleBackgroundTable()) {
        const auto g2 = static_cast<std::uint8_t>(entry.record.graphics2);
        const auto g3 = static_cast<std::uint8_t>(entry.record.graphics3);
        if (g2 != 0xFF) {
            EXPECT_EQ(g2 & 0x80, 0) << "record " << static_cast<int>(entry.id);
        }
        if (g3 != 0xFF) {
            EXPECT_EQ(g3 & 0x80, 0) << "record " << static_cast<int>(entry.id);
        }
    }
}

}  // namespace
