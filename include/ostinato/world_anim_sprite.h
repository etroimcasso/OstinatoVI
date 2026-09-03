// One hardware sprite inside a world-map animation frame: where it sits
// relative to the object being drawn, which tile it shows, and how that tile is
// coloured, layered and mirrored.
//
// The four bytes are the cartridge's own, stored untouched and named through
// accessors, so a caller asks a question instead of masking. Both world draw
// routines copy bytes 2 and 3 into the sprite table as a single 16-bit read
// (world/sprite.asm:423-489, :495-554), which is why they stay adjacent and
// this type never reorders them.
#pragma once

#include <cstdint>
#include <type_traits>

namespace ostinato {

// A sprite row as the cartridge stores it (world/world_anim.asm:14-24).
struct WorldAnimSprite {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t tileLow = 0;
    std::uint8_t attributes = 0;

    // Offsets from the object's own position, in pixels. Both are signed: a
    // sprite sits above and to the left of its object as often as below and to
    // the right.
    constexpr std::int8_t offsetX() const { return static_cast<std::int8_t>(x); }
    constexpr std::int8_t offsetY() const { return static_cast<std::int8_t>(y); }

    // The tile shown, 0-511. The ninth bit is the attribute byte's lowest.
    constexpr std::uint16_t tileIndex() const {
        return static_cast<std::uint16_t>(tileLow |
                                          ((attributes & 0x01) << 8));
    }

    // Which of the eight sprite palettes colours the tile.
    constexpr std::uint8_t paletteIndex() const {
        return static_cast<std::uint8_t>((attributes >> 1) & 0x07);
    }

    // How far forward the tile draws against the background layers, 0-3.
    constexpr std::uint8_t layerPriority() const {
        return static_cast<std::uint8_t>((attributes >> 4) & 0x03);
    }

    constexpr bool flippedHorizontally() const {
        return (attributes & 0x40) != 0;
    }
    constexpr bool flippedVertically() const {
        return (attributes & 0x80) != 0;
    }
};

// The type is a view onto cartridge bytes, so it must be exactly those bytes
// and must be readable at any offset inside a record.
static_assert(sizeof(WorldAnimSprite) == 4,
              "a sprite row is the cartridge's four bytes and nothing else");
static_assert(alignof(WorldAnimSprite) == 1,
              "a sprite row is read wherever a record puts it, so it cannot "
              "carry an alignment requirement");
static_assert(std::is_trivially_copyable_v<WorldAnimSprite>,
              "a sprite row is copied straight out of the cartridge");

}  // namespace ostinato
