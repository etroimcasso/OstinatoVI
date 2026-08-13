// The animation init/special-function byte: field +10 of an AttackAnimProp row.
// Two consumers read it as a flag-plus-index carrier: CreateThread masks off
// the high bit and dispatches on the low 7 bits (btlgfx_main.asm:26437-26443,
// "and #$7f" then cmp #$05 / #$02), and a bg1 setup path strips the high bit in
// place (:48349-48351, upstream-commented "use bg1 for graphics ???"). The low
// 7 bits are the special-function index; bit 7 is a flag whose runtime meaning
// is uncertain in the source and is left to the animation player. sizeof == 1
// keeps it
// byte-identical to the ROM byte.
#pragma once

#include <cstdint>

namespace ostinato {

struct AnimationInitFunction {
    std::uint8_t bits = 0;

    // The low 7 bits: the init/special-function dispatch index
    // (CreateThread & $7f, btlgfx_main.asm:26438).
    constexpr std::uint8_t index() const {
        return static_cast<std::uint8_t>(bits & 0x7F);
    }
    // Bit 7: a flag stripped at btlgfx_main.asm:48350 (upstream comment "use
    // bg1 for graphics ???"); the runtime meaning belongs to the animation
    // player.
    constexpr bool hasHighBit() const { return (bits & 0x80) != 0; }

    // Builders so every row names a decimal index and, where the flag is set,
    // says so — never a raw hex byte. Byte-identical to the ROM byte.
    static constexpr AnimationInitFunction of(std::uint8_t index) {
        return AnimationInitFunction{index};
    }
    static constexpr AnimationInitFunction withHighBit(std::uint8_t index) {
        return AnimationInitFunction{static_cast<std::uint8_t>(index | 0x80)};
    }
};

static_assert(sizeof(AnimationInitFunction) == 1,
              "AnimationInitFunction must be byte-identical to the ROM byte");
static_assert(
    AnimationInitFunction::of(0x18).bits == 0x18 &&
        AnimationInitFunction::of(0x18).index() == 0x18 &&
        !AnimationInitFunction::of(0x18).hasHighBit() &&
        AnimationInitFunction::withHighBit(0x02).bits == 0x82 &&
        AnimationInitFunction::withHighBit(0x02).index() == 0x02 &&
        AnimationInitFunction::withHighBit(0x02).hasHighBit(),
    "AnimationInitFunction builders must round-trip the index and the high bit");

}  // namespace ostinato
