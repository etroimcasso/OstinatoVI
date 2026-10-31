// A battle background's artwork, decoded from the player's cartridge when a battle asks for it.
//
// A background (BattleBackgroundProperties) names up to three graphics blocks, a tilemap and a
// palette. The tilemap and most graphics blocks are compressed streams the cartridge's own
// decompressor unpacks (assets::CartridgeRoutines); the rest of the graphics blocks are field map
// graphics, copied as they are. Nothing here is compiled in: the pointer tables, the blocks, the
// tilemaps and the palettes are read in place out of the cartridge, and a background is decoded
// from them on demand.
//
//     const BattleBackgroundPicture picture = content.battleBackgrounds.load(
//         BattleBackgroundId::FIELD_WOB, *content.routines);
//     picture.tiles.size();            // 32 bytes a tile, in sheet order
//     picture.cells[32 * row + col];   // one tilemap cell, as the cartridge stores it
//     picture.colors[16 * 2 + 3];      // the third color of the background's third palette
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "retropp/memory_region.h"

#include "assets/cartridge_routines.h"
#include "data/cartridge_color.h"
#include "ostinato/battle_background_id.h"

namespace ostinato {

// A tilemap is 32 x 32 cells of two bytes; the battlefield shows its first 19 rows.
inline constexpr int kBackgroundColumns = 32;
inline constexpr int kBackgroundRows = 32;
inline constexpr int kBackgroundRowsShown = 19;
inline constexpr std::size_t kBackgroundTilemapBytes = kBackgroundColumns * kBackgroundRows * 2;

// A graphics block is taken as 4,096 bytes — 128 tiles of 32 — or 8,192 for a double-width first
// block. The blocks are decoded one after another into one buffer the size of a double block and
// taken from its start, so a block whose art decodes to less than the transfer carries the previous
// block's bytes after its own; a field map graphics block fills the whole buffer.
inline constexpr std::size_t kBackgroundTileBytes = 32;
inline constexpr std::size_t kBackgroundBlockTiles = 128;
inline constexpr std::size_t kBackgroundDoubleBlockTiles = 256;
inline constexpr std::size_t kBackgroundBufferBytes = kBackgroundDoubleBlockTiles * kBackgroundTileBytes;

// A background draws through three palettes of sixteen colors.
inline constexpr std::size_t kBackgroundPalettes = 3;
inline constexpr std::size_t kBackgroundPaletteColors = 16;
inline constexpr std::size_t kBackgroundColorCount = kBackgroundPalettes * kBackgroundPaletteColors;

// The first of the three palettes, as the tilemap's cells number them.
inline constexpr unsigned kBackgroundFirstPalette = 5;

// One cell of a tilemap: which tile, which of the eight palettes, flipped which ways, and whether
// it draws in front.
struct BackgroundCell {
    std::uint16_t word = 0;

    constexpr unsigned tile() const { return word & 0x3FF; }
    constexpr unsigned palette() const { return (word >> 10) & 7; }
    constexpr bool drawnInFront() const { return (word & 0x2000) != 0; }
    constexpr bool flippedX() const { return (word & 0x4000) != 0; }
    constexpr bool flippedY() const { return (word & 0x8000) != 0; }

    constexpr bool operator==(const BackgroundCell&) const = default;
};

static_assert(sizeof(BackgroundCell) == 2, "BackgroundCell must be byte-identical to a tilemap word");

// Where a tilemap's tile numbers reach the three blocks: block 1 holds tiles 0-127 (0-255 when
// double-width), block 2 tiles 128-255, block 3 tiles 896-1023. Numbers 256-895 are nobody's.
inline constexpr unsigned kBackgroundBlock2FirstTile = 128;
inline constexpr unsigned kBackgroundBlock3FirstTile = 896;

// A background decoded: its tiles in one sheet, its tilemap, and its 48 colors.
struct BattleBackgroundPicture {
    // Every tile of the blocks the background loads, 32 bytes each: block 1's tiles first, then
    // block 2's, then block 3's. A block the record leaves empty contributes no tiles.
    std::vector<std::uint8_t> tiles;

    // Where each block's first tile sits in `tiles`, or nothing when the block is not loaded.
    std::array<std::optional<std::size_t>, 3> blockFirstTile{};

    // How many tiles each block was taken as: 128, or 256 for a double-width first block.
    std::array<std::size_t, 3> blockTiles{};

    // How many tiles each block's own art decoded to. A block whose art is shorter than its
    // transfer is followed in `tiles` by what the buffer held before it.
    std::array<std::size_t, 3> blockOwnTiles{};

    // The 32 x 32 cells, row by row.
    std::array<BackgroundCell, kBackgroundColumns * kBackgroundRows> cells{};

    // The three palettes' colors, in order.
    std::array<CartridgeColor, kBackgroundColorCount> colors{};

    std::size_t tileCount() const { return tiles.size() / kBackgroundTileBytes; }

    // Which tile of `tiles` a cell's tile number names, or nothing for a number no loaded block
    // reaches. The blocks are loaded in order, each over whatever the one before put at the same
    // numbers, so a number two blocks cover names the later block's tile.
    std::optional<std::size_t> sheetTile(unsigned tileNumber) const;
};

// One stretch of the cartridge handed to the decoder: the bytes and the address they begin at.
struct CartridgeStretch {
    std::uint32_t at = 0;
    std::span<const std::uint8_t> bytes;
};

// The battle backgrounds' art, as views over the cartridge's bytes. The bytes must outlive this
// object.
class BattleBackgroundArt {
public:
    BattleBackgroundArt() = default;

    // `graphicsPointers` is BATTLE_BG_GFX_PTRS, `tilemapPointers` BATTLE_BG_TILES_PTRS,
    // `graphics` BATTLE_BG_GFX, `tilemaps` BATTLE_BG_TILES and `palettes` BATTLE_BG_PAL.
    // `mapGraphics` begins at MAP_GFX and runs to the end of the cartridge: one block's 8,192-byte
    // copy starts inside the family and reads on past its end.
    BattleBackgroundArt(std::span<const std::uint8_t> graphicsPointers,
                        std::span<const std::uint8_t> tilemapPointers, CartridgeStretch graphics,
                        CartridgeStretch tilemaps, CartridgeStretch mapGraphics,
                        std::span<const std::uint8_t> palettes);

    bool valid() const;

    // Decode `background`: its blocks (through `routines` where they are compressed), its tilemap
    // and its colors. Throws std::out_of_range when a pointer leaves the bytes it should land in,
    // and std::runtime_error when a stream decodes to the wrong length.
    BattleBackgroundPicture load(BattleBackgroundId background,
                                 assets::CartridgeRoutines& routines) const;

private:
    std::span<const std::uint8_t> graphicsPointers_;
    std::span<const std::uint8_t> tilemapPointers_;
    CartridgeStretch graphics_;
    CartridgeStretch tilemaps_;
    CartridgeStretch mapGraphics_;
    std::span<const std::uint8_t> palettes_;
};

}  // namespace ostinato
