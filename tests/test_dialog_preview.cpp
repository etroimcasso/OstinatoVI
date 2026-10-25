// The dialog preview's screen, read back from the layers it returns.
//
// No window and no renderer: the sheets are handles only, and each case builds a dialog state and
// a window state and checks what the screen puts on its three layers — the backdrop, the box and the
// text.
#include <gtest/gtest.h>

#include <variant>

#include "retropp/draw_state.h"
#include "retropp/image.h"

#include "dialog_preview.h"
#include "render/layers.h"
#include "render/text/dialog_text.h"
#include "render/text/dialog_wipe.h"
#include "render/text/map_title.h"
#include "render/window/window_clip.h"
#include "render/window/window_shading.h"
#include "ostinato/window_tile.h"
#include "text/dialog.h"
#include "text/dialog_window.h"
#include "text/font.h"
#include "text/layout.h"

namespace ostinato::preview {
namespace {

constexpr int kEnglishGlyphs = 224;

// The screen's layers, back to front.
constexpr std::size_t kBackdropLayer = 0;
constexpr std::size_t kBoxLayer = 1;
constexpr std::size_t kTextLayer = 2;

retropp::AtlasManifest oneRowSheet(int atlas, int tiles) {
    return retropp::AtlasManifest{
        .atlasId = retropp::AtlasId{static_cast<std::uint16_t>(atlas)},
        .slots = retropp::sliceLayout(retropp::PixelSize{.width = tiles * 8, .height = 8},
                                      retropp::AssetDimensions{8, 8},
                                      retropp::ContentKind::Tileset,
                                      retropp::ReadOrder::LeftRightThenDown, tiles),
    };
}

// The English glyph sheet as an upload would carve it; no arrow.
const DialogSheets kSheets{
    .glyphs = retropp::AtlasManifest{
        .atlasId = retropp::AtlasId{7},
        .slots = retropp::sliceLayout(
            retropp::PixelSize{.width = text::kSheetColumns * text::kGlyphCellPx,
                               .height = kEnglishGlyphs / text::kSheetColumns * text::kGlyphCellPx},
            retropp::AssetDimensions{text::kGlyphCellPx, text::kGlyphCellPx},
            retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown, kEnglishGlyphs),
    },
    .ink = retropp::PaletteId{4},
    .shadow = retropp::PaletteId{5},
    .window = oneRowSheet(8, static_cast<int>(ostinato::kWindowTileCount)),
    .windowColors = retropp::PaletteId{6},
    .backdrop = oneRowSheet(9, 1),
    .backdropColor = retropp::PaletteId{7},
};

// A page with two glyphs on line 1, being wiped: three half-lines gone.
text::DialogState wipingPage() {
    text::DialogState state;
    state.page = 2;
    state.placed = {
        {.page = 2, .line = 1, .x = 4, .glyph = 0x20},
        {.page = 2, .line = 1, .x = 12, .glyph = 0x21},
    };
    state.pageClearFrames = text::kPageClearFrames - 3;
    return state;
}

// An open window at `placement`.
text::DialogWindowState openWindow(text::WindowPlacement placement = text::WindowPlacement::Top) {
    text::DialogWindowState window;
    text::initDialogWindow(window);
    text::openDialogWindow(window, placement, false);
    for (int frame = 0; frame < text::kWindowOpenFrames; ++frame) {
        text::tickDialogWindow(window);
    }
    return window;
}

TEST(DialogPreviewScreen, IsTheBackdropTheBoxAndTheTextBackToFront) {
    const render::Layers layers = DialogPreviewScreen(wipingPage(), openWindow(), kSheets);
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[kBackdropLayer].key.value, kBackdropKey);
    EXPECT_EQ(layers[kBackdropLayer].z, kBackdropZ);
    EXPECT_EQ(layers[kBoxLayer].key.value, render::kDialogWindowKey);
    EXPECT_EQ(layers[kBoxLayer].z, render::kDialogWindowZ);
    EXPECT_EQ(layers[kTextLayer].key.value, "dialog");
    EXPECT_EQ(layers[kTextLayer].z, render::kDialogTextZ);
    EXPECT_LT(layers[kBackdropLayer].z, layers[kBoxLayer].z);
    EXPECT_LT(layers[kBoxLayer].z, layers[kTextLayer].z);
}

// The open box: nine rows of thirty cells, its corner at x 8 on cell row 1 or 18, shaded.
TEST(DialogPreviewScreen, TheOpenBoxSitsWhereTheWindowPutsIt) {
    for (const text::WindowPlacement placement :
         {text::WindowPlacement::Top, text::WindowPlacement::Bottom}) {
        const text::DialogWindowState window = openWindow(placement);
        const render::Layers layers = DialogPreviewScreen(wipingPage(), window, kSheets);
        const retropp::DrawLayer& box = layers[kBoxLayer];
        const auto& content = std::get<retropp::TileContent>(box.content);
        EXPECT_EQ(content.widthInTiles, 30);
        EXPECT_EQ(content.heightInTiles, 9);
        EXPECT_EQ(content.wrap, retropp::TileWrap::Blank);
        const int top = text::boxTopRow(window) * text::kWindowCellPx;
        EXPECT_EQ(box.scroll, (retropp::LayerScroll{.x = -8, .y = -top}));
        const retropp::Point corner{.x = 8.0f, .y = static_cast<float>(top)};
        EXPECT_EQ(box.regions.size(), render::WindowShading(window, corner).size());
        EXPECT_EQ(box.transform, retropp::Transform::identity());
    }
}

// While the box opens it is squashed, and both the box and the text are clipped to the rows shown.
TEST(DialogPreviewScreen, WhileOpeningTheBoxSquashesAndBothLayersClip) {
    text::DialogWindowState window;
    text::initDialogWindow(window);
    text::openDialogWindow(window, text::WindowPlacement::Top, false);
    text::tickDialogWindow(window);
    text::tickDialogWindow(window);
    const render::Layers layers = DialogPreviewScreen(wipingPage(), window, kSheets);
    const retropp::Point corner{.x = 8.0f, .y = 8.0f};
    const render::Regions clip = render::WindowClip(window, corner);
    ASSERT_EQ(clip.size(), 1u);
    EXPECT_NE(layers[kBoxLayer].transform, retropp::Transform::identity());
    EXPECT_EQ(layers[kBoxLayer].regions.back().key.value, clip[0].key.value);
    EXPECT_EQ(layers[kTextLayer].regions.back().key.value, clip[0].key.value);
}

// The text sits in the window's text area, spaced as the window spaces it.
TEST(DialogPreviewScreen, DrawsTheTextWhereTheWindowPutsIt) {
    const text::DialogState state = wipingPage();
    for (const text::WindowPlacement placement :
         {text::WindowPlacement::Top, text::WindowPlacement::Bottom}) {
        const text::DialogWindowState window = openWindow(placement);
        const render::Layers layers = DialogPreviewScreen(state, window, kSheets);
        const auto& sprites = std::get<retropp::SpriteContent>(layers[kTextLayer].content).sprites;
        const render::Sprites expected =
            render::DialogText(state, text::kTextAreaLeft, text::textAreaTop(window),
                               text::linePitch(window), kSheets.glyphs, kSheets.ink,
                               kSheets.shadow);
        ASSERT_EQ(sprites.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(sprites[i].key.value, expected[i].key.value);
            EXPECT_EQ(sprites[i].x, expected[i].x);
            EXPECT_EQ(sprites[i].y, expected[i].y);
        }
    }
}

// The first glyph on line 1 of a top box: the text area starts at y 12, line 1 is 15 below it.
TEST(DialogPreviewScreen, LineOneOfATopBoxIsFifteenBelowLineZero) {
    const render::Layers layers = DialogPreviewScreen(wipingPage(), openWindow(), kSheets);
    const auto& sprites = std::get<retropp::SpriteContent>(layers[kTextLayer].content).sprites;
    ASSERT_FALSE(sprites.empty());
    EXPECT_EQ(sprites[1].y, 12 + 15 + text::kGlyphRowOffset);
}

TEST(DialogPreviewScreen, DrawsTheMapTitleInTheTitleBox) {
    text::DialogState state = wipingPage();
    state.titleShowing = true;
    state.pageClearFrames = 0;
    const render::Layers layers = DialogPreviewScreen(state, openWindow(), kSheets);
    const auto& sprites = std::get<retropp::SpriteContent>(layers[kTextLayer].content).sprites;
    const render::Sprites expected =
        render::MapTitle(state, text::kTextAreaLeft, text::titleTextAreaTop(), kSheets.glyphs,
                         kSheets.ink, kSheets.shadow);
    ASSERT_EQ(sprites.size(), expected.size());
    ASSERT_FALSE(sprites.empty());
    EXPECT_EQ(sprites[0].y, expected[0].y);
}

TEST(DialogPreviewScreen, CarriesThePageWipeAsTheLayersRegions) {
    const text::DialogState state = wipingPage();
    const text::DialogWindowState window = openWindow();
    const render::Layers layers = DialogPreviewScreen(state, window, kSheets);
    const render::Regions expected = render::DialogWipe(
        state, text::kTextAreaLeft, text::textAreaTop(window), text::linePitch(window));
    ASSERT_EQ(expected.size(), 3u);
    ASSERT_EQ(layers[kTextLayer].regions.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(layers[kTextLayer].regions[i].key.value, expected[i].key.value);
    }
}

TEST(DialogPreviewScreen, AnIdleDialogInAClosedWindowShowsOnlyTheBackdrop) {
    text::DialogState state;
    text::initDialog(state);
    text::DialogWindowState window;
    text::initDialogWindow(window);
    const render::Layers layers = DialogPreviewScreen(state, window, kSheets);
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_TRUE(std::get<retropp::TileContent>(layers[kBoxLayer].content).cells.empty());
    EXPECT_TRUE(layers[kBoxLayer].regions.empty());
    EXPECT_TRUE(std::get<retropp::SpriteContent>(layers[kTextLayer].content).sprites.empty());
    EXPECT_TRUE(layers[kTextLayer].regions.empty());
    EXPECT_EQ(std::get<retropp::TileContent>(layers[kBackdropLayer].content).cells.size(),
              32u * 28u);
}

}  // namespace
}  // namespace ostinato::preview
