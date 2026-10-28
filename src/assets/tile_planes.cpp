#include "assets/tile_planes.h"

#include <cassert>

namespace ostinato::assets {

namespace {

// The two planes of a pair are interleaved row by row; a pair takes this many bytes.
constexpr std::size_t kPlanePairBytes = 2 * kTilePx;

// The bit of plane `plane` for pixel (x, y) of a tile. Planes 0 and 1 are the first pair, planes
// 2 and 3 the second; a three-plane tile's plane 2 is alone after the first pair, one byte a row.
unsigned planeBit(std::span<const std::uint8_t> bytes, TileDepth depth, unsigned plane, int x,
                  int y) {
    std::size_t at = 0;
    if (plane < 2) {
        at = static_cast<std::size_t>(y) * 2 + plane;
    } else if (depth == TileDepth::ThreePlanes) {
        at = kPlanePairBytes + static_cast<std::size_t>(y);
    } else {
        at = kPlanePairBytes + static_cast<std::size_t>(y) * 2 + (plane - 2);
    }
    return (bytes[at] >> (kTilePx - 1 - x)) & 1u;
}

}  // namespace

void drawTile(text::IndexedImage& image, std::span<const std::uint8_t> bytes, TileDepth depth, int x,
              int y) {
    assert(bytes.size() == tileBytes(depth) && "a tile's bytes must match its depth");
    assert(x >= 0 && y >= 0 && x + kTilePx <= image.width && y + kTilePx <= image.height &&
           "the tile must fit in the image");
    const auto planes = static_cast<unsigned>(depth);
    for (int row = 0; row < kTilePx; ++row) {
        for (int column = 0; column < kTilePx; ++column) {
            unsigned value = 0;
            for (unsigned plane = 0; plane < planes; ++plane) {
                value |= planeBit(bytes, depth, plane, column, row) << plane;
            }
            image.pixels[static_cast<std::size_t>(y + row) * static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(x + column)] = static_cast<std::uint8_t>(value);
        }
    }
}

text::IndexedImage blankImage(int width, int height) {
    assert(width >= 0 && height >= 0);
    text::IndexedImage image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    return image;
}

}  // namespace ostinato::assets
