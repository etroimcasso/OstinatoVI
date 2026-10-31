// Where the battlers stand and the order they are drawn in, and the battle's picture components
// read back from what they return. No cartridge and no renderer: the sheets are carved with the
// engine's own slicer, exactly as an upload would carve them, and the pictures are built by hand.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "retropp/draw_state.h"
#include "retropp/image.h"

#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/battle_background_art.h"
#include "data/battle_gfx_tables.h"
#include "ostinato/monster_vertical_alignment.h"
#include "ostinato/window_tile.h"
#include "render/battle/battle_background.h"
#include "render/battle/battle_placement.h"
#include "render/battle/character_sprite.h"
#include "render/battle/layout.h"
#include "render/battle/menu_window.h"
#include "render/battle/monster_sprite.h"
#include "render/layers.h"
#include "render/screen_border.h"

namespace ostinato::render {
namespace {

using battle::BattlerSlot;
using battle::BattleType;

// --- the party's x --------------------------------------------------------------------------------

// Hand-traced through the original's arithmetic: a lean to the right is scaled and added; a lean to
// the left is complemented, scaled, and complemented again.
TEST(PartyMemberX, HandTracedValues) {
    EXPECT_EQ(partyMemberX(368, 68), 195);   // front, slot 0: lean 112 over 172 → 75 right of center
    EXPECT_EQ(partyMemberX(368, 122), 218);  // front, slot 3: lean 112 over 226 → 98
    EXPECT_EQ(partyMemberX(380, 68), 203);   // front, back row: lean 124 → 83
    EXPECT_EQ(partyMemberX(144, 68), 45);    // back, slot 0: lean -112 → 75 left of center
    EXPECT_EQ(partyMemberX(144, 122), 22);
    EXPECT_EQ(partyMemberX(132, 68), 37);    // back, back row
    EXPECT_EQ(partyMemberX(256, 68), 120);   // pincer: straight down, whatever the y
    EXPECT_EQ(partyMemberX(256, 122), 120);
    EXPECT_EQ(partyMemberX(144, 78), 41);    // side, slot 2
}

// The complement is not a negation: where the scaled lean has a fraction, the two differ by one.
TEST(PartyMemberX, ALeftwardLeanIsComplementedNotNegated) {
    EXPECT_EQ(partyMemberX(144, 0), 74);  // a negation would give 120 - (112 * 104 >> 8) = 75
}

// Only the angle's low nine bits count, as the original masks them.
TEST(PartyMemberX, TheAngleIsNineBits) {
    EXPECT_EQ(partyMemberX(368 + 512, 68), partyMemberX(368, 68));
}

// --- where the party stands -----------------------------------------------------------------------

TEST(PartyMemberStanding, EverySlotOfEveryBattleType) {
    for (std::size_t layout = 0; layout < kBattleLayoutCount; ++layout) {
        const auto type = static_cast<BattleType>(layout);
        for (std::size_t slot = 0; slot < kPartySlotCount; ++slot) {
            const PartySlotPlacement& place = partyPlacement(layout)[slot];
            const PartyStanding standing = partyMemberStanding(type, slot, false, false, false, false);
            EXPECT_EQ(standing.y, place.y) << layout << " " << slot;
            EXPECT_EQ(standing.x, partyMemberX(place.angle, place.y)) << layout << " " << slot;
            EXPECT_EQ(standing.mirrored, place.mirrored) << layout << " " << slot;
            EXPECT_EQ(standing.bottomEdge, place.y + kCharacterBottomEdgePx) << layout << " " << slot;
        }
    }
}

TEST(PartyMemberStanding, TheFrontAttackPutsThePartyOnTheRightFacingLeft) {
    const PartyStanding first = partyMemberStanding(BattleType::Front, 0, false, false, false, false);
    EXPECT_EQ(first, (PartyStanding{.x = 195, .y = 68, .mirrored = false, .bottomEdge = 84}));
    const PartyStanding last = partyMemberStanding(BattleType::Front, 3, false, false, false, false);
    EXPECT_EQ(last, (PartyStanding{.x = 218, .y = 122, .mirrored = false, .bottomEdge = 138}));
}

TEST(PartyMemberStanding, TheBackAttackMirrorsThePartyOnTheLeft) {
    const PartyStanding first = partyMemberStanding(BattleType::Back, 0, false, false, false, false);
    EXPECT_EQ(first, (PartyStanding{.x = 45, .y = 68, .mirrored = true, .bottomEdge = 84}));
}

TEST(PartyMemberStanding, ThePincerStandsThePartyDownTheMiddleAlternatingFacings) {
    for (std::size_t slot = 0; slot < kPartySlotCount; ++slot) {
        const PartyStanding standing = partyMemberStanding(BattleType::Pincer, slot, false, false, false, false);
        EXPECT_EQ(standing.x, 120) << slot;
        EXPECT_EQ(standing.mirrored, slot % 2 == 0) << slot;
    }
}

TEST(PartyMemberStanding, TheSideAttackSplitsThePartyToBothEdges) {
    EXPECT_FALSE(partyMemberStanding(BattleType::Side, 0, false, false, false, false).mirrored);
    EXPECT_FALSE(partyMemberStanding(BattleType::Side, 1, false, false, false, false).mirrored);
    EXPECT_TRUE(partyMemberStanding(BattleType::Side, 2, false, false, false, false).mirrored);
    EXPECT_TRUE(partyMemberStanding(BattleType::Side, 3, false, false, false, false).mirrored);
    EXPECT_EQ(partyMemberStanding(BattleType::Side, 0, false, false, false, false).x, 199);  // lean 112 over 182
    EXPECT_EQ(partyMemberStanding(BattleType::Side, 2, false, false, false, false).x, 41);
}

TEST(PartyMemberStanding, TheBackRowLeansTheLineFurtherFromTheMonsters) {
    const PartyStanding front = partyMemberStanding(BattleType::Front, 0, false, false, false, false);
    const PartyStanding back = partyMemberStanding(BattleType::Front, 0, true, false, false, false);
    EXPECT_EQ(back.x, 203);
    EXPECT_GT(back.x, front.x);
    const PartyStanding backAttack = partyMemberStanding(BattleType::Back, 0, true, false, false, false);
    EXPECT_EQ(backAttack.x, 37);
    // A pincer's back row leans nowhere.
    EXPECT_EQ(partyMemberStanding(BattleType::Pincer, 1, true, false, false, false).x, 120);
}

TEST(PartyMemberStanding, AMemberFightingAgainstThePartyFacesTheOtherWay) {
    EXPECT_TRUE(partyMemberStanding(BattleType::Front, 0, false, true, false, false).mirrored);
    EXPECT_FALSE(partyMemberStanding(BattleType::Back, 0, false, true, false, false).mirrored);
}

TEST(PartyMemberStanding, TheMagitekTrainAndTheColosseumMoveThePartysY) {
    for (std::size_t slot = 0; slot < kPartySlotCount; ++slot) {
        const PartyStanding standing = partyMemberStanding(BattleType::Front, slot, false, false, true, false);
        EXPECT_EQ(standing.y, magitekTrainPartyY(slot)) << slot;
        EXPECT_EQ(standing.x, partyMemberX(partyPlacement(0)[slot].angle, standing.y)) << slot;
    }
    EXPECT_EQ(partyMemberStanding(BattleType::Front, 0, false, false, false, true).y, kColosseumFighterY);
    EXPECT_EQ(partyMemberStanding(BattleType::Front, 1, false, false, false, true).y, 86);
}

// --- where the monsters stand -------------------------------------------------------------------------

constexpr MonsterSpriteSize kSize{.width = 4, .height = 3};

TEST(MonsterStanding, TheFrontAttackLeavesTheFormationsPlaces) {
    const MonsterStanding standing = monsterStanding(BattleType::Front, 40, 56, kSize, 0);
    EXPECT_EQ(standing, (MonsterStanding{.x = 40, .y = 56, .size = kSize, .mirrored = false, .bottomEdge = 72}));
}

TEST(MonsterStanding, TheBackAttackMirrorsAboutTheMiddle) {
    const MonsterStanding standing = monsterStanding(BattleType::Back, 40, 56, kSize, 0);
    EXPECT_EQ(standing.x, 256 - (40 + 32));
    EXPECT_TRUE(standing.mirrored);
    EXPECT_EQ(standing.y, 56);
}

TEST(MonsterStanding, ThePincerMirrorsOnlyTheRightGroup) {
    EXPECT_FALSE(monsterStanding(BattleType::Pincer, 40, 56, kSize, 0).mirrored);
    const MonsterStanding right = monsterStanding(BattleType::Pincer, 80, 56, kSize, 0);
    EXPECT_TRUE(right.mirrored);
    EXPECT_EQ(right.x, 320 - (80 + 32));
}

TEST(MonsterStanding, TheSideAttackShiftsRightAndFacesTheMiddle) {
    const MonsterStanding left = monsterStanding(BattleType::Side, 40, 56, kSize, 0);
    EXPECT_EQ(left.x, 88);
    EXPECT_TRUE(left.mirrored);
    const MonsterStanding right = monsterStanding(BattleType::Side, 120, 56, kSize, 0);
    EXPECT_EQ(right.x, 168);
    EXPECT_FALSE(right.mirrored);
}

TEST(MonsterStanding, TheFeetAreOneTileAboveTheBottomPlusTheOverlap) {
    EXPECT_EQ(monsterStanding(BattleType::Front, 0, 56, kSize, 0).bottomEdge, 56 + 24 - 8);
    EXPECT_EQ(monsterStanding(BattleType::Front, 0, 56, kSize, 5).bottomEdge, 56 + 24 - 8 + 5);
}

TEST(ColosseumMonsterStanding, CenteredWithItsFeetOnTheGroundByAlignment) {
    const MonsterSpriteSize size{.width = 8, .height = 8};
    const MonsterStanding ground = colosseumMonsterStanding(size, MonsterVerticalAlignment::GROUND, 0);
    EXPECT_EQ(ground.x, (128 - 64) / 2 + 24);
    EXPECT_EQ(ground.y, 128 - 64);
    EXPECT_FALSE(ground.mirrored);
    EXPECT_EQ(ground.bottomEdge, 128 - 8);
    EXPECT_EQ(colosseumMonsterStanding(size, MonsterVerticalAlignment::BURIED, 0).y, 128 - 64 - 8);
    EXPECT_EQ(colosseumMonsterStanding(size, MonsterVerticalAlignment::FLOATING, 0).y, 128 - 64 + 8);
    EXPECT_EQ(colosseumMonsterStanding(size, MonsterVerticalAlignment::FLYING, 0).y, 128 - 64 - 40);
    EXPECT_EQ(colosseumMonsterStanding(size, MonsterVerticalAlignment::CEILING, 0).y, 0);
}

// --- the draw order -----------------------------------------------------------------------------------

TEST(BattlerDrawOrder, HighestFeetFirstAbsentSkippedTiesKeepTheEarlierSlotInFront) {
    std::array<std::optional<int>, battle::kBattlerSlots> edges{};
    edges[0] = 84;    // a party member
    edges[2] = 120;   // another, level with monster 1
    edges[4] = 60;    // monster 0
    edges[5] = 120;   // monster 1
    edges[9] = 200;   // monster 5
    const std::vector<BattlerSlot> order = battlerDrawOrder(edges);
    ASSERT_EQ(order.size(), 5u);
    EXPECT_EQ(order[0], BattlerSlot::monster(0));
    EXPECT_EQ(order[1], BattlerSlot::character(0));
    EXPECT_EQ(order[2], BattlerSlot::monster(1));     // level with slot 2, drawn behind it
    EXPECT_EQ(order[3], BattlerSlot::character(2));
    EXPECT_EQ(order[4], BattlerSlot::monster(5));
}

// --- sheets as an upload would carve them ---------------------------------------------------------------

retropp::AtlasManifest carve(int width, int height, retropp::AssetDimensions cell, retropp::ContentKind kind,
                             int count, std::uint16_t id) {
    return retropp::AtlasManifest{
        .atlasId = retropp::AtlasId{id},
        .slots = retropp::sliceLayout(retropp::PixelSize{.width = width, .height = height}, cell, kind,
                                      retropp::ReadOrder::LeftRightThenDown, count),
    };
}

const retropp::PaletteId kColors{3};

// --- MenuWindow --------------------------------------------------------------------------------------

retropp::AtlasManifest wallpaperSheet() {
    return carve(static_cast<int>(kWindowTileCount) * 8, 8, {8, 8}, retropp::ContentKind::Tileset,
                 static_cast<int>(kWindowTileCount), 9);
}

WindowTile tileOf(const Cells& cells, int columns, int row, int column, const retropp::AtlasManifest& sheet) {
    const retropp::TileCell& cell = cells[static_cast<std::size_t>(row * columns + column)];
    for (std::size_t t = 0; t < kWindowTileCount; ++t) {
        if (sheet[t].tile == cell.tile) {
            return static_cast<WindowTile>(t);
        }
    }
    ADD_FAILURE() << "a cell naming no wallpaper tile";
    return WindowTile::WALLPAPER_R0_C0;
}

TEST(MenuWindow, TheMonsterListWindowTwelveByEight) {
    const retropp::AtlasManifest sheet = wallpaperSheet();
    const Cells cells = MenuWindow(12, 8, sheet, kColors);
    ASSERT_EQ(cells.size(), 12u * 8u);
    for (const retropp::TileCell& cell : cells) {
        EXPECT_EQ(cell.atlas, sheet.atlasId);
        EXPECT_EQ(cell.palette, kColors);
        EXPECT_FALSE(cell.flipX);
    }
    // The corners.
    EXPECT_EQ(tileOf(cells, 12, 0, 0, sheet), WindowTile::CORNER_TOP_LEFT);
    EXPECT_EQ(tileOf(cells, 12, 0, 11, sheet), WindowTile::CORNER_TOP_RIGHT);
    EXPECT_EQ(tileOf(cells, 12, 7, 0, sheet), WindowTile::CORNER_BOTTOM_LEFT);
    EXPECT_EQ(tileOf(cells, 12, 7, 11, sheet), WindowTile::CORNER_BOTTOM_RIGHT);
    // The top and bottom edges alternate from the left; the first tile inside the corner is the A tile.
    EXPECT_EQ(tileOf(cells, 12, 0, 1, sheet), WindowTile::EDGE_TOP_A);
    EXPECT_EQ(tileOf(cells, 12, 0, 2, sheet), WindowTile::EDGE_TOP_B);
    EXPECT_EQ(tileOf(cells, 12, 0, 10, sheet), WindowTile::EDGE_TOP_B);
    EXPECT_EQ(tileOf(cells, 12, 7, 1, sheet), WindowTile::EDGE_BOTTOM_A);
    EXPECT_EQ(tileOf(cells, 12, 7, 2, sheet), WindowTile::EDGE_BOTTOM_B);
    // The left and right edges alternate from the top; the first tile below the corner is the even one.
    EXPECT_EQ(tileOf(cells, 12, 1, 0, sheet), WindowTile::EDGE_LEFT_EVEN);
    EXPECT_EQ(tileOf(cells, 12, 2, 0, sheet), WindowTile::EDGE_LEFT_ODD);
    EXPECT_EQ(tileOf(cells, 12, 1, 11, sheet), WindowTile::EDGE_RIGHT_EVEN);
    EXPECT_EQ(tileOf(cells, 12, 6, 11, sheet), WindowTile::EDGE_RIGHT_ODD);
    // Inside, the wallpaper's pattern starts at its first column and row and repeats every four.
    EXPECT_EQ(tileOf(cells, 12, 1, 1, sheet), WindowTile::WALLPAPER_R0_C0);
    EXPECT_EQ(tileOf(cells, 12, 1, 2, sheet), WindowTile::WALLPAPER_R0_C1);
    EXPECT_EQ(tileOf(cells, 12, 1, 4, sheet), WindowTile::WALLPAPER_R0_C3);
    EXPECT_EQ(tileOf(cells, 12, 1, 5, sheet), WindowTile::WALLPAPER_R0_C0);
    EXPECT_EQ(tileOf(cells, 12, 2, 1, sheet), WindowTile::WALLPAPER_R1_C0);
    EXPECT_EQ(tileOf(cells, 12, 4, 1, sheet), WindowTile::WALLPAPER_R3_C0);
    EXPECT_EQ(tileOf(cells, 12, 5, 1, sheet), WindowTile::WALLPAPER_R0_C0);
    EXPECT_EQ(tileOf(cells, 12, 6, 10, sheet), WindowTile::WALLPAPER_R1_C1);
}

TEST(MenuWindow, TheSmallestWindowIsAllBorder) {
    const retropp::AtlasManifest sheet = wallpaperSheet();
    const Cells cells = MenuWindow(2, 2, sheet, kColors);
    ASSERT_EQ(cells.size(), 4u);
    EXPECT_EQ(tileOf(cells, 2, 0, 0, sheet), WindowTile::CORNER_TOP_LEFT);
    EXPECT_EQ(tileOf(cells, 2, 0, 1, sheet), WindowTile::CORNER_TOP_RIGHT);
    EXPECT_EQ(tileOf(cells, 2, 1, 0, sheet), WindowTile::CORNER_BOTTOM_LEFT);
    EXPECT_EQ(tileOf(cells, 2, 1, 1, sheet), WindowTile::CORNER_BOTTOM_RIGHT);
}

// --- BattleBackground --------------------------------------------------------------------------------

// A background of three single blocks, as the first background's is.
BattleBackgroundPicture threeBlockPicture() {
    BattleBackgroundPicture picture;
    picture.tiles.assign(3 * kBackgroundBlockTiles * kBackgroundTileBytes, 0);
    picture.blockFirstTile = {0, kBackgroundBlockTiles, 2 * kBackgroundBlockTiles};
    picture.blockTiles = {kBackgroundBlockTiles, kBackgroundBlockTiles, kBackgroundBlockTiles};
    picture.blockOwnTiles = picture.blockTiles;
    for (BackgroundCell& cell : picture.cells) {
        cell.word = static_cast<std::uint16_t>(kBackgroundFirstPalette << 10);
    }
    return picture;
}

BackgroundSheet backgroundSheet(const BattleBackgroundPicture& picture) {
    const auto tiles = static_cast<int>(picture.tileCount());
    return BackgroundSheet{
        .tiles = carve(8 * (tiles + 1), 8, {8, 8}, retropp::ContentKind::Tileset, tiles + 1, 11),
        .blankTile = static_cast<std::uint16_t>(tiles),
        .palettes = {retropp::PaletteId{5}, retropp::PaletteId{6}, retropp::PaletteId{7}},
    };
}

constexpr std::uint16_t cellWord(unsigned tile, unsigned palette, bool flipX = false, bool flipY = false) {
    return static_cast<std::uint16_t>(tile | palette << 10 | (flipX ? 0x4000 : 0) | (flipY ? 0x8000 : 0));
}

TEST(BattleBackground, TheShownRowsThroughTheCellsPalettesAndFlips) {
    BattleBackgroundPicture picture = threeBlockPicture();
    picture.cells[0].word = cellWord(5, 5);
    picture.cells[1].word = cellWord(130, 6, true);
    picture.cells[2].word = cellWord(900, 7, false, true);
    picture.cells[3].word = cellWord(300, 5);  // a number no block holds
    picture.cells[static_cast<std::size_t>(kBackgroundColumns * kBackgroundRowsShown)].word = cellWord(1, 5);
    const BackgroundSheet sheet = backgroundSheet(picture);
    const Cells cells = BattleBackground(picture, sheet);
    ASSERT_EQ(cells.size(), static_cast<std::size_t>(kBackgroundColumns * kBackgroundRowsShown));
    EXPECT_EQ(cells[0].tile, sheet.tiles[5].tile);
    EXPECT_EQ(cells[0].palette, retropp::PaletteId{5});
    EXPECT_EQ(cells[0].atlas, sheet.tiles.atlasId);
    EXPECT_FALSE(cells[0].flipX);
    EXPECT_EQ(cells[1].tile, sheet.tiles[130].tile);  // block 2's third tile
    EXPECT_EQ(cells[1].palette, retropp::PaletteId{6});
    EXPECT_TRUE(cells[1].flipX);
    EXPECT_EQ(cells[2].tile, sheet.tiles[256 + 4].tile);  // block 3's fifth tile
    EXPECT_EQ(cells[2].palette, retropp::PaletteId{7});
    EXPECT_TRUE(cells[2].flipY);
    EXPECT_EQ(cells[3].tile, sheet.blankTile);
}

// --- ScreenBorder ------------------------------------------------------------------------------------

TEST(ScreenBorder, TheBattleShowsColumns8To247AndLines4To218) {
    const Regions border = ScreenBorder(kBattleTopBlankRows, kBattleBottomBlankRows);
    ASSERT_EQ(border.size(), 1u);
    EXPECT_EQ(border[0].key.value, kScreenBorderKey);
    ASSERT_EQ(border[0].effects.size(), 1u);
    EXPECT_EQ(border[0].effects[0].kind, retropp::ScreenSpaceEffectKind::Transparency);
    EXPECT_EQ(border[0].effects[0].stencil, retropp::StencilMode::TransparentOutside);
    ASSERT_EQ(border[0].shape.points.size(), 4u);
    EXPECT_FLOAT_EQ(border[0].shape.points[0].x, 8.0f);
    EXPECT_FLOAT_EQ(border[0].shape.points[0].y, 4.0f);
    EXPECT_FLOAT_EQ(border[0].shape.points[2].x, 248.0f);
    EXPECT_FLOAT_EQ(border[0].shape.points[2].y, 219.0f);
}

// --- MonsterSprite -----------------------------------------------------------------------------------

TEST(MonsterSprite, OneSpriteTheWholeImageMirroredAsOne) {
    const retropp::AtlasManifest image = carve(32, 24, {32, 24}, retropp::ContentKind::Single, 1, 12);
    const Sprites sprites = MonsterSprite("mon:1", 40, 56, true, image, kColors, 13);
    ASSERT_EQ(sprites.size(), 1u);
    const retropp::Sprite& sprite = sprites[0];
    EXPECT_EQ(sprite.key.value, "mon:1");
    EXPECT_EQ(sprite.x, 40);
    EXPECT_EQ(sprite.y, 56);
    EXPECT_EQ(sprite.z, 13);
    EXPECT_EQ(sprite.size, (retropp::AssetDimensions{32, 24}));
    EXPECT_EQ(sprite.atlas, image.atlasId);
    EXPECT_EQ(sprite.tile, image[0].tile);
    EXPECT_EQ(sprite.palette, kColors);
    EXPECT_TRUE(sprite.flipX);
    EXPECT_FALSE(MonsterSprite("mon:1", 40, 56, false, image, kColors, 13)[0].flipX);
}

// --- CharacterSprite ---------------------------------------------------------------------------------

CharacterSheet characterSheet() {
    return CharacterSheet{
        .standing = carve(16, 32 * 32, {16, 32}, retropp::ContentKind::SpriteSeries, 32, 14),
        .fallen = carve(32, 16, {32, 16}, retropp::ContentKind::Single, 1, 15),
    };
}

TEST(CharacterSprite, AStandingFrameIsPlacedEightAboveThePoint) {
    const CharacterSheet sheet = characterSheet();
    const Sprites sprites = CharacterSprite("chr:0", 195, 68, 17, false, sheet, kColors, 10);
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].x, 195);
    EXPECT_EQ(sprites[0].y, 60);
    EXPECT_EQ(sprites[0].size, (retropp::AssetDimensions{16, 32}));
    EXPECT_EQ(sprites[0].atlas, sheet.standing.atlasId);
    EXPECT_EQ(sprites[0].tile, sheet.standing[17].tile);
    EXPECT_EQ(sprites[0].palette, kColors);
    EXPECT_EQ(sprites[0].z, 10);
    EXPECT_FALSE(sprites[0].flipX);
    EXPECT_TRUE(CharacterSprite("chr:0", 195, 68, 17, true, sheet, kColors, 10)[0].flipX);
}

