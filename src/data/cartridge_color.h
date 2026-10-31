// A color as the cartridge stores one: a 15-bit word, five bits each of red, green and blue, red
// in the low bits and the top bit unused. Every palette the game holds — the backgrounds', the
// monsters', the party's, the windows', the fonts' — is a run of these.
//
//     const CartridgeColor color{0x7FFE};
//     color.red5();     // 30
//     color.toRgba8();  // {247, 255, 255, 255}
#pragma once

#include <cstdint>

#include "retropp/palette.h"

namespace ostinato {

struct CartridgeColor {
    std::uint16_t word = 0;

    static constexpr unsigned kChannelMax = 31;
    static constexpr unsigned kChannelMask = 0x1F;
    static constexpr unsigned kGreenShift = 5;
    static constexpr unsigned kBlueShift = 10;

    constexpr unsigned red5() const { return word & kChannelMask; }
    constexpr unsigned green5() const { return (word >> kGreenShift) & kChannelMask; }
    constexpr unsigned blue5() const { return (word >> kBlueShift) & kChannelMask; }

    // A five-bit channel spread over eight bits, rounded to the nearest value.
    static constexpr std::uint8_t expandChannel(unsigned fiveBit) {
        return static_cast<std::uint8_t>((fiveBit * 255 + kChannelMax / 2) / kChannelMax);
    }

    // The color as the renderer takes it, fully opaque.
    constexpr retropp::Rgba8 toRgba8() const {
        return retropp::Rgba8{.r = expandChannel(red5()),
                              .g = expandChannel(green5()),
                              .b = expandChannel(blue5())};
    }

    constexpr bool operator==(const CartridgeColor&) const = default;
};

static_assert(sizeof(CartridgeColor) == 2, "CartridgeColor must be byte-identical to a ROM color word");

// The color word at `at` in a run of color bytes, low byte first.
constexpr CartridgeColor colorAt(const std::uint8_t* bytes, unsigned at) {
    return CartridgeColor{static_cast<std::uint16_t>(bytes[2 * at] | bytes[2 * at + 1] << 8)};
}

}  // namespace ostinato
