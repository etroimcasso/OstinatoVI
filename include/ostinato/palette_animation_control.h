// A palette-animation slot's control byte (byte 0 of a 6-byte slot, copied to
// $10ea). It packs the slot's disabled flag, its animation type, and its step
// count. UpdatePalAnim (anim.asm:74-118) tests bit 7 first (bmi -> skip the
// disabled slot), then masks the type field (and #$f0, lsr4 -> the four-way
// dispatch); the low nibble is the color-counter modulus (step count).
// sizeof == 1 keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

#include "ostinato/palette_animation_type.h"

namespace ostinato {

struct PaletteAnimationControl {
    std::uint8_t bits = 0;

    constexpr PaletteAnimationControl() = default;
    explicit constexpr PaletteAnimationControl(std::uint8_t raw) : bits(raw) {}

    // Bit 7: the slot is disabled (skipped every frame).
    constexpr bool disabled() const { return (bits & 0x80) != 0; }
    // Bits 4-6: the animation type dispatched each frame.
    constexpr PaletteAnimationType type() const {
        return static_cast<PaletteAnimationType>((bits & 0x70) >> 4);
    }
    // Bits 0-3: the color-counter modulus (step count).
    constexpr std::uint8_t stepCount() const {
        return static_cast<std::uint8_t>(bits & 0x0F);
    }
};

static_assert(sizeof(PaletteAnimationControl) == 1,
              "PaletteAnimationControl must be byte-identical to the ROM byte");

}  // namespace ostinato
