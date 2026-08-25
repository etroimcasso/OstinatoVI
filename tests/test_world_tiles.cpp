// Full-corpus tests for the world map's terrain, songs, curves and Magitek
// train tile geometry. Every table is compared entry-by-entry against its
// generated fixture (the ROM-assembled values), independent of the typed rows,
// so any decode or re-emit drift fails loudly. On top of that: each named
// tile-property bit is traced to a value its consumer reads, the background
// selector is cross-checked against the shipped world battle-background table,
// the two-reads-disagree behaviour the original ships is pinned, and the train
// tile offsets are checked against the sequence the ROM's own precomputed table
// holds.
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "data/encounters.h"
#include "data/world_tiles.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/song_id.h"
#include "ostinato/world_map_id.h"
#include "ostinato/world_tile_properties.h"

#include "fixtures/world_tiles_expected.h"

namespace {

using namespace ostinato;

// --- terrain ------------------------------------------------------------------

TEST(WorldTiles, TilePropertyTablesMatchRom) {
    const WorldMapId worlds[] = {WorldMapId::WORLD_OF_BALANCE,
                                 WorldMapId::WORLD_OF_RUIN};
    for (std::size_t table = 0; table < 2; ++table) {
        const auto rows = worldTilePropertyTable(worlds[table]);
        ASSERT_EQ(rows.size(), kWorldTilePropertyCount);
        for (std::size_t tile = 0; tile < rows.size(); ++tile) {
            EXPECT_EQ(rows[tile].index, tile)
                << "table " << table << " row " << tile;
            EXPECT_EQ(rows[tile].properties.raw(),
                      test::kExpectedWorldTileProps[table].words[tile])
                << "table " << table << " tile " << tile;
        }
    }
}

TEST(WorldTiles, TilePropertyLookupSelectsTheWorldsTable) {
    for (std::size_t tile = 0; tile < kWorldTilePropertyCount; ++tile) {
        const auto index = static_cast<std::uint8_t>(tile);
        EXPECT_EQ(worldTileProperties(WorldMapId::WORLD_OF_BALANCE, index).raw(),
                  test::kExpectedWorldTileProps[0].words[tile]);
        EXPECT_EQ(worldTileProperties(WorldMapId::WORLD_OF_RUIN, index).raw(),
                  test::kExpectedWorldTileProps[1].words[tile]);
    }
}

// Each named bit, traced to a tile the consumer actually reads it on.
TEST(WorldTiles, TilePropertyBitsDecodeTheirTracedValues) {
    // world/tile_prop.asm:5 — tile 2 of the World of Balance is $0044: open
    // ground where battles happen, nothing else named.
    const auto plain = WorldTileProperties::of(0x0044);
    EXPECT_TRUE(plain.battlesEnabled());
    EXPECT_FALSE(plain.impassableOnFoot());
    EXPECT_FALSE(plain.isForest());
    EXPECT_FALSE(plain.airshipCannotLand());
    EXPECT_FALSE(plain.isVeldt());
    EXPECT_EQ(plain.battleBackgroundSelector(), 0);

    // $0366 is the forest terrain: the sprite is drawn translucent, the
    // airship cannot land, battles still happen.
    const auto forest = WorldTileProperties::of(0x0366);
    EXPECT_TRUE(forest.isForest());
    EXPECT_TRUE(forest.airshipCannotLand());
    EXPECT_TRUE(forest.battlesEnabled());
    EXPECT_EQ(forest.battleBackgroundSelector(), 3);

    // $001b is terrain the party cannot walk onto and the airship cannot land
    // on, and it carries no battles.
    const auto blocked = WorldTileProperties::of(0x001B);
    EXPECT_TRUE(blocked.impassableOnFoot());
    EXPECT_TRUE(blocked.airshipCannotLand());
    EXPECT_FALSE(blocked.battlesEnabled());

    // $2644 is a Veldt tile; $4515 and $8019 are the Phoenix Cave and Kefka's
    // Tower entrances in the World of Ruin table (world/tile_prop.asm:64, :55).
    EXPECT_TRUE(WorldTileProperties::of(0x2644).isVeldt());
    EXPECT_TRUE(WorldTileProperties::of(0x4515).isPhoenixCaveEntrance());
    EXPECT_TRUE(WorldTileProperties::of(0x8019).isKefkasTowerEntrance());
    EXPECT_TRUE(WorldTileProperties::of(0x4515).impassableOnFoot());
    EXPECT_TRUE(WorldTileProperties::of(0x8019).impassableOnFoot());
    EXPECT_FALSE(WorldTileProperties::of(0x2644).isPhoenixCaveEntrance());
    EXPECT_FALSE(WorldTileProperties::of(0x2644).isKefkasTowerEntrance());
}

// The selector is the high three bits, and it indexes the battle-background
// table the encounter layer already ships. A Veldt tile must select the Veldt
// background — two independent facts about the same word agreeing.
TEST(WorldTiles, BattleBackgroundSelectorIndexesTheShippedTable) {
    const auto veldt = WorldTileProperties::of(0x2644);
    ASSERT_EQ(veldt.battleBackgroundSelector(), 6);
    EXPECT_EQ(kWorldBattleBackgrounds[0][veldt.battleBackgroundSelector()],
              BattleBackgroundId::VELDT);
    EXPECT_TRUE(veldt.isVeldt());

    // Every tile's selector stays inside the eight slots the table holds.
    for (std::size_t table = 0; table < 2; ++table) {
        for (const auto word : test::kExpectedWorldTileProps[table].words) {
            EXPECT_LT(WorldTileProperties::of(word).battleBackgroundSelector(),
                      kWorldBattleBackgrounds[table].size());
        }
    }
}

// The original reads the terrain table two ways and they disagree:
// GetWorldTileProp adds the map offset, MovePlayer's own read does not, so the
// second always lands in the World of Balance table. The port keeps both.
TEST(WorldTiles, MovePlayerReadAlwaysUsesTheWorldOfBalanceTable) {
    for (std::size_t tile = 0; tile < kWorldTilePropertyCount; ++tile) {
        const auto index = static_cast<std::uint8_t>(tile);
        EXPECT_EQ(worldOfBalanceTileProperties(index).raw(),
                  worldTileProperties(WorldMapId::WORLD_OF_BALANCE, index).raw());
    }

    // The two tables genuinely differ, so the quirk is observable rather than
    // a distinction without a difference.
    bool differs = false;
    for (std::size_t tile = 0; tile < kWorldTilePropertyCount; ++tile) {
        if (test::kExpectedWorldTileProps[0].words[tile]
            != test::kExpectedWorldTileProps[1].words[tile]) {
            differs = true;
            const auto index = static_cast<std::uint8_t>(tile);
            EXPECT_NE(worldOfBalanceTileProperties(index).raw(),
                      worldTileProperties(WorldMapId::WORLD_OF_RUIN, index).raw())
                << "tile " << tile;
            break;
        }
    }
    EXPECT_TRUE(differs) << "the two terrain tables must not be identical";
}

// --- songs --------------------------------------------------------------------

TEST(WorldTiles, SongTablesMatchRom) {
    struct Case {
        const char* name;
        std::span<const WorldSongEntry> rows;
        const std::uint8_t* expected;
        std::size_t count;
    };
    const Case cases[] = {
        {"airship", airshipSongs(), test::kExpectedAirshipSong.data(),
         test::kExpectedAirshipSong.size()},
        {"chocobo", chocoboSongs(), test::kExpectedChocoboSong.data(),
         test::kExpectedChocoboSong.size()},
        {"world", worldSongs(), test::kExpectedWorldSong.data(),
         test::kExpectedWorldSong.size()},
        {"train", trainSongs(), test::kExpectedTrainSong.data(),
         test::kExpectedTrainSong.size()},
        {"serpent trench", serpentTrenchSongs(),
         test::kExpectedSerpentTrenchSong.data(),
         test::kExpectedSerpentTrenchSong.size()},
    };
    for (const auto& c : cases) {
        ASSERT_EQ(c.rows.size(), c.count) << c.name;
        for (std::size_t i = 0; i < c.rows.size(); ++i) {
            EXPECT_EQ(c.rows[i].index, i) << c.name << " row " << i;
            EXPECT_EQ(static_cast<std::uint8_t>(c.rows[i].song), c.expected[i])
                << c.name << " row " << i;
        }
    }
}

TEST(WorldTiles, SongAccessorsNameTheirTracedTracks) {
    // world/init.asm:22-59 — the tables' own comments name these.
    EXPECT_EQ(airshipSong(0), SongId::BLACKJACK);
    EXPECT_EQ(airshipSong(2), SongId::SEARCHING_FOR_FRIENDS);
    EXPECT_EQ(chocoboSong(0), SongId::TECHNO_DE_CHOCOBO);
    EXPECT_EQ(worldSong(0), SongId::TERRA);
    EXPECT_EQ(worldSong(2), SongId::DARK_WORLD);
    EXPECT_EQ(trainSong(0), SongId::SAVE_THEM);
    EXPECT_EQ(serpentTrenchSong(0), SongId::SERPENT_TRENCH);
}

// --- curves -------------------------------------------------------------------

void expectCurveMatches(std::span<const WorldCurveEntry> rows,
                        const std::uint8_t* expected, std::size_t count,
                        const char* name) {
    ASSERT_EQ(rows.size(), count) << name;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].index, i) << name << " row " << i;
        EXPECT_EQ(rows[i].value, expected[i]) << name << " row " << i;
    }
}

