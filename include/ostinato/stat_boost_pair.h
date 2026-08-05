// One nibble-packed stat-boost byte of an item-properties record (+16 =
// vigor/speed, +17 = stamina/mag.pwr). Each nibble is a signed boost in
// -7..+7: bits 0-2 carry the magnitude and bit 3 the sign, decoded by the
// battle consumer as "if bit 3: eor #$fff7, inc" (battle_main.asm:2498-2512)
// — i.e. $9..$F decode to -1..-7 (and $8 to -0 == 0). The menu draws the low
// nibble first (vigor / stamina rows, then speed / mag.pwr —
// src/menu/item.asm:1583-1616), so first() is the low nibble. sizeof == 1
// keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

namespace ostinato {

struct StatBoostPair {
    std::uint8_t packed = 0;

    static constexpr int decode(std::uint8_t nibble) {
        return (nibble & 0x8) ? -(nibble & 0x7) : nibble;
    }

    // The low nibble — vigor at +16, stamina at +17.
    constexpr int first() const { return decode(packed & 0x0F); }

    // The high nibble — speed at +16, mag.pwr at +17.
    constexpr int second() const { return decode(packed >> 4); }

    // Builder from the two decoded boosts. The $8 negative-zero nibble is not
    // producible here (0 packs as $0); the parser hard-errors if the corpus
    // ever carries one, so every emitted row round-trips to its ROM byte.
    static constexpr StatBoostPair of(int first, int second) {
        const auto pack = [](int v) {
            return static_cast<std::uint8_t>(
                v < 0 ? (0x8 | (-v & 0x7)) : (v & 0x7));
        };
        return StatBoostPair{
            static_cast<std::uint8_t>(pack(first) | (pack(second) << 4))};
    }
};

static_assert(sizeof(StatBoostPair) == 1,
              "StatBoostPair must be byte-identical to the ROM stat-boost byte");
static_assert(StatBoostPair::of(7, -7).packed == 0xF7 &&
              StatBoostPair::of(-1, 0).packed == 0x09 &&
              StatBoostPair{0xF7}.first() == 7 &&
              StatBoostPair{0xF7}.second() == -7 &&
              StatBoostPair{0x08}.first() == 0,
              "StatBoostPair must round-trip the consumer's nibble transform");

}  // namespace ostinato
