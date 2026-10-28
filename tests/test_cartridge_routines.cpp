// The cartridge's decompressor, placed on a machine of its own and called: the routine's bytes read
// from the cartridge, the first tilemap decoded, and the stream helper's edges.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "assets/cartridge_routines.h"
#include "assets/rom_reader.h"
#include "data/rom_regions.h"
#include "ostinato/game_version.h"
#include "ostinato/rom_asset.h"
#include "vanilla_rom.h"

namespace ostinato::assets {
namespace {

// The first tilemap pointer names the first tilemap, which decodes to the 32 x 32 cells of a
// background's map.
constexpr std::size_t kTilemapBytes = 0x800;

std::uint16_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | bytes[at + 1] << 8);
}

class CartridgeRoutinesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    static test::IngestedCartridge* cartridge_;
};

test::IngestedCartridge* CartridgeRoutinesTest::cartridge_ = nullptr;

TEST_F(CartridgeRoutinesTest, TheRoutineIsPlacedFromTheCartridgesBytes) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const std::vector<std::uint8_t> bytes =
        test::romSlice(cartridge_->image, RomAsset::DECOMPRESS_ROUTINE);
    ASSERT_EQ(bytes.size(), 147u);
    EXPECT_NO_THROW(CartridgeRoutines{bytes});
}

TEST_F(CartridgeRoutinesTest, BytesThatAreNotTheRoutineAreRefused) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    std::vector<std::uint8_t> bytes = test::romSlice(cartridge_->image, RomAsset::DECOMPRESS_ROUTINE);
    EXPECT_THROW(CartridgeRoutines{std::span<const std::uint8_t>{bytes}.first(146)},
                 std::invalid_argument);
    bytes[0] ^= 0xFF;
    EXPECT_THROW(CartridgeRoutines{bytes}, std::invalid_argument);
}

// The first tilemap, found through the pointer table, decodes to exactly one 32 x 32 map.
TEST_F(CartridgeRoutinesTest, TheFirstTilemapDecodesToOneMap) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const HiRomImage image{cartridge_->image};
    const auto pointers = romRegion(RomAsset::BATTLE_BG_TILES_PTRS, Language::EN);
    const auto tilemaps = romRegion(RomAsset::BATTLE_BG_TILES, Language::EN);
    ASSERT_TRUE(pointers && tilemaps);
    const std::span<const std::uint8_t> table = image.read(*pointers);
    const std::uint32_t first = (tilemaps->at & 0xFF0000u) | word(table, 0);
    ASSERT_EQ(first, tilemaps->at);
    const std::span<const std::uint8_t> block = image.read(*tilemaps);
    const std::span<const std::uint8_t> stream = compressedStream(block);
    EXPECT_GT(stream.size(), 2u);
    EXPECT_LT(stream.size(), kTilemapBytes);

    CartridgeRoutines routines{test::romSlice(cartridge_->image, RomAsset::DECOMPRESS_ROUTINE)};
    const std::vector<std::uint8_t> decoded = routines.decompress(stream);
    ASSERT_EQ(decoded.size(), kTilemapBytes);
    // A tilemap cell names a tile below 1024 and one of the background's three palettes, 5 to 7.
    for (std::size_t cell = 0; cell < kTilemapBytes; cell += 2) {
        const std::uint16_t value = word(decoded, cell);
        const unsigned palette = (value >> 10) & 7;
        EXPECT_GE(palette, 5u) << "cell " << cell / 2;
        EXPECT_LE(palette, 7u) << "cell " << cell / 2;
    }
    // Decoding the same stream again gives the same bytes: nothing of one call leaks into the next.
    EXPECT_EQ(routines.decompress(stream), decoded);
}

TEST(CompressedStream, IsItsFirstWordOfBytes) {
    const std::array<std::uint8_t, 6> bytes{0x04, 0x00, 0xAA, 0xBB, 0xCC, 0xDD};
    const std::span<const std::uint8_t> stream = compressedStream(bytes);
    EXPECT_EQ(stream.size(), 4u);
    EXPECT_EQ(stream[3], 0xBB);
}

TEST(CompressedStream, RefusesAWordThatRunsPastTheBytesOrInsideItsHeader) {
    const std::array<std::uint8_t, 3> tooLong{0x09, 0x00, 0xAA};
    EXPECT_THROW((void)compressedStream(tooLong), std::out_of_range);
    const std::array<std::uint8_t, 1> noHeader{0x02};
    EXPECT_THROW((void)compressedStream(noHeader), std::out_of_range);
    const std::array<std::uint8_t, 2> insideHeader{0x01, 0x00};
    EXPECT_THROW((void)compressedStream(insideHeader), std::invalid_argument);
}

}  // namespace
}  // namespace ostinato::assets
