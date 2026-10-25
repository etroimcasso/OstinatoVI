#include "render/text/dialog_text.h"

#include <array>
#include <string>

#include "render/layers.h"
#include "render/text/glyph.h"
#include "text/layout.h"

namespace ostinato::render {

Sprites DialogText(const text::DialogState& state, int x, int y, int linePitch,
                     const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                     retropp::PaletteId shadow) {
    Sprites sprites;
    if (state.titleShowing) {
        return sprites;
    }
    sprites.reserve(state.placed.size() * 2);

    std::array<int, text::kLinesPerPage> onLine{};
    for (const text::PlacedGlyph& placed : state.placed) {
        const int n = onLine[static_cast<std::size_t>(placed.line)]++;
        const std::string key = std::string{kDialogGlyphKey} + ":" +
                                std::to_string(placed.page) + ":" + std::to_string(placed.line) +
                                ":" + std::to_string(n);
        const int glyphX = x + placed.x - text::kGlyphArtLeftOfPen;
        const int glyphY = y + placed.line * linePitch + text::kGlyphRowOffset;
        for (retropp::Sprite& sprite :
             Glyph(key, placed.glyph, glyphX, glyphY, glyphs, ink, shadow)) {
            sprites.push_back(std::move(sprite));
        }
    }
    return sprites;
}

}  // namespace ostinato::render
