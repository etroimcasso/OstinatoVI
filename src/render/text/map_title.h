// MapTitle — the map's name, centred on the first line while the title is up.
//
//     SpriteLayer("map-title", kDialogueTextZ, {MapTitle(state, textX, textY, glyphs, ink, shadow)});
//
// (x, y) is the top-left corner of the text area on screen; the title's glyphs were placed centred
// in it when the map opened (text::showMapTitle). Each is drawn with its shadow (Glyph), keyed
// title:<n> by its place in the title. Returns nothing once the title is down.
#pragma once

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"
#include "text/dialogue.h"

namespace ostinato::render {

[[nodiscard]] Sprites MapTitle(const text::DialogueState& state, int x, int y,
                               const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                               retropp::PaletteId shadow);

}  // namespace ostinato::render
