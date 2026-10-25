// The dialog box's data and its rendering components, read back from what they return, checked
// against docs/contracts/dialog_window.md.
//
// The formation and the shading tables are compared to the raw cartridge bytes in the fixture.
// Each component is called with a window state built by driving the window logic, and its cells,
// regions or transform are read back. No renderer is involved: the wallpaper sheet is carved with
// the engine's own slicer, exactly as an upload would carve it.
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "retropp/draw_state.h"
#include "retropp/image.h"
#include "retropp/transform.h"

#include "fixtures/dialog_window_expected.h"
#include "ostinato/window_tile.h"
#include "render/layers.h"
#include "render/tile_layer.h"
#include "render/window/dialog_window.h"
#include "render/window/window_clip.h"
#include "render/window/window_shading.h"
#include "render/window/window_squash.h"
#include "text/dialog_window.h"
#include "text/layout.h"
#include "text/window_layout.h"

namespace ostinato::render {
namespace {

using text::DialogWindowState;
using text::WindowPhase;
using text::WindowPlacement;

// --- the data, against the cartridge's bytes --------------------------------------------------------

constexpr std::uint8_t kFirstWindowTileByte = 0xE0;

TEST(WindowFormation, EveryTileMatchesTheCartridge) {
    ASSERT_EQ(text::kDialogWindowFormation.size(), test::kExpectedDialogWindowFormation.size());
    for (std::size_t r = 0; r < text::kDialogWindowFormation.size(); ++r) {
        const auto& row = text::kDialogWindowFormation[r];
        const auto& expected = test::kExpectedDialogWindowFormation[r];
        EXPECT_EQ(row.row, expected.row);
        for (std::size_t c = 0; c < row.tiles.size(); ++c) {
            EXPECT_EQ(static_cast<int>(row.tiles[c]) + kFirstWindowTileByte, expected.tiles[c])
                << "row " << r << " column " << c;
        }
    }
}

TEST(WindowFormation, TheHiddenColumnsRepeatTheirNeighbors) {
    for (const auto& row : text::kDialogWindowFormation) {
        EXPECT_EQ(row.tiles[0], row.tiles[1]) << "row " << int{row.row};
        EXPECT_EQ(row.tiles[31], row.tiles[30]) << "row " << int{row.row};
    }
}

template <std::size_t N, std::size_t M>
void expectShadingMatches(const std::array<text::WindowShadingRow, N>& table,
                          const std::array<test::ExpectedShadingRow, M>& expected) {
    ASSERT_EQ(N, M);
    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_EQ(table[i].row, expected[i].row);
        EXPECT_EQ(table[i].level, expected[i].value & 0x1F) << "row " << i;
        EXPECT_EQ(expected[i].value & 0xE0, 0xE0) << "row " << i;
    }
}

TEST(WindowShadingTables, EveryRowMatchesTheCartridge) {
    expectShadingMatches(text::kWindowShading72, test::kExpectedWindowShading72);
    expectShadingMatches(text::kWindowShading56, test::kExpectedWindowShading56);
    expectShadingMatches(text::kWindowShading40, test::kExpectedWindowShading40);
    expectShadingMatches(text::kWindowShading24, test::kExpectedWindowShading24);
    expectShadingMatches(text::kWindowShading8, test::kExpectedWindowShading8);
}

// The open table: 7, 7, 7, five rows each of 6 down to 0, five each of 1 up to 6, then 7 four times.
TEST(WindowShadingTables, TheOpenTableRunsSevenToZeroToSeven) {
    std::vector<int> expected{7, 7, 7};
    for (int level = 6; level >= 0; --level) {
        expected.insert(expected.end(), 5, level);
    }
    for (int level = 1; level <= 6; ++level) {
        expected.insert(expected.end(), 5, level);
    }
    expected.insert(expected.end(), 4, 7);
    ASSERT_EQ(expected.size(), text::kWindowShading72.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(text::kWindowShading72[i].level, expected[i]) << "row " << i;
    }
}

// Each table changes from lightening to darkening at its middle row, where its level is 0.
TEST(WindowShadingTables, TheSignChangesAtTheMiddleWhereTheLevelIsZero) {
    for (const int height : {72, 56, 40, 24, 8}) {
        const auto table = text::windowShading(height);
        ASSERT_EQ(static_cast<int>(table.size()), height);
        EXPECT_TRUE(text::shadingLightens(height / 2 - 1, height)) << height;
        EXPECT_FALSE(text::shadingLightens(height / 2, height)) << height;
        EXPECT_EQ(table[static_cast<std::size_t>(height / 2)].level, 0) << height;
    }
    EXPECT_TRUE(text::windowShading(16).empty());
}

// --- driving the window ------------------------------------------------------------------------------

constexpr int kEnglishFirstColumnX = text::kWindowFirstVisibleColumn * text::kWindowCellPx;

DialogWindowState opening(WindowPlacement placement = WindowPlacement::Top, bool textOnly = false) {
    DialogWindowState state;
    text::initDialogWindow(state);
    text::openDialogWindow(state, placement, textOnly);
    return state;
}

DialogWindowState afterTicks(DialogWindowState state, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        text::tickDialogWindow(state);
    }
    return state;
}

