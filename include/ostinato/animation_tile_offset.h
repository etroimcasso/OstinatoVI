// An animation-graphics tile offset: the 16-bit first field of an AttackGfxProp
// row (the "tile/graphics" word). LoadAnimGfx (btlgfx_main.asm:24304) branches
// on bit 15 — "branch if 2bpp graphics" — so bit 15 is the graphics-depth flag
// and the low 15 bits are the tile offset. Unlike AnimationRef there is no
// $ffff sentinel here (every AttackGfxProp row names real graphics).
#pragma once

#include <cstdint>

namespace ostinato {

struct AnimationTileOffset {
    std::uint16_t raw = 0;

    // Bit 15: the graphics are 2bpp rather than 4bpp (LoadAnimGfx bmi at
    // btlgfx_main.asm:24304).
    constexpr bool is2bpp() const { return (raw & 0x8000) != 0; }
    // The tile offset (low 15 bits).
    constexpr std::uint16_t offset() const {
        return static_cast<std::uint16_t>(raw & 0x7FFF);
    }

    // Builders so every row names a decimal offset and, when 2bpp, says so —
    // never a raw hex word. Byte-identical to the ROM word.
    static constexpr AnimationTileOffset of(std::uint16_t offset) {
        return AnimationTileOffset{offset};
    }
    static constexpr AnimationTileOffset of2bpp(std::uint16_t offset) {
        return AnimationTileOffset{static_cast<std::uint16_t>(offset | 0x8000)};
    }
};

static_assert(sizeof(AnimationTileOffset) == 2,
              "AnimationTileOffset must be byte-identical to the ROM word");
static_assert(
    AnimationTileOffset::of(0x0100).raw == 0x0100 &&
        AnimationTileOffset::of(0x0100).offset() == 0x0100 &&
        !AnimationTileOffset::of(0x0100).is2bpp() &&
        AnimationTileOffset::of2bpp(0x0100).raw == 0x8100 &&
        AnimationTileOffset::of2bpp(0x0100).offset() == 0x0100 &&
        AnimationTileOffset::of2bpp(0x0100).is2bpp(),
    "AnimationTileOffset builders must round-trip the offset and the 2bpp flag");

}  // namespace ostinato
