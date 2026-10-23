#include "render/text/glyph.h"

#include <cassert>
#include <string>

#include "render/layers.h"
#include "text/font.h"
#include "text/layout.h"

namespace ostinato::render {

Sprites Glyph(std::string_view key, std::uint16_t glyph, int x, int y,
              const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
              retropp::PaletteId shadow) {
    const std::size_t slot = text::glyphSlot(glyph);
    assert(slot < glyphs.tileCount() && "the glyph sheet has no cell for this glyph");
    const retropp::AssetSlot& cell = glyphs[slot];

    std::string shadowKey{key};
    shadowKey += kShadowKeySuffix;
    return {
        retropp::Sprite{.key = std::move(shadowKey),
                        .x = x + text::kShadowDx,
                        .y = y + text::kShadowDy,
                        .z = kShadowSpriteZ,
                        .size = cell.dimensions,
                        .atlas = glyphs.atlasId,
                        .tile = cell.tile,
                        .palette = shadow},
        retropp::Sprite{.key = key,
                        .x = x,
                        .y = y,
                        .z = kInkSpriteZ,
                        .size = cell.dimensions,
                        .atlas = glyphs.atlasId,
                        .tile = cell.tile,
                        .palette = ink},
    };
}

}  // namespace ostinato::render