DialogWindowState open(WindowPlacement placement = WindowPlacement::Top) {
    return afterTicks(opening(placement), text::kWindowOpenFrames);
}

DialogWindowState closingAfter(int ticks) {
    DialogWindowState state = open();
    text::closeDialogWindow(state);
    return afterTicks(state, ticks);
}

DialogWindowState title() {
    DialogWindowState state;
    text::initDialogWindow(state);
    text::openMapTitleWindow(state);
    return state;
}

retropp::Point boxTopLeft(const DialogWindowState& state) {
    return {.x = static_cast<float>(kEnglishFirstColumnX),
            .y = static_cast<float>(text::boxTopRow(state) * text::kWindowCellPx)};
}

// The wallpaper sheet as an upload would carve it.
retropp::AtlasManifest windowSheet() {
    return retropp::AtlasManifest{
        .atlasId = retropp::AtlasId{9},
        .slots = retropp::sliceLayout(
            retropp::PixelSize{.width = static_cast<int>(kWindowTileCount) * 8, .height = 8},
            retropp::AssetDimensions{8, 8}, retropp::ContentKind::Tileset,
            retropp::ReadOrder::LeftRightThenDown, static_cast<int>(kWindowTileCount)),
    };
}

const retropp::PaletteId kWindowPalette{6};

// --- DialogWindow ------------------------------------------------------------------------------------

TEST(DialogWindow, NothingWhileClosedOrBeforeTheFirstStepOrTextOnly) {
    DialogWindowState closed;
    text::initDialogWindow(closed);
    EXPECT_TRUE(DialogWindow(closed, windowSheet(), kWindowPalette).empty());
    EXPECT_TRUE(DialogWindow(opening(), windowSheet(), kWindowPalette).empty());
    const DialogWindowState textOnly = afterTicks(opening(WindowPlacement::Top, true), 5);
    EXPECT_TRUE(DialogWindow(textOnly, windowSheet(), kWindowPalette).empty());
}

// The whole box whenever any of it shows: 30 visible columns by 9 rows, opening, open or closing.
TEST(DialogWindow, TheWholeBoxThirtyColumnsByNineRows) {
    for (const DialogWindowState& state : {afterTicks(opening(), 1), open(), closingAfter(3)}) {
        EXPECT_EQ(DialogWindow(state, windowSheet(), kWindowPalette).size(), 30u * 9u);
    }
}

TEST(DialogWindow, CellsAreTheFormationsVisibleColumnsThroughTheWindowPalette) {
    const retropp::AtlasManifest sheet = windowSheet();
    const Cells cells = DialogWindow(open(), sheet, kWindowPalette);
    for (int r = 0; r < text::kDialogWindowRows; ++r) {
        for (int c = 0; c < text::kWindowVisibleColumns; ++c) {
            const retropp::TileCell& cell = cells[static_cast<std::size_t>(r * 30 + c)];
            const WindowTile tile = text::kDialogWindowFormation[static_cast<std::size_t>(r)]
                                        .tiles[static_cast<std::size_t>(c + 1)];
            EXPECT_EQ(cell.tile, sheet[static_cast<std::size_t>(tile)].tile) << r << "," << c;
            EXPECT_EQ(cell.atlas, sheet.atlasId);
            EXPECT_EQ(cell.palette, kWindowPalette);
        }
    }
    EXPECT_EQ(cells.front().tile, sheet[static_cast<std::size_t>(WindowTile::CORNER_TOP_LEFT)].tile);
    EXPECT_EQ(cells[29].tile, sheet[static_cast<std::size_t>(WindowTile::CORNER_TOP_RIGHT)].tile);
    EXPECT_EQ(cells.back().tile,
              sheet[static_cast<std::size_t>(WindowTile::CORNER_BOTTOM_RIGHT)].tile);
}