TEST(WorldTiles, CurvesMatchRom) {
    expectCurveMatches(trainBattleMosaicCurve(),
                       test::kExpectedTrainBattleMosaic.data(),
                       test::kExpectedTrainBattleMosaic.size(),
                       "train battle mosaic");
    expectCurveMatches(airshipDirectionAnimationOffsets(),
                       test::kExpectedAirshipDirAnimOffset.data(),
                       test::kExpectedAirshipDirAnimOffset.size(),
                       "airship direction anim offsets");
    expectCurveMatches(characterMoveFrames(),
                       test::kExpectedCharMoveFrame.data(),
                       test::kExpectedCharMoveFrame.size(),
                       "character move frames");
    expectCurveMatches(groundedAirshipSizeCurve(),
                       test::kExpectedGroundedAirshipSize.data(),
                       test::kExpectedGroundedAirshipSize.size(),
                       "grounded airship size");
}

TEST(WorldTiles, HorizontalFlipTablesMatchRom) {
    const auto top = characterTopHalfFlips();
    const auto bottom = characterBottomHalfFlips();
    ASSERT_EQ(top.size(), test::kExpectedCharTopHflip.size());
    ASSERT_EQ(bottom.size(), test::kExpectedCharBtmHflip.size());
    for (std::size_t i = 0; i < top.size(); ++i) {
        EXPECT_EQ(top[i].index, i);
        EXPECT_EQ(bottom[i].index, i);
        EXPECT_EQ(top[i].flipped, test::kExpectedCharTopHflip[i] != 0)
            << "top row " << i;
        EXPECT_EQ(bottom[i].flipped, test::kExpectedCharBtmHflip[i] != 0)
            << "bottom row " << i;
        // The source bytes really are boolean, which is why the port models
        // them as bool rather than as an opaque byte.
        EXPECT_LE(test::kExpectedCharTopHflip[i], 1u) << "top row " << i;
        EXPECT_LE(test::kExpectedCharBtmHflip[i], 1u) << "bottom row " << i;
    }
}

