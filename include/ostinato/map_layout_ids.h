// The map's tilemap-layout id group (MapProperties +13..+16): four bytes holding
// a packed little-endian bit-stream of three 10-bit layout indices, with two
// spare high bits. LoadMapTilemap reads them with word reads and shifts
// (map.asm:1753-1833):
//   * bg1 layout: bits 0-9   (and #$03ff)
//   * bg2 layout: bits 10-19 (word@+14, >>2 & $3ff)
//   * bg3 layout: bits 20-29 (word@+15, >>4 & $3ff)
// A layout index of 0 means the layer has no tilemap. Bits 30-31 have no
// consumer and are preserved raw. The indices select pack-side sub_tilemap sets
// (Phase F1); this layer carries the indices only. sizeof == 4 keeps the group
// byte-identical to the ROM.
#pragma once

#include <array>
#include <cstdint>

namespace ostinato {

struct MapLayoutIds {
    std::array<std::uint8_t, 4> bytes = {0, 0, 0, 0};

    // The four bytes as a 32-bit little-endian value.
    constexpr std::uint32_t raw() const {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            value |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
        }
        return value;
    }

    // Bits 0-9: the bg1 tilemap-layout index (0 = no bg1 layer).
    constexpr std::uint16_t bg1Layout() const { return (raw() >> 0) & 0x3FF; }
    // Bits 10-19: the bg2 tilemap-layout index (0 = no bg2 layer).
    constexpr std::uint16_t bg2Layout() const { return (raw() >> 10) & 0x3FF; }
    // Bits 20-29: the bg3 tilemap-layout index (0 = no bg3 layer).
    constexpr std::uint16_t bg3Layout() const { return (raw() >> 20) & 0x3FF; }
    // Bits 30-31: no consumer; preserved raw.
    constexpr std::uint8_t spareBits() const { return (raw() >> 30) & 0x03; }
};

static_assert(sizeof(MapLayoutIds) == 4,
              "MapLayoutIds must be byte-identical to the 4-byte ROM group");
static_assert(alignof(MapLayoutIds) == 1,
              "MapLayoutIds must be alignment-1 to sit inside the packed "
              "MapProperties record at offset +13");

}  // namespace ostinato
