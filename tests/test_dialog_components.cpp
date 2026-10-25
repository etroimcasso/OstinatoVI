// The dialog's rendering components, read back from what they return.
//
// Each component is a function of the dialog state, so each case builds a state — by hand where
// only the geometry matters, by driving the engine where the timing does — calls the component, and
// reads the sprites, regions or layer it returns. No renderer is involved: the sheets are carved
// with the engine's own slicer, exactly as an upload would carve them.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "retropp/draw_state.h"
#include "retropp/image.h"

#include "ostinato/field_text_command.h"
#include "ostinato/text_token.h"
#include "render/layer_options.h"
#include "render/layers.h"
#include "render/sprite_layer.h"
#include "render/text/dialog_cursor.h"
#include "render/text/dialog_text.h"
#include "render/text/dialog_wipe.h"
#include "render/text/glyph.h"
#include "render/text/map_title.h"
#include "render/types.h"
#include "text/dialog.h"
#include "text/font.h"
#include "text/layout.h"

namespace ostinato::render {
namespace {

using text::kGlyphArtLeftOfPen;
using text::kGlyphRowOffset;

constexpr std::size_t kEnglishGlyphs = 224;

// Where the text area sits on screen in these cases — a box at the bottom of the screen — and
// how far apart its lines are once the box is open.
constexpr int kAreaX = text::kTextAreaLeft;
constexpr int kAreaY =
    text::kDialogWindowBottomRow * text::kWindowCellPx + text::kTextAreaTopInBox;
constexpr int kPitch = text::kLinePitch;

const retropp::PaletteId kInk{4};
const retropp::PaletteId kShadow{5};

// The English glyph sheet as an upload would carve it.
retropp::AtlasManifest glyphSheet() {
    return retropp::AtlasManifest{
        .atlasId = retropp::AtlasId{7},
        .slots = retropp::sliceLayout(
            retropp::PixelSize{.width = text::kSheetColumns * text::kGlyphCellPx,
                               .height = static_cast<int>(kEnglishGlyphs) /
                                         text::kSheetColumns * text::kGlyphCellPx},
            retropp::AssetDimensions{text::kGlyphCellPx, text::kGlyphCellPx},
            retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown,
            static_cast<int>(kEnglishGlyphs)),
    };
}

// The arrow's one-cell sheet.
retropp::AtlasManifest arrowSheet() {
    return retropp::AtlasManifest{
        .atlasId = retropp::AtlasId{8},
        .slots = retropp::sliceLayout(
            retropp::PixelSize{.width = text::kCursorWidth, .height = text::kCursorCellHeight},
            retropp::AssetDimensions{text::kCursorWidth, text::kCursorCellHeight},
            retropp::ContentKind::Single, retropp::ReadOrder::LeftRightThenDown),
    };
}

std::string keyOf(const retropp::Sprite& sprite) { return sprite.key.value; }

const retropp::Sprite* find(const Sprites& sprites, std::string_view key) {
    const auto it = std::find_if(sprites.begin(), sprites.end(),
                                 [&](const retropp::Sprite& s) { return s.key.value == key; });
    return it == sprites.end() ? nullptr : &*it;
}

// A page with three glyphs on line 0 and one on line 2.
text::DialogState pageOfGlyphs() {
    text::DialogState state;
    state.page = 3;
    state.placed = {
        {.page = 3, .line = 0, .x = 4, .glyph = 0x20},
        {.page = 3, .line = 0, .x = 12, .glyph = 0x21},
        {.page = 3, .line = 0, .x = 20, .glyph = 0x7F},
        {.page = 3, .line = 2, .x = 4, .glyph = 0x22},
    };
    return state;
}

// --- SpriteLayer --------------------------------------------------------------------------------

TEST(SpriteLayer, GivenNoOptionsDrawsAsAPlainLayer) {
    const retropp::DrawLayer plain{.key = "plain"};
    const retropp::DrawLayer layer = SpriteLayer("plain-sprites", 12, {});
    EXPECT_EQ(layer.key.value, "plain-sprites");
    EXPECT_EQ(layer.z, 12);
    EXPECT_EQ(layer.size, kScreenSize);
    EXPECT_EQ(layer.scroll, plain.scroll);
    EXPECT_EQ(layer.alpha, plain.alpha);
    EXPECT_EQ(layer.blend, plain.blend);
    EXPECT_TRUE(layer.effects.empty());
    EXPECT_TRUE(layer.regions.empty());
    EXPECT_EQ(layer.transform, plain.transform);
    EXPECT_EQ(layer.transformEdge, plain.transformEdge);
    ASSERT_TRUE(std::holds_alternative<retropp::SpriteContent>(layer.content));
    EXPECT_TRUE(std::get<retropp::SpriteContent>(layer.content).sprites.empty());
}

TEST(SpriteLayer, CoversTheWholeScreen) {
    EXPECT_EQ(kScreenSize, (retropp::PixelSize{.width = 256, .height = 224}));
}

TEST(SpriteLayer, ForwardsEveryOption) {
    const retropp::DrawLayer layer = SpriteLayer(
        "forwarded", 13, {},
        LayerOptions{
            .scroll = {.x = 3, .y = -2},
            .alpha = 0.25f,
            .blend = retropp::BlendMode::Multiply,
            .effects = {retropp::ScreenSpaceEffect{.kind = retropp::ScreenSpaceEffectKind::Gleam}},
            .regions = {retropp::Region{.key = "r"}},
            .transform = retropp::Transform::translation(5.0f, 6.0f),
            .transformEdge = retropp::DisplacementEdge::Stretch,
        });
    EXPECT_EQ(layer.scroll, (retropp::LayerScroll{.x = 3, .y = -2}));
    EXPECT_EQ(layer.alpha, 0.25f);
    EXPECT_EQ(layer.blend, retropp::BlendMode::Multiply);
    ASSERT_EQ(layer.effects.size(), 1u);
    EXPECT_EQ(layer.effects[0].kind, retropp::ScreenSpaceEffectKind::Gleam);
    ASSERT_EQ(layer.regions.size(), 1u);
    EXPECT_EQ(layer.regions[0].key.value, "r");
    EXPECT_EQ(layer.transform, retropp::Transform::translation(5.0f, 6.0f));
    EXPECT_EQ(layer.transformEdge, retropp::DisplacementEdge::Stretch);
}

TEST(SpriteLayer, KeepsItsChildrenInOrderAfterTheyAreGone) {
    // The children are temporaries that end with this statement; the layer's view must not.
    const retropp::DrawLayer layer =
        SpriteLayer("kept", 14,
                    {Sprites{retropp::Sprite{.key = "a"}, retropp::Sprite{.key = "b"}},
                     Sprites{retropp::Sprite{.key = "c"}}});
    const auto& sprites = std::get<retropp::SpriteContent>(layer.content).sprites;
    ASSERT_EQ(sprites.size(), 3u);
    EXPECT_EQ(keyOf(sprites[0]), "a");
    EXPECT_EQ(keyOf(sprites[1]), "b");
    EXPECT_EQ(keyOf(sprites[2]), "c");
}

TEST(SpriteLayer, TheNextCallWithTheSameKeyReplacesWhatItKept) {
    (void)SpriteLayer("replaced", 15, {Sprites{retropp::Sprite{.key = "old"}}});
    const retropp::DrawLayer layer =
        SpriteLayer("replaced", 15, {Sprites{retropp::Sprite{.key = "new"}}});
    const auto& sprites = std::get<retropp::SpriteContent>(layer.content).sprites;
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(keyOf(sprites[0]), "new");
}

TEST(SpriteLayer, LayersWithDifferentKeysKeepTheirOwnSprites) {
    const retropp::DrawLayer first = SpriteLayer("first", 16, {Sprites{retropp::Sprite{.key = "1"}}});
    const retropp::DrawLayer second =
        SpriteLayer("second", 17, {Sprites{retropp::Sprite{.key = "2"}}});
    EXPECT_EQ(keyOf(std::get<retropp::SpriteContent>(first.content).sprites[0]), "1");
    EXPECT_EQ(keyOf(std::get<retropp::SpriteContent>(second.content).sprites[0]), "2");
}

// --- Glyph --------------------------------------------------------------------------------------

TEST(Glyph, IsTheShadowThenTheLetterFromOneCell) {
    const retropp::AtlasManifest sheet = glyphSheet();
    const std::uint16_t glyph = 0x41;
    const Sprites sprites = Glyph("g", glyph, 40, 60, sheet, kInk, kShadow);
    ASSERT_EQ(sprites.size(), 2u);
    const retropp::Sprite& shadow = sprites[0];
    const retropp::Sprite& letter = sprites[1];

    EXPECT_EQ(keyOf(letter), "g");
    EXPECT_EQ(keyOf(shadow), "g:shadow");
    EXPECT_EQ(letter.x, 40);
    EXPECT_EQ(letter.y, 60);
    EXPECT_EQ(shadow.x, 41);  // one right
    EXPECT_EQ(shadow.y, 61);  // one down
    EXPECT_LT(shadow.z, letter.z);
    EXPECT_EQ(letter.palette, kInk);
    EXPECT_EQ(shadow.palette, kShadow);

    const retropp::AssetSlot& cell = sheet[text::glyphSlot(glyph)];
    for (const retropp::Sprite& s : sprites) {
        EXPECT_EQ(s.atlas, sheet.atlasId);
        EXPECT_EQ(s.tile, cell.tile);
        EXPECT_EQ(s.size, (retropp::AssetDimensions{text::kGlyphCellPx, text::kGlyphCellPx}));
    }
}

// --- DialogText -------------------------------------------------------------------------------

TEST(DialogText, DrawsEveryPlacedGlyphWithItsShadow) {
    const text::DialogState state = pageOfGlyphs();
    const Sprites sprites = DialogText(state, kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow);
    EXPECT_EQ(sprites.size(), 2 * state.placed.size());
}

TEST(DialogText, PutsTheInkAtThePenOnTheGlyphsLine) {
    const text::DialogState state = pageOfGlyphs();
    const Sprites sprites = DialogText(state, kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow);

    // The second glyph on line 0, at pen x 12.
    const retropp::Sprite* second = find(sprites, "dlg:3:0:1");
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->x, kAreaX + 12 - kGlyphArtLeftOfPen);
    EXPECT_EQ(second->y, kAreaY + kGlyphRowOffset);
    EXPECT_EQ(second->tile, glyphSheet()[text::glyphSlot(0x21)].tile);

