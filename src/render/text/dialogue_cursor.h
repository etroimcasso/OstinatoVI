// DialogueCursor — the arrow beside the selected choice.
//
//     SpriteLayer("dialogue", kDialogueTextZ,
//                 {DialogueText(...), DialogueCursor(state, textX, textY, arrow, ink, shadow)});
//
// (x, y) is the top-left corner of the text area on screen. When the engine shows the arrow
// (state.cursor), it is drawn at the left edge of the selected choice's column, at a glyph's height
// in the line, with its shadow one pixel to the right through the black palette. `arrow` is the
// uploaded arrow sheet (text::buildCursorSheet), one cell.
//
// The arrow is keyed by the choice it points at, so moving the selection puts it straight at the
// new choice. Returns nothing while there is no arrow, and on a cartridge with no arrow artwork
// (an empty `arrow` sheet).
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"
#include "text/dialogue.h"

namespace ostinato::render {

[[nodiscard]] Sprites DialogueCursor(const text::DialogueState& state, int x, int y,
                                     const retropp::AtlasManifest& arrow, retropp::PaletteId ink,
                                     retropp::PaletteId shadow);

}  // namespace ostinato::render
