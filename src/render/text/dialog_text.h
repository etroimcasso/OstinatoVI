// DialogText — the glyphs on the dialog's current page.
//
//     const int x = text::kTextAreaLeft;
//     const int y = text::textAreaTop(window);
//     const int pitch = text::linePitch(window);
//     SpriteLayer("dialog", kDialogTextZ,
//                 {DialogText(state, x, y, pitch, glyphs, ink, shadow),
//                  DialogCursor(state, x, y, pitch, arrow, ink, shadow)},
//                 {.regions = DialogWipe(state, x, y, pitch)});
//
// (x, y) is the top-left corner of the text area on screen, and `linePitch` how far apart its lines
// are, top to top — the dialog window decides both. Each glyph the engine has placed is drawn on its
// line kGlyphRowOffset pixels below the line's top with its ink starting at its pen x (its artwork
// starts kGlyphArtLeftOfPen to the left), with its shadow (Glyph). A glyph is keyed by its page,
// line, and place on the line, so the same glyph on the next page is a new object rather than this
// one moved, and a glyph whose line moves when the window finishes opening is the same object.
//
// While a map title is showing its glyphs are MapTitle's, and this returns nothing. After the title
// closes its glyphs stay on the page until the next message wipes them, as the original's do; the
// window this layer sits in decides whether they are seen.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"
#include "text/dialog.h"

namespace ostinato::render {

[[nodiscard]] Sprites DialogText(const text::DialogState& state, int x, int y, int linePitch,
                                   const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                                   retropp::PaletteId shadow);

}  // namespace ostinato::render
