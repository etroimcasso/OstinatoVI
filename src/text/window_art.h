// The dialog box's artwork and colors, as the player's cartridge holds them: eight wallpapers of 28
// tiles each, and each wallpaper's palette. Nothing here is compiled in — the cartridge is read once
// at launch (assets::ingestCartridge) and this is the view over what it held.
//
// A wallpaper's art draws through seven colors: every pixel is 1 to 7, and pixel value v takes the
// window's v-th color. The colors are the player's to choose; the cartridge's are the defaults.
//
//     const WindowArt& art = content.windowArt;
//     const IndexedImage sheet = buildWindowSheet(art, WallpaperId::WALLPAPER_0);
//     const WindowColors colors = defaultWindowColors(art, WallpaperId::WALLPAPER_0);
//     // tile WindowTile::X is cell static_cast<int>(X) of the sheet
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "retropp/palette.h"

#include "ostinato/wallpaper_id.h"
#include "ostinato/window_tile.h"
#include "text/font.h"

namespace ostinato::text {

// --- the cartridge's artwork --------------------------------------------------------------------

// A wallpaper is kWindowTileCount tiles of kWindowTileBytes each: eight rows of 8 pixels in four
// bit planes, the first two planes interleaved row by row in the first 16 bytes and the other two
// in the next 16. The art uses the first three planes; the fourth is never read.
inline constexpr std::size_t kWindowTileBytes = 32;
inline constexpr std::size_t kWallpaperBytes = kWindowTileCount * kWindowTileBytes;
inline constexpr int kWindowTilePx = 8;

// Each wallpaper's palette is 16 colors of two bytes, five bits each of red, green and blue, red in
// the low bits. The window draws through colors 1 to kWindowColorCount of it.
inline constexpr std::size_t kWallpaperPaletteColors = 16;
inline constexpr std::size_t kColorBytes = 2;
inline constexpr std::size_t kWallpaperPaletteBytes = kWallpaperPaletteColors * kColorBytes;
inline constexpr std::size_t kWindowColorCount = 7;

// The window's colors, first to seventh: the colors pixel values 1 to 7 draw in.
using WindowColors = std::array<retropp::Rgba8, kWindowColorCount>;

// The artwork and palettes, as views over the cartridge's bytes. The bytes must outlive this
// object.
struct WindowArt {
    std::span<const std::uint8_t> tiles;     // kWallpaperCount wallpapers, kWallpaperBytes each
    std::span<const std::uint8_t> palettes;  // kWallpaperCount palettes, kWallpaperPaletteBytes each
};

// --- the sheet ----------------------------------------------------------------------------------

// The wallpaper sheet puts all kWindowTileCount tiles on one row, tile t in cell t. A pixel is a
// color number 1 to 7; no pixel is blank.
IndexedImage buildWindowSheet(const WindowArt& art, WallpaperId wallpaper);

// The cartridge's colors for `wallpaper`: colors 1 to 7 of its palette.
WindowColors defaultWindowColors(const WindowArt& art, WallpaperId wallpaper);

}  // namespace ostinato::text
