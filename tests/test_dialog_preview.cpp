// The dialog preview's screen, read back from the layers it returns.
//
// No window and no renderer: the sheets are handles only, and each case builds a dialog state by
// hand and checks what the screen puts on its one layer.
#include <gtest/gtest.h>

#include <variant>

#include "retropp/draw_state.h"
#include "retropp/image.h"

#include "dialog_preview.h"
#include "render/layers.h"
#include "render/text/dialog_text.h"
#include "render/text/dialog_wipe.h"
#include "render/text/map_title.h"
#include "text/dialog.h"
#include "text/dialog_window.h"
#include "text/font.h"
#include "text/layout.h"

namespace ostinato::preview {
namespace {

constexpr int kEnglishGlyphs = 224;

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

TEST(DialogPreviewScreen, IsOneDialogLayerAtTheDialogDepth) {
    const render::Layers layers = DialogPreviewScreen(wipingPage(), openWindow(), kSheets);
    ASSERT_EQ(layers.size(), 1u);
    EXPECT_EQ(layers[0].key.value, "dialog");
    EXPECT_EQ(layers[0].z, render::kDialogTextZ);
}

// The text sits in the window's text area, spaced as the window spaces it.
TEST(DialogPreviewScreen, DrawsTheTextWhereTheWindowPutsIt) {
    const text::DialogState state = wipingPage();
    for (const text::WindowPlacement placement :
         {text::WindowPlacement::Top, text::WindowPlacement::Bottom}) {
        const text::DialogWindowState window = openWindow(placement);
        const render::Layers layers = DialogPreviewScreen(state, window, kSheets);
        const auto& sprites = std::get<retropp::SpriteContent>(layers[0].content).sprites;
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
    const auto& sprites = std::get<retropp::SpriteContent>(layers[0].content).sprites;
    ASSERT_FALSE(sprites.empty());
    EXPECT_EQ(sprites[1].y, 12 + 15 + text::kGlyphRowOffset);
}

TEST(DialogPreviewScreen, DrawsTheMapTitleInTheTitleBox) {
    text::DialogState state = wipingPage();
    state.titleShowing = true;
    state.pageClearFrames = 0;
    const render::Layers layers = DialogPreviewScreen(state, openWindow(), kSheets);
    const auto& sprites = std::get<retropp::SpriteContent>(layers[0].content).sprites;
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
    ASSERT_EQ(layers[0].regions.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(layers[0].regions[i].key.value, expected[i].key.value);
    }
}

TEST(DialogPreviewScreen, AnIdleDialogIsAnEmptyLayer) {
    text::DialogState state;
    text::initDialog(state);
    text::DialogWindowState window;
    text::initDialogWindow(window);
    const render::Layers layers = DialogPreviewScreen(state, window, kSheets);
    ASSERT_EQ(layers.size(), 1u);
    EXPECT_TRUE(std::get<retropp::SpriteContent>(layers[0].content).sprites.empty());
    EXPECT_TRUE(layers[0].regions.empty());
}

}  // namespace
}  // namespace ostinato::preview
