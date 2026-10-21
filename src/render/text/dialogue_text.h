// DialogueText — the glyphs on the dialogue's current page.
//
//     SpriteLayer("dialogue", kDialogueTextZ,
//                 {DialogueText(state, textX, textY, glyphs, ink, shadow),
//                  DialogueCursor(state, textX, textY, arrow, ink, shadow)},
//                 {.regions = DialogueWipe(state, textX, textY)});
//
// (x, y) is the top-left corner of the text area on screen. Each glyph the engine has placed is
// drawn on its line kGlyphRowOffset pixels below the line's top with its ink starting at its pen x
// (its artwork starts kGlyphArtLeftOfPen to the left), with its shadow (Glyph). A glyph is keyed by
// its page, line, and place on the line, so the same glyph on the next page is a new object rather
// than this one moved.
//
// While a map title is showing its glyphs are MapTitle's, and this returns nothing. After the title
// closes its glyphs stay on the page until the next message wipes them, as the original's do; the
// window this layer sits in decides whether they are seen.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"
#include "text/dialogue.h"

namespace ostinato::render {

[[nodiscard]] Sprites DialogueText(const text::DialogueState& state, int x, int y,
                                   const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                                   retropp::PaletteId shadow);

}  // namespace ostinato::render
