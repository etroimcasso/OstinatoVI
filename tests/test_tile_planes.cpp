// The cartridge's planar tile formats, decoded from hand-built tiles: which byte holds which plane,
// and which bit which pixel.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "assets/tile_planes.h"

namespace ostinato::assets {
namespace {

// Row 0 of plane 0 is $80: the leftmost pixel has bit 0 set. Row 1 of plane 1 is $01: the rightmost
// pixel of row 1 has bit 1 set.
TEST(TilePlanes, TwoPlanesInterleaveRowByRow) {
    std::array<std::uint8_t, 16> tile{};
    tile[0] = 0x80;  // row 0, plane 0
    tile[3] = 0x01;  // row 1, plane 1
    text::IndexedImage image = blankImage(8, 8);
    drawTile(image, tile, TileDepth::TwoPlanes, 0, 0);
    EXPECT_EQ(image.at(0, 0), 1);
    EXPECT_EQ(image.at(1, 0), 0);
    EXPECT_EQ(image.at(7, 1), 2);
    EXPECT_EQ(image.at(7, 0), 0);
}

// The second pair of planes follows the first sixteen bytes; plane 3's bit is worth 8.
TEST(TilePlanes, FourPlanesPutTheSecondPairAfterTheFirst) {
    std::array<std::uint8_t, 32> tile{};
    tile[16] = 0x80;  // row 0, plane 2
    tile[17] = 0x80;  // row 0, plane 3
    tile[0] = 0x80;   // row 0, plane 0
    text::IndexedImage image = blankImage(16, 16);
    drawTile(image, tile, TileDepth::FourPlanes, 8, 8);
    EXPECT_EQ(image.at(8, 8), 1 | 4 | 8);
    EXPECT_EQ(image.at(0, 0), 0);
}

// A three-plane tile's third plane is alone after the pair, one byte a row.
TEST(TilePlanes, ThreePlanesKeepTheThirdAloneOneByteARow) {
    std::array<std::uint8_t, 24> tile{};
    tile[16 + 2] = 0x40;  // row 2, plane 2: pixel 1
    text::IndexedImage image = blankImage(8, 8);
    drawTile(image, tile, TileDepth::ThreePlanes, 0, 0);
    EXPECT_EQ(image.at(1, 2), 4);
    EXPECT_EQ(image.at(0, 2), 0);
    EXPECT_EQ(tileBytes(TileDepth::ThreePlanes), 24u);
    EXPECT_EQ(tileBytes(TileDepth::FourPlanes), 32u);
    EXPECT_EQ(tileBytes(TileDepth::TwoPlanes), 16u);
}

}  // namespace
}  // namespace ostinato::assets