    // The first glyph on line 2.
    const retropp::Sprite* third = find(sprites, "dlg:3:2:0");
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third->x, kAreaX + 4 - kGlyphArtLeftOfPen);
    EXPECT_EQ(third->y, kAreaY + 2 * kPitch + kGlyphRowOffset);

    const retropp::Sprite* shadow = find(sprites, "dlg:3:2:0:shadow");
    ASSERT_NE(shadow, nullptr);
    EXPECT_EQ(shadow->x, third->x + text::kShadowDx);
    EXPECT_EQ(shadow->y, third->y + text::kShadowDy);
}

// The lines are as far apart as the pitch the window gives: 16 while it opens, 15 once open. The
// glyph keeps its key, so it moves rather than being replaced.
TEST(DialogText, LinesFollowThePitchTheyAreGiven) {
    const text::DialogState state = pageOfGlyphs();
    const Sprites opening = DialogText(state, kAreaX, kAreaY, text::kOpeningLinePitch,
                                       glyphSheet(), kInk, kShadow);
    const Sprites open =
        DialogText(state, kAreaX, kAreaY, text::kLinePitch, glyphSheet(), kInk, kShadow);
    const retropp::Sprite* before = find(opening, "dlg:3:2:0");
    const retropp::Sprite* after = find(open, "dlg:3:2:0");
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(before->y, kAreaY + 2 * 16 + kGlyphRowOffset);
    EXPECT_EQ(after->y, kAreaY + 2 * 15 + kGlyphRowOffset);
    EXPECT_EQ(before->y - after->y, 2);

    // Line 0 does not move.
    EXPECT_EQ(find(opening, "dlg:3:0:0")->y, find(open, "dlg:3:0:0")->y);
}

