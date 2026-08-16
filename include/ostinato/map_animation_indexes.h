// The map's animation-index byte (MapProperties +27, copied to $053b). Packs
// the bg1/bg2 animation index with the bg3 animation index. InitBG12Anim
// (anim.asm:288-293) masks the low 5 bits (and #$1f) as the bg1/bg2 animation
// index into the MapBGAnimProp streams; InitBG3Anim (anim.asm:397-408) reads
// the high 3 bits (and #$e0 lsr5) as the bg3 animation index, where 0 means no
// bg3 animation and any other value n selects MapBG3AnimProp row n-1.
// sizeof == 1 keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

namespace ostinato {

struct MapAnimationIndexes {
    std::uint8_t bits = 0;

    constexpr MapAnimationIndexes() = default;
    explicit constexpr MapAnimationIndexes(std::uint8_t raw) : bits(raw) {}

    // Bits 0-4: the bg1/bg2 animation index (into the MapBGAnimProp streams).
    constexpr std::uint8_t bgAnimIndex() const {
        return static_cast<std::uint8_t>(bits & 0x1F);
    }
    // Bits 5-7: the bg3 animation selector; 0 = none, else selects row (n-1).
    constexpr std::uint8_t bg3AnimSelector() const {
        return static_cast<std::uint8_t>((bits & 0xE0) >> 5);
    }
    // Whether this map has a bg3 animation (selector != 0).
    constexpr bool hasBg3Animation() const { return bg3AnimSelector() != 0; }
    // The bg3 animation row (valid only when hasBg3Animation()).
    constexpr std::uint8_t bg3AnimIndex() const {
        return static_cast<std::uint8_t>(bg3AnimSelector() - 1);
    }
};

static_assert(sizeof(MapAnimationIndexes) == 1,
              "MapAnimationIndexes must be byte-identical to the ROM byte");

}  // namespace ostinato
