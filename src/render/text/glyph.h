// Glyph — one letter of the dialog font and its drop shadow.
//
// The shadow is the same artwork, drawn one pixel right and one down through a black palette and
// beneath every piece of ink in the layer; the letter is drawn at (x, y) through the font color.
// Where a letter and any shadow overlap — its own or a neighbor's — the letter shows.
//
//     Sprites s = Glyph("dlg:3:0:5", 0x41, 40, 172, glyphs, ink, shadow);
//     // s[0] is the shadow (key "dlg:3:0:5:shadow"), s[1] the letter
#pragma once

#include <cstdint>
#include <string_view>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/types.h"

namespace ostinato::render {

// `glyph` is a dialog glyph code; (x, y) is where the top-left of its artwork goes. `glyphs` is
// the uploaded glyph sheet (text::buildGlyphSheet), carved into kGlyphCellPx squares.
// PRECONDITION (asserted): the sheet has a cell for `glyph`.
[[nodiscard]] Sprites Glyph(std::string_view key, std::uint16_t glyph, int x, int y,
                            const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                            retropp::PaletteId shadow);

}  // namespace ostinato::render