TEST(DialogText, KeysCountGlyphsWithinTheirLineAndPage) {
    const Sprites sprites =
        DialogText(pageOfGlyphs(), kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow);
    std::vector<std::string> letters;
    for (const retropp::Sprite& s : sprites) {
        if (s.z == kInkSpriteZ) {
            letters.push_back(keyOf(s));
        }
    }
    EXPECT_EQ(letters,
              (std::vector<std::string>{"dlg:3:0:0", "dlg:3:0:1", "dlg:3:0:2", "dlg:3:2:0"}));
}

TEST(DialogText, EveryShadowIsBeneathEveryLetter) {
    const Sprites sprites =
        DialogText(pageOfGlyphs(), kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow);
    for (const retropp::Sprite& s : sprites) {
        const bool isShadow = s.key.value.ends_with(kShadowKeySuffix);
        EXPECT_EQ(s.z, isShadow ? kShadowSpriteZ : kInkSpriteZ) << keyOf(s);
        EXPECT_EQ(s.palette, isShadow ? kShadow : kInk) << keyOf(s);
    }
    EXPECT_LT(kShadowSpriteZ, kInkSpriteZ);
}

TEST(DialogText, LeavesAShowingMapTitleToMapTitle) {
    text::DialogState state = pageOfGlyphs();
    state.titleShowing = true;
    EXPECT_TRUE(DialogText(state, kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow).empty());
}

