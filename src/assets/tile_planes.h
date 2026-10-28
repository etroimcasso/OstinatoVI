// The cartridge's tile formats, turned into indexed pixels.
//
// Every piece of artwork the cartridge holds is 8 x 8 tiles stored as bit planes: each plane is
// eight bytes, one per row, the leftmost pixel in the top bit, and a pixel's color index is one
// bit from each plane. The planes are interleaved in pairs, so a tile's first sixteen bytes hold
// planes 0 and 1 row by row and the next sixteen planes 2 and 3. A two-plane tile has four
// colors, a three-plane one eight, a four-plane one sixteen.
//
//     text::IndexedImage sheet{.width = 64, .height = 8, .pixels(64 * 8, 0)};
//     drawTile(sheet, tileBytes, TileDepth::FourPlanes, /*x=*/8, /*y=*/0);
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "text/font.h"

namespace ostinato::assets {

// How many bit planes a tile is stored in.
enum class TileDepth : std::uint8_t {
    TwoPlanes = 2,    // 16 bytes, colors 0-3: the small font
    ThreePlanes = 3,  // 24 bytes, colors 0-7: some monsters
    FourPlanes = 4,   // 32 bytes, colors 0-15: backgrounds, the party, most monsters
};

inline constexpr int kTilePx = 8;

// How many bytes one tile takes at `depth`.
constexpr std::size_t tileBytes(TileDepth depth) {
    return static_cast<std::size_t>(depth) * kTilePx;
}

// Write the tile `bytes` holds, at `depth`, into `image` with its top-left pixel at (x, y).
// PRECONDITION (asserted): `bytes` holds tileBytes(depth) bytes and the tile fits in the image.
void drawTile(text::IndexedImage& image, std::span<const std::uint8_t> bytes, TileDepth depth, int x,
              int y);

// An image of `width` by `height` pixels, every pixel 0.
text::IndexedImage blankImage(int width, int height);

}  // namespace ostinato::assets
