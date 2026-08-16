// The map's graphics/tileset id group (MapProperties +7..+12): six bytes holding
// a packed 48-bit little-endian bit-stream of seven indices, with no spare bits.
// LoadMapGfx reads them as overlapping word reads with shifts (map.asm:1512-1544
// for gfx1-4, 1619-1623 for bg3 gfx, 1678-1716 for the two tilesets):
//   * gfx1-4    : bits 0-6, 7-13, 14-20, 21-27 (7 bits each) -> MapGfxPtrs
//   * bg3 gfx   : bits 28-33 (6 bits) -> MapGfxBG3Ptrs
//   * tileset1-2: bits 34-40, 41-47 (7 bits each) -> MapTilesetPtrs
// The indices select pack-side graphics/tileset sets (Phase F1); this layer
// carries the indices only. sizeof == 6 keeps the group byte-identical to the
// ROM.
#pragma once

#include <array>
#include <cstdint>

namespace ostinato {

struct MapGraphicsIds {
    std::array<std::uint8_t, 6> bytes = {0, 0, 0, 0, 0, 0};

    // The six bytes as a 48-bit little-endian value.
    constexpr std::uint64_t raw() const {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
        }
        return value;
    }

    constexpr std::uint8_t gfx1() const { return (raw() >> 0) & 0x7F; }
    constexpr std::uint8_t gfx2() const { return (raw() >> 7) & 0x7F; }
    constexpr std::uint8_t gfx3() const { return (raw() >> 14) & 0x7F; }
    constexpr std::uint8_t gfx4() const { return (raw() >> 21) & 0x7F; }
    constexpr std::uint8_t bg3Gfx() const { return (raw() >> 28) & 0x3F; }
    constexpr std::uint8_t tileset1() const { return (raw() >> 34) & 0x7F; }
    constexpr std::uint8_t tileset2() const { return (raw() >> 41) & 0x7F; }
};

static_assert(sizeof(MapGraphicsIds) == 6,
              "MapGraphicsIds must be byte-identical to the 6-byte ROM group");
static_assert(alignof(MapGraphicsIds) == 1,
              "MapGraphicsIds must be alignment-1 to sit inside the packed "
              "MapProperties record at offset +7");

}  // namespace ostinato