// The title box: top border, two wallpaper rows, bottom border.
TEST(DialogWindow, TheTitleBoxIsFourRows) {
    const retropp::AtlasManifest sheet = windowSheet();
    const Cells cells = DialogWindow(title(), sheet, kWindowPalette);
    ASSERT_EQ(cells.size(), 30u * 4u);
    EXPECT_EQ(cells[0].tile, sheet[static_cast<std::size_t>(WindowTile::CORNER_TOP_LEFT)].tile);
    EXPECT_EQ(cells[30].tile, sheet[static_cast<std::size_t>(WindowTile::EDGE_LEFT_EVEN)].tile);
    EXPECT_EQ(cells[60].tile, sheet[static_cast<std::size_t>(WindowTile::EDGE_LEFT_ODD)].tile);
    EXPECT_EQ(cells[90].tile, sheet[static_cast<std::size_t>(WindowTile::CORNER_BOTTOM_LEFT)].tile);
}

// --- WindowShading -----------------------------------------------------------------------------------

// The level and side each screen row gets from `regions`, from y `top` for `rows` rows: 0 where no
// region covers it, positive for lightening and negative for darkening.
std::vector<int> shadingByRow(const Regions& regions, float top, int rows) {
    std::vector<int> byRow(static_cast<std::size_t>(rows), 0);
    for (const retropp::Region& region : regions) {
        EXPECT_EQ(region.shape.points.size(), 4u);
        EXPECT_EQ(region.effects.size(), 1u);
        EXPECT_EQ(region.effects[0].kind, retropp::ScreenSpaceEffectKind::ColorFill);
        const retropp::Rgba8 fill = region.effects[0].fill;
        EXPECT_EQ(fill.r, fill.g);
        EXPECT_EQ(fill.g, fill.b);
        const int level = static_cast<int>(std::lround(fill.r * 31.0 / 255.0));
        EXPECT_EQ(fill.r, (level * 255 + 15) / 31);
        const int sign = region.blend == retropp::BlendMode::Add ? 1 : -1;
        EXPECT_TRUE(region.blend == retropp::BlendMode::Add ||
                    region.blend == retropp::BlendMode::Subtract);
        EXPECT_EQ(region.shape.points[0].x, static_cast<float>(kEnglishFirstColumnX));
        EXPECT_EQ(region.shape.points[2].x - region.shape.points[0].x, 240.0f);
        const int first = static_cast<int>(region.shape.points[0].y - top);
        const int end = static_cast<int>(region.shape.points[2].y - top);
        for (int y = first; y < end; ++y) {
            if (y < 0 || y >= rows) {
                ADD_FAILURE() << "a region covers row " << y << ", outside the " << rows
                              << " rows shown";
                continue;
            }
            EXPECT_EQ(byRow[static_cast<std::size_t>(y)], 0) << "row " << y << " covered twice";
            byRow[static_cast<std::size_t>(y)] = sign * level;
        }
    }
    return byRow;
}

std::vector<int> expectedByRow(int tableHeight, int first, int end) {
    const auto table = text::windowShading(tableHeight);
    std::vector<int> byRow;
    for (int row = first; row < end; ++row) {
        const int level = table[static_cast<std::size_t>(row)].level;
        byRow.push_back(text::shadingLightens(row, tableHeight) ? level : -level);
    }
    return byRow;
}

TEST(WindowShading, TheOpenBoxIsShadedByTheOpenTable) {
    for (const WindowPlacement placement : {WindowPlacement::Top, WindowPlacement::Bottom}) {
        const DialogWindowState state = open(placement);
        const Regions regions = WindowShading(state, boxTopLeft(state));
        EXPECT_EQ(shadingByRow(regions, boxTopLeft(state).y, 72), expectedByRow(72, 0, 72));
    }
}

// One region per run of equal level on one side of the middle, and none for level 0.
TEST(WindowShading, OneRegionPerRunNoneForLevelZero) {
    const DialogWindowState state = open();
    const Regions regions = WindowShading(state, boxTopLeft(state));
    // 7 / 6 5 4 3 2 1 above the middle, 1 2 3 4 5 6 / 7 below it.
    EXPECT_EQ(regions.size(), 14u);
    EXPECT_EQ(regions.front().blend, retropp::BlendMode::Add);
    EXPECT_EQ(regions.back().blend, retropp::BlendMode::Subtract);
    EXPECT_EQ(regions.front().effects[0].fill.r, 58);  // level 7 of 31
}