TEST(DialogText, AnEmptyPageDrawsNothing) {
    EXPECT_TRUE(
        DialogText(text::DialogState{}, kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow).empty());
}

// --- MapTitle -----------------------------------------------------------------------------------

TEST(MapTitle, DrawsTheTitleWhileItIsUp) {
    text::DialogState state = pageOfGlyphs();
    state.titleShowing = true;
    const Sprites sprites = MapTitle(state, kAreaX, kAreaY, glyphSheet(), kInk, kShadow);
    ASSERT_EQ(sprites.size(), 2 * state.placed.size());

    const retropp::Sprite* first = find(sprites, "title:0");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->x, kAreaX + 4 - kGlyphArtLeftOfPen);
    EXPECT_EQ(first->y, kAreaY + kGlyphRowOffset);
    EXPECT_NE(find(sprites, "title:3:shadow"), nullptr);
}

TEST(MapTitle, DrawsNothingOnceTheTitleIsDown) {
    EXPECT_TRUE(MapTitle(pageOfGlyphs(), kAreaX, kAreaY, glyphSheet(), kInk, kShadow).empty());
}

// --- DialogCursor -----------------------------------------------------------------------------

TEST(DialogCursor, DrawsNothingWithoutAnArrowOnScreen) {
    EXPECT_TRUE(DialogCursor(pageOfGlyphs(), kAreaX, kAreaY, kPitch, arrowSheet(), kInk, kShadow).empty());
}

TEST(DialogCursor, DrawsNothingWithoutArrowArtwork) {
    text::DialogState state = pageOfGlyphs();
    state.cursor = text::ChoicePosition{.line = 1, .x = 48};
    EXPECT_TRUE(
        DialogCursor(state, kAreaX, kAreaY, kPitch, retropp::AtlasManifest{}, kInk, kShadow).empty());
}

TEST(DialogCursor, SitsAtTheChoicesColumnWithItsShadowToTheRight) {
    text::DialogState state = pageOfGlyphs();
    state.choices.selection = 1;
    state.cursor = text::ChoicePosition{.line = 2, .x = 64};
    const retropp::AtlasManifest sheet = arrowSheet();
    const Sprites sprites = DialogCursor(state, kAreaX, kAreaY, kPitch, sheet, kInk, kShadow);
    ASSERT_EQ(sprites.size(), 2u);
    const retropp::Sprite& shadow = sprites[0];
    const retropp::Sprite& arrow = sprites[1];

    EXPECT_EQ(keyOf(arrow), "dlgc:3:1");
    EXPECT_EQ(keyOf(shadow), "dlgc:3:1:shadow");
    EXPECT_EQ(arrow.x, kAreaX + 64);
    EXPECT_EQ(arrow.y, kAreaY + 2 * kPitch + kGlyphRowOffset);
    EXPECT_EQ(shadow.x, arrow.x + 1);
    EXPECT_EQ(shadow.y, arrow.y);
    EXPECT_EQ(arrow.z, kInkSpriteZ);
    EXPECT_EQ(shadow.z, kShadowSpriteZ);
    EXPECT_EQ(arrow.palette, kInk);
    EXPECT_EQ(shadow.palette, kShadow);
    EXPECT_EQ(arrow.atlas, sheet.atlasId);
    EXPECT_EQ(arrow.size, (retropp::AssetDimensions{text::kCursorWidth, text::kCursorCellHeight}));
}

