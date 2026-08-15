// The map's background-size codes (MapProperties +23..+24): two bytes packing a
// width/height size code per background. SetScrollClip reads them
// (scroll.asm:297-341) as 2-bit fields:
//   * byte +23: bg1 width (bits 6-7), bg1 height (4-5), bg2 width (2-3),
//     bg2 height (0-1)
//   * byte +24: bg3 width (bits 6-7), bg3 height (4-5); bits 0-2 are written to
//     the per-bg flag bytes ($0591-3) but never read (dead state; preserved raw)
// Each 2-bit code (0-4 in the data) selects a tile-count via the ScrollClipTbl
// mask {$0f,$1f,$3f,$7f,$ff} — i.e. 16/32/64/128/256 tiles. sizeof == 2 keeps
// the pair byte-identical to the ROM.
#pragma once

#include <array>
#include <cstdint>

namespace ostinato {

struct MapBgSizes {
    std::array<std::uint8_t, 2> bytes = {0, 0};

    // The tile count for a size code (0-4 -> 16/32/64/128/256).
    static constexpr std::uint16_t tilesForCode(std::uint8_t code) {
        return static_cast<std::uint16_t>(16u << code);
    }

    constexpr std::uint8_t bg1WidthCode() const { return (bytes[0] >> 6) & 0x03; }
    constexpr std::uint8_t bg1HeightCode() const {
        return (bytes[0] >> 4) & 0x03;
    }
    constexpr std::uint8_t bg2WidthCode() const { return (bytes[0] >> 2) & 0x03; }
    constexpr std::uint8_t bg2HeightCode() const {
        return (bytes[0] >> 0) & 0x03;
    }
    constexpr std::uint8_t bg3WidthCode() const { return (bytes[1] >> 6) & 0x03; }
    constexpr std::uint8_t bg3HeightCode() const {
        return (bytes[1] >> 4) & 0x03;
    }

    constexpr std::uint16_t bg1WidthTiles() const {
        return tilesForCode(bg1WidthCode());
    }
    constexpr std::uint16_t bg1HeightTiles() const {
        return tilesForCode(bg1HeightCode());
    }
    constexpr std::uint16_t bg2WidthTiles() const {
        return tilesForCode(bg2WidthCode());
    }
    constexpr std::uint16_t bg2HeightTiles() const {
        return tilesForCode(bg2HeightCode());
    }
    constexpr std::uint16_t bg3WidthTiles() const {
        return tilesForCode(bg3WidthCode());
    }
    constexpr std::uint16_t bg3HeightTiles() const {
        return tilesForCode(bg3HeightCode());
    }

    // Byte +24 bits 0-2: written to the per-bg flag bytes but never read.
    constexpr std::uint8_t deadFlags() const { return bytes[1] & 0x07; }
};

static_assert(sizeof(MapBgSizes) == 2,
              "MapBgSizes must be byte-identical to the 2-byte ROM pair");

}  // namespace ostinato
