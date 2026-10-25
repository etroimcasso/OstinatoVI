// MapTitle — the map's name, centered on the first line while the title is up.
//
//     SpriteLayer("dialog", kDialogTextZ,
//                 {DialogText(...), DialogCursor(...),
//                  MapTitle(state, text::kTextAreaLeft, text::titleTextAreaTop(), glyphs, ink,
//                           shadow)});
//
// (x, y) is the top-left corner of the title's text area on screen, which sits lower in the title
// box than a message's does in its box (text::titleTextAreaTop). The title is one line; its glyphs
// were placed centered on it when the map opened (text::showMapTitle), and each is drawn
// kGlyphRowOffset pixels below the area's top with its shadow (Glyph), keyed title:<n> by its place
// in the title. Returns nothing once the title is down.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"
#include "text/dialog.h"

namespace ostinato::render {

[[nodiscard]] Sprites MapTitle(const text::DialogState& state, int x, int y,
                               const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                               retropp::PaletteId shadow);

}  // namespace ostinato::render