TEST(DialogCursor, MovingTheSelectionMakesANewArrow) {
    text::DialogState state = pageOfGlyphs();
    state.cursor = text::ChoicePosition{.line = 2, .x = 64};
    state.choices.selection = 0;
    const Sprites before = DialogCursor(state, kAreaX, kAreaY, kPitch, arrowSheet(), kInk, kShadow);
    state.choices.selection = 1;
    state.cursor = text::ChoicePosition{.line = 2, .x = 128};
    const Sprites after = DialogCursor(state, kAreaX, kAreaY, kPitch, arrowSheet(), kInk, kShadow);
    EXPECT_NE(keyOf(before[1]), keyOf(after[1]));
}

// --- DialogWipe -------------------------------------------------------------------------------

TEST(DialogWipe, NothingIsGoneWhileNoPageIsWiped) {
    EXPECT_TRUE(DialogWipe(pageOfGlyphs(), kAreaX, kAreaY, kPitch).empty());
}

// A gone half-line covers its own line's glyph and shadow rows and none of the lines around it:
// the next line's glyphs start below it, and the line above's shadow row ends above it.
TEST(DialogWipe, EachHalfLineCoversOnlyItsOwnLine) {
    text::DialogState state = pageOfGlyphs();
    state.pageClearFrames = 1;
    const Regions regions = DialogWipe(state, kAreaX, kAreaY, kPitch);
    ASSERT_EQ(static_cast<int>(regions.size()), text::kWipeSteps);
    for (int line = 0; line < text::kLinesPerPage; ++line) {
        const retropp::Region& region = regions[static_cast<std::size_t>(2 * line)];
        const float top = region.shape.points[0].y;
        const float bottom = region.shape.points[2].y;
        const int glyphTop = kAreaY + line * kPitch + kGlyphRowOffset;
        EXPECT_EQ(top, static_cast<float>(glyphTop));
        EXPECT_EQ(bottom, static_cast<float>(glyphTop + text::kGlyphRows + text::kShadowDy));
        EXPECT_LE(bottom, static_cast<float>(glyphTop + kPitch)) << "reaches line " << line + 1;
        EXPECT_GE(top, static_cast<float>(glyphTop - kPitch + text::kGlyphRows + text::kShadowDy))
            << "reaches line " << line - 1;
    }
    EXPECT_LE(text::kCursorRows, text::kLineInkRows);
}

TEST(DialogWipe, OneRegionPerHalfLineGone) {
    text::DialogState state = pageOfGlyphs();
    for (int frames = text::kPageClearFrames; frames >= 0; --frames) {
        state.pageClearFrames = frames;
        EXPECT_EQ(static_cast<int>(DialogWipe(state, kAreaX, kAreaY, kPitch).size()),
                  text::halfLinesCleared(state))
            << frames;
    }
}

TEST(DialogWipe, HalfLinesGoLeftThenRightTopLineFirst) {
    text::DialogState state = pageOfGlyphs();
    state.pageClearFrames = 1;  // the last frame of the wipe: every half-line is gone
    const Regions regions = DialogWipe(state, kAreaX, kAreaY, kPitch);
    ASSERT_EQ(static_cast<int>(regions.size()), text::kWipeSteps);

    for (int i = 0; i < text::kWipeSteps; ++i) {
        const int line = i / 2;
        const int half = i % 2;
        const retropp::Region& region = regions[static_cast<std::size_t>(i)];
        EXPECT_EQ(region.key.value, "dlgw:" + std::to_string(line) + ":" + std::to_string(half));

        const float left = static_cast<float>(kAreaX + half * text::kHalfLinePx);
        const float top = static_cast<float>(kAreaY + line * kPitch + kGlyphRowOffset);
        const float right = left + static_cast<float>(text::kHalfLinePx);
        const float bottom = top + static_cast<float>(text::kLineInkRows);
        ASSERT_EQ(region.shape.points.size(), 4u);
        EXPECT_EQ(region.shape.points[0], (retropp::Point{.x = left, .y = top}));
        EXPECT_EQ(region.shape.points[2], (retropp::Point{.x = right, .y = bottom}));

        ASSERT_EQ(region.effects.size(), 1u);
        EXPECT_EQ(region.effects[0].kind, retropp::ScreenSpaceEffectKind::Transparency);
        EXPECT_EQ(region.effects[0].stencil, retropp::StencilMode::TransparentInside);
    }
}

