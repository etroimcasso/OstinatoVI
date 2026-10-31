// The small font, as the player's cartridge holds it: the 256 glyphs of 8 x 8 the battle's HUD,
// menus and damage numerals are drawn in, and the eight four-color palettes they draw through.
//
// A glyph is a two-plane tile, every pixel a color 0-3. The battle loads sixteen of the font's
// thirty-two palette colors as its first four palettes, with the player's chosen font color as the
// first palette's third entry, and the last twelve as palettes five to seven; palette four is left
// black. Which palette a glyph draws through is part of each piece of HUD text.
//
//     const text::IndexedImage sheet = text::buildSmallFontSheet(content.smallFont);
//     const text::SmallFontPalettes palettes = text::smallFontPalettes(content.smallFontColors, fontColor);
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "data/cartridge_color.h"
#include "text/font.h"

namespace ostinato::text {

inline constexpr std::size_t kSmallFontGlyphCount = 256;
inline constexpr std::size_t kSmallFontGlyphBytes = 16;
inline constexpr std::size_t kSmallFontBytes = kSmallFontGlyphCount * kSmallFontGlyphBytes;
inline constexpr int kSmallGlyphPx = 8;
inline constexpr int kSmallFontSheetColumns = 16;

// The font's palette colors as the cartridge holds them, and how the battle lays them out.
inline constexpr std::size_t kSmallFontColorCount = 32;
inline constexpr std::size_t kSmallFontPaletteCount = 8;
inline constexpr std::size_t kSmallFontPaletteColors = 4;
inline constexpr std::size_t kSmallFontLoadedFirst = 16;   // colors 0-15 become palettes 0-3
inline constexpr std::size_t kSmallFontLoadedLastFrom = 20; // colors 20-31 become palettes 5-7
inline constexpr std::size_t kFontColorEntry = 3;          // palette 0's entry the player's color takes

using SmallFontPalette = std::array<CartridgeColor, kSmallFontPaletteColors>;
using SmallFontPalettes = std::array<SmallFontPalette, kSmallFontPaletteCount>;

// The glyph sheet: sixteen glyphs a row, glyph g in row g / 16, column g % 16, every pixel 0-3.
// PRECONDITION (asserted): `font` holds kSmallFontBytes bytes.
IndexedImage buildSmallFontSheet(std::span<const std::uint8_t> font);

// The eight palettes the battle draws the font through, from the cartridge's 32 colors
// (`colors`, BATTLE_FONT_PAL) and the player's font color.
// PRECONDITION (asserted): `colors` holds kSmallFontColorCount * 2 bytes.
SmallFontPalettes smallFontPalettes(std::span<const std::uint8_t> colors, CartridgeColor fontColor);

}  // namespace ostinato::text
