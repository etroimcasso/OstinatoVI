#include "text/small_font.h"

#include <cassert>

#include "assets/tile_planes.h"

namespace ostinato::text {

IndexedImage buildSmallFontSheet(std::span<const std::uint8_t> font) {
    assert(font.size() == kSmallFontBytes && "the small font is 256 glyphs of 16 bytes");
    IndexedImage sheet = assets::blankImage(
        kSmallFontSheetColumns * kSmallGlyphPx,
        static_cast<int>(kSmallFontGlyphCount / kSmallFontSheetColumns) * kSmallGlyphPx);
    for (std::size_t glyph = 0; glyph < kSmallFontGlyphCount; ++glyph) {
        assets::drawTile(sheet, font.subspan(glyph * kSmallFontGlyphBytes, kSmallFontGlyphBytes),
                         assets::TileDepth::TwoPlanes,
                         static_cast<int>(glyph % kSmallFontSheetColumns) * kSmallGlyphPx,
                         static_cast<int>(glyph / kSmallFontSheetColumns) * kSmallGlyphPx);
    }
    return sheet;
}

SmallFontPalettes smallFontPalettes(std::span<const std::uint8_t> colors, CartridgeColor fontColor) {
    assert(colors.size() == kSmallFontColorCount * 2 && "the font palettes are 32 colors");
    SmallFontPalettes palettes{};
    for (std::size_t color = 0; color < kSmallFontLoadedFirst; ++color) {
        palettes[color / kSmallFontPaletteColors][color % kSmallFontPaletteColors] =
            colorAt(colors.data(), static_cast<unsigned>(color));
    }
    for (std::size_t color = kSmallFontLoadedLastFrom; color < kSmallFontColorCount; ++color) {
        palettes[color / kSmallFontPaletteColors][color % kSmallFontPaletteColors] =
            colorAt(colors.data(), static_cast<unsigned>(color));
    }
    palettes[0][kFontColorEntry] = fontColor;
    return palettes;
}

}  // namespace ostinato::text