// Each opening step shades the rows it shows with its own height's table.
TEST(WindowShading, EachOpeningStepUsesItsOwnHeightsTable) {
    const std::vector<int> heights{8, 24, 40, 56};
    for (int step = 1; step <= 4; ++step) {
        const DialogWindowState state = afterTicks(opening(), step);
        const int height = heights[static_cast<std::size_t>(step - 1)];
        const float top = boxTopLeft(state).y + static_cast<float>((72 - height) / 2);
        const Regions regions = WindowShading(state, boxTopLeft(state));
        EXPECT_EQ(shadingByRow(regions, top, height), expectedByRow(height, 0, height)) << step;
    }
}

// The closing box keeps the open box's shading on the rows that remain.
TEST(WindowShading, TheClosingBoxKeepsTheOpenShadingOnTheRowsLeft) {
    const DialogWindowState state = closingAfter(2);  // 5 rows: box rows 2-6
    ASSERT_EQ(text::rowsShown(state), 5);
    const float top = boxTopLeft(state).y + 16.0f;
    EXPECT_EQ(shadingByRow(WindowShading(state, boxTopLeft(state)), top, 40),
              expectedByRow(72, 16, 56));
}

TEST(WindowShading, TheTitleBoxUsesThe40RowTablesFirst32Rows) {
    const DialogWindowState state = title();
    const Regions regions = WindowShading(state, boxTopLeft(state));
    EXPECT_EQ(shadingByRow(regions, boxTopLeft(state).y, 32), expectedByRow(40, 0, 32));
}

TEST(WindowShading, NothingForAClosedWindowOrTextOnly) {
    DialogWindowState closed;
    text::initDialogWindow(closed);
    EXPECT_TRUE(WindowShading(closed, boxTopLeft(closed)).empty());
    const DialogWindowState textOnly = afterTicks(opening(WindowPlacement::Top, true), 5);
    EXPECT_TRUE(WindowShading(textOnly, boxTopLeft(textOnly)).empty());
}

// --- WindowSquash ------------------------------------------------------------------------------------

// On opening steps 1-4 the box is scaled by (rows shown) / 9 about its middle row; the middle row
// stays put and the top edge lands on the first row shown.
TEST(WindowSquash, OpeningStepsScaleAboutTheMiddle) {
    for (const WindowPlacement placement : {WindowPlacement::Top, WindowPlacement::Bottom}) {
        for (int step = 1; step <= 4; ++step) {
            const DialogWindowState state = afterTicks(opening(placement), step);
            const retropp::Transform t = WindowSquash(state, boxTopLeft(state));
            const float rows = static_cast<float>(2 * step - 1);
            EXPECT_FLOAT_EQ(t.m00, 1.0f);
            EXPECT_FLOAT_EQ(t.m11, rows / 9.0f);
            const float top = boxTopLeft(state).y;
            const float middle = top + 36.0f;
            EXPECT_FLOAT_EQ(t.m11 * middle + t.m12, middle);
            EXPECT_FLOAT_EQ(t.m11 * top + t.m12, middle - rows * 4.0f);
        }
    }
}

TEST(WindowSquash, IdentityOnceOpenWhileClosingAndForTitleOrTextOnly) {
    const retropp::Transform identity = retropp::Transform::identity();
    EXPECT_EQ(WindowSquash(open(), boxTopLeft(open())), identity);
    EXPECT_EQ(WindowSquash(closingAfter(1), boxTopLeft(closingAfter(1))), identity);
    EXPECT_EQ(WindowSquash(title(), boxTopLeft(title())), identity);
    const DialogWindowState textOnly = afterTicks(opening(WindowPlacement::Top, true), 2);
    EXPECT_EQ(WindowSquash(textOnly, boxTopLeft(textOnly)), identity);
}

// --- WindowClip --------------------------------------------------------------------------------------

TEST(WindowClip, WhileMovingEverythingOutsideTheRowsShownIsSeeThrough) {
    for (const DialogWindowState& state : {afterTicks(opening(), 2), closingAfter(1)}) {
        const Regions regions = WindowClip(state, boxTopLeft(state));
        ASSERT_EQ(regions.size(), 1u);
        const retropp::Region& clip = regions[0];
        EXPECT_EQ(clip.key.value, kWindowClipKey);
        ASSERT_EQ(clip.effects.size(), 1u);
        EXPECT_EQ(clip.effects[0].kind, retropp::ScreenSpaceEffectKind::Transparency);
        EXPECT_EQ(clip.effects[0].stencil, retropp::StencilMode::TransparentOutside);
        const float top = boxTopLeft(state).y +
                          static_cast<float>(text::firstRowShown(state) * text::kWindowCellPx);
        EXPECT_EQ(clip.shape.points[0], (retropp::Point{.x = 0.0f, .y = top}));
        EXPECT_EQ(clip.shape.points[2],
                  (retropp::Point{.x = 256.0f,
                                  .y = top + static_cast<float>(text::rowsShown(state) * 8)}));
    }
}

