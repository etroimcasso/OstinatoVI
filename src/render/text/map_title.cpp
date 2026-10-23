#include "render/text/map_title.h"

#include <string>

#include "render/layers.h"
#include "render/text/glyph.h"
#include "text/layout.h"

namespace ostinato::render {

Sprites MapTitle(const text::DialogState& state, int x, int y,
                 const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                 retropp::PaletteId shadow) {
    Sprites sprites;
    if (!state.titleShowing) {
        return sprites;
    }
    sprites.reserve(state.placed.size() * 2);

    int n = 0;
    for (const text::PlacedGlyph& placed : state.placed) {
        const std::string key = std::string{kTitleGlyphKey} + ":" + std::to_string(n++);
        const int glyphX = x + placed.x - text::kGlyphArtLeftOfPen;
        const int glyphY = y + placed.line * text::kLineHeight + text::kGlyphRowOffset;
        for (retropp::Sprite& sprite :
             Glyph(key, placed.glyph, glyphX, glyphY, glyphs, ink, shadow)) {
            sprites.push_back(std::move(sprite));
        }
    }
    return sprites;
}

}  // namespace ostinato::render