TEST(CharacterSprite, TheFallenFrameLiesBelowAndToTheLeftFurtherWhenMirrored) {
    const CharacterSheet sheet = characterSheet();
    const Sprites fallen = CharacterSprite("chr:0", 195, 68, 0, false, sheet, kColors, 10);
    ASSERT_EQ(fallen.size(), 1u);
    EXPECT_EQ(fallen[0].x, 191);
    EXPECT_EQ(fallen[0].y, 76);
    EXPECT_EQ(fallen[0].size, (retropp::AssetDimensions{32, 16}));
    EXPECT_EQ(fallen[0].atlas, sheet.fallen.atlasId);
    EXPECT_FALSE(fallen[0].flipX);
    const Sprites mirrored = CharacterSprite("chr:0", 195, 68, 0, true, sheet, kColors, 10);
    EXPECT_EQ(mirrored[0].x, 183);
    EXPECT_EQ(mirrored[0].y, 76);
    EXPECT_TRUE(mirrored[0].flipX);
}

// --- the layout's numbers ----------------------------------------------------------------------------

TEST(BattleLayout, TheScreenSplitsAtLine151AndTheHudTextSitsTwelveApart) {
    EXPECT_EQ(kBattlefieldHeightPx, 151);
    // The background shown from its second pixel row ends on the battlefield's last line.
    EXPECT_EQ(kBackgroundRowsShown * kBattleCellPx - kBattlefieldScrollPx, kBattlefieldHeightPx);
    EXPECT_EQ(kHudTopPx, 151);
    EXPECT_EQ(kHudTopPx + kHudWindowRows * kBattleCellPx, 215);
    EXPECT_EQ(kHudTextFirstLinePx, 163);
    EXPECT_EQ(kHudTextFirstLinePx + (kHudTextLines - 1) * kHudTextLinePitchPx, 199);
    EXPECT_EQ(kMessageTextTopPx, 15);
    EXPECT_EQ(kBattleTopBlankRows, 4);
    EXPECT_EQ(kScreenSize.height - kBattleBottomBlankRows, 219);
}

}  // namespace
}  // namespace ostinato::render