TEST(WorldTiles, BattleZoomStepsSplitTheRomWord) {
    const auto steps = battleZoomSteps();
    ASSERT_EQ(steps.size(), test::kExpectedBattleZoom.size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const std::uint16_t word = test::kExpectedBattleZoom[i];
        EXPECT_EQ(steps[i].index, i);
        // The consumer reads the low byte as the zoom level and the byte after
        // it as the screen brightness (world/move.asm:1414-1417).
        EXPECT_EQ(steps[i].zoomLevel, word & 0xFF) << "step " << i;
        EXPECT_EQ(steps[i].screenBrightness, (word >> 8) & 0xFF) << "step " << i;
    }
    // The transition starts at full brightness and ends dimmed.
    EXPECT_EQ(steps.front().screenBrightness, 0x0F);
    EXPECT_EQ(steps.back().screenBrightness, 0x01);
    EXPECT_EQ(steps.back().zoomLevel, 0x00);
}

TEST(WorldTiles, AirshipDirectionOffsetsAreFourRowsOfFour) {
    const auto rows = airshipDirectionAnimationOffsets();
    ASSERT_EQ(rows.size(), 16u);
    // Column 0 is unused on every row, and the fourth row is unused entirely
    // (world/sprite.asm:799-804).
    for (std::size_t row = 0; row < 4; ++row) {
        EXPECT_EQ(rows[row * 4].value, 0) << "row " << row << " column 0";
    }
    for (std::size_t column = 0; column < 4; ++column) {
        EXPECT_EQ(rows[3 * 4 + column].value, 0) << "unused row column "
                                                 << column;
    }
    // Not turning: straight, up, down are six frames apart.
    EXPECT_EQ(rows[1].value, 0x01);
    EXPECT_EQ(rows[2].value, 0x07);
    EXPECT_EQ(rows[3].value, 0x0D);
}

// --- Magitek train tile geometry ----------------------------------------------

TEST(WorldTiles, TrainLayerGeometryMatchesRom) {
    const auto pixels = trainLayerPixelCounts();
    const auto sides = trainLayerTileSides();
    ASSERT_EQ(pixels.size(), kTrainLayerCount);
    ASSERT_EQ(sides.size(), kTrainLayerCount);
    for (std::size_t i = 0; i < kTrainLayerCount; ++i) {
        EXPECT_EQ(pixels[i].index, i);
        EXPECT_EQ(sides[i].index, i);
        EXPECT_EQ(pixels[i].value, test::kExpectedTrainLayerPixelCount[i])
            << "layer " << i;
        EXPECT_EQ(sides[i].value, test::kExpectedTrainLayerTileSide[i])
            << "layer " << i;
        // A layer's pixel count is its tile side squared, which is what makes
        // the offsets below derivable.
        EXPECT_EQ(pixels[i].value, sides[i].value * sides[i].value)
            << "layer " << i;
    }
}

// The ROM ships these offsets as a baked table; the port computes them. This
// checks the computation against the sequence that table holds.
TEST(WorldTiles, TrainTileOffsetsMatchTheRomsBakedTable) {
    ASSERT_EQ(test::kExpectedTrainTileOffsets.size(), kTrainTileOffsetCount);
    for (std::size_t step = 0; step < kTrainTileOffsetCount; ++step) {
        EXPECT_EQ(trainTileOffset(step), test::kExpectedTrainTileOffsets[step])
            << "step " << step;
    }
    EXPECT_EQ(trainTileOffset(0), kTrainTileBufferBase);
    // 29 tiles of 12 non-zero layers each.
    EXPECT_EQ(kTrainTileOffsetCount, kTrainTileCount * 12);
}

}  // namespace