TEST(WindowClip, WithNoRowsShownTheWholeLayerIsSeeThrough) {
    const DialogWindowState state = closingAfter(5);
    ASSERT_EQ(text::rowsShown(state), 0);
    const Regions regions = WindowClip(state, boxTopLeft(state));
    ASSERT_EQ(regions.size(), 1u);
    EXPECT_EQ(regions[0].effects[0].kind, retropp::ScreenSpaceEffectKind::Transparency);
    EXPECT_EQ(regions[0].effects[0].stencil, retropp::StencilMode::TransparentInside);
    EXPECT_EQ(regions[0].shape.points[2], (retropp::Point{.x = 256.0f, .y = 224.0f}));
}

TEST(WindowClip, NothingOnceOpenWhenClosedOrForTheTitle) {
    EXPECT_TRUE(WindowClip(open(), boxTopLeft(open())).empty());
    DialogWindowState closed;
    text::initDialogWindow(closed);
    EXPECT_TRUE(WindowClip(closed, boxTopLeft(closed)).empty());
    EXPECT_TRUE(WindowClip(title(), boxTopLeft(title())).empty());
}

// --- TileLayer ---------------------------------------------------------------------------------------

TEST(TileLayer, AFiniteScreenSizedTileLayerWithEveryOptionPassedThrough) {
    const Regions regions = WindowShading(open(), boxTopLeft(open()));
    const retropp::Transform squash = retropp::Transform::scale(1.0f, 0.5f, 8.0f, 44.0f);
    const retropp::DrawLayer layer =
        TileLayer("box", kDialogWindowZ, DialogWindow(open(), windowSheet(), kWindowPalette), 30, 9,
                  {.scroll = {.x = -8, .y = -8},
                   .alpha = 0.5f,
                   .blend = retropp::BlendMode::Multiply,
                   .regions = regions,
                   .transform = squash,
                   .transformEdge = retropp::DisplacementEdge::Stretch});
    EXPECT_EQ(layer.key.value, "box");
    EXPECT_EQ(layer.z, kDialogWindowZ);
    EXPECT_EQ(layer.size, kScreenSize);
    EXPECT_EQ(layer.scroll, (retropp::LayerScroll{.x = -8, .y = -8}));
    EXPECT_FLOAT_EQ(layer.alpha, 0.5f);
    EXPECT_EQ(layer.blend, retropp::BlendMode::Multiply);
    EXPECT_EQ(layer.regions.size(), regions.size());
    EXPECT_EQ(layer.transform, squash);
    EXPECT_EQ(layer.transformEdge, retropp::DisplacementEdge::Stretch);
    const auto& content = std::get<retropp::TileContent>(layer.content);
    EXPECT_EQ(content.widthInTiles, 30);
    EXPECT_EQ(content.heightInTiles, 9);
    EXPECT_EQ(content.cells.size(), 270u);
    EXPECT_EQ(content.wrap, retropp::TileWrap::Blank);
}

TEST(TileLayer, GivenNoOptionsDrawsAsAPlainLayer) {
    const retropp::DrawLayer layer = TileLayer("plain", 1, Cells(4), 2, 2);
    const retropp::DrawLayer plain{.key = "plain"};
    EXPECT_EQ(layer.scroll, plain.scroll);
    EXPECT_EQ(layer.alpha, plain.alpha);
    EXPECT_EQ(layer.blend, plain.blend);
    EXPECT_TRUE(layer.regions.empty());
    EXPECT_TRUE(layer.effects.empty());
    EXPECT_EQ(layer.transform, plain.transform);
    EXPECT_EQ(layer.transformEdge, plain.transformEdge);
}

// The cells are kept under the key until the next call with that key replaces them.
TEST(TileLayer, KeepsItsCellsUntilTheNextCallWithItsKey) {
    const retropp::DrawLayer first = TileLayer("kept", 1, Cells(3), 3, 1);
    const auto firstCells = std::get<retropp::TileContent>(first.content).cells;
    const retropp::DrawLayer other = TileLayer("other", 2, Cells(5), 5, 1);
    EXPECT_EQ(firstCells.size(), 3u);
    const retropp::DrawLayer second = TileLayer("kept", 1, Cells(6), 3, 2);
    EXPECT_EQ(std::get<retropp::TileContent>(second.content).cells.size(), 6u);
    EXPECT_EQ(std::get<retropp::TileContent>(other.content).cells.size(), 5u);
}

}  // namespace
}  // namespace ostinato::render