// --- the components over a running dialog ------------------------------------------------------

std::vector<std::uint8_t> evenWidths() { return std::vector<std::uint8_t>(256, 8); }

TEST(DialogComponents, ADismissedPageGoesHalfALineAFrame) {
    const std::vector<std::uint8_t> widths = evenWidths();
    text::DialogContext context;
    context.metrics = text::FontMetrics{widths};
    text::DialogState state;
    text::initDialog(state);

    // Four lines, one glyph each; the fourth line break ends the page.
    const auto glyphs = GlyphRun{{0x20}};
    const auto newline = FieldControl{FieldTextCommand::NEWLINE, 0};
    text::startDialog(state, {glyphs, newline, glyphs, newline, glyphs, newline, glyphs, newline,
                                glyphs, FieldControl{FieldTextCommand::END, 0}});
    const text::DialogInput nothing{};
    const text::DialogInput confirm{.confirmHeld = true};
    for (int frame = 0; frame < 8; ++frame) {
        text::tickDialog(state, nothing, context);
    }
    ASSERT_EQ(state.placed.size(), 4u);
    EXPECT_TRUE(DialogWipe(state, kAreaX, kAreaY, kPitch).empty());

    // Release, then press: the page is dismissed and goes over the next eight frames.
    text::tickDialog(state, nothing, context);
    text::tickDialog(state, confirm, context);
    for (int gone = 1; gone <= text::kWipeSteps; ++gone) {
        text::tickDialog(state, nothing, context);
        EXPECT_EQ(static_cast<int>(DialogWipe(state, kAreaX, kAreaY, kPitch).size()), gone);
        EXPECT_EQ(DialogText(state, kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow).size(), 8u);
    }

    // Then the page is gone and nothing is left to hide.
    text::tickDialog(state, nothing, context);
    EXPECT_TRUE(DialogWipe(state, kAreaX, kAreaY, kPitch).empty());
    EXPECT_TRUE(DialogText(state, kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow).empty());
}

// A glyph across the middle of a line: the right half-line's region starts inside it, so the wipe
// takes exactly the part of it that lies in the right half.
TEST(DialogComponents, TheWipeSplitsAGlyphAcrossTheMiddle) {
    text::DialogState state;
    state.page = 1;
    state.placed = {{.page = 1, .line = 0, .x = text::kHalfLinePx - 3, .glyph = 0x20}};
    state.pageClearFrames = text::kPageClearFrames - 2;  // line 0's two halves are gone

    const Sprites sprites = DialogText(state, kAreaX, kAreaY, kPitch, glyphSheet(), kInk, kShadow);
    const retropp::Sprite* letter = find(sprites, "dlg:1:0:0");
    ASSERT_NE(letter, nullptr);
    const Regions regions = DialogWipe(state, kAreaX, kAreaY, kPitch);
    ASSERT_EQ(regions.size(), 2u);
    const float middle = regions[1].shape.points[0].x;
    EXPECT_GT(middle, static_cast<float>(letter->x));
    EXPECT_LT(middle, static_cast<float>(letter->x + text::kGlyphCellPx));
}

TEST(DialogComponents, OneLayerHoldsTheTextTheArrowAndTheWipe) {
    text::DialogState state = pageOfGlyphs();
    state.cursor = text::ChoicePosition{.line = 2, .x = 64};
    state.pageClearFrames = text::kPageClearFrames - 1;
    const retropp::AtlasManifest glyphs = glyphSheet();
    const retropp::AtlasManifest arrow = arrowSheet();

    const retropp::DrawLayer layer = SpriteLayer(
        "dialog", kDialogTextZ,
        {DialogText(state, kAreaX, kAreaY, kPitch, glyphs, kInk, kShadow),
         DialogCursor(state, kAreaX, kAreaY, kPitch, arrow, kInk, kShadow)},
        {.regions = DialogWipe(state, kAreaX, kAreaY, kPitch)});
    EXPECT_EQ(layer.z, kDialogTextZ);
    EXPECT_EQ(std::get<retropp::SpriteContent>(layer.content).sprites.size(),
              2 * state.placed.size() + 2);
    EXPECT_EQ(layer.regions.size(), 1u);
}

}  // namespace
}  // namespace ostinato::render
