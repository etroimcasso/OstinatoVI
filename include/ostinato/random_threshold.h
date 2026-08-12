// A cumulative random threshold (DanceRateTbl c2/05ce, RandBitRateTbl c2/5269).
// The battle engine rolls a random byte and walks a ladder of these, counting
// how many it lands at or above; the share belonging to one outcome is the gap
// between its threshold and the next one up. Carrying the raw byte keeps the
// probabilities byte-identical to the ROM — rewriting them as fractions would
// change the outcome distribution at the edges.
#pragma once

#include <cstdint>

namespace ostinato {

struct RandomThreshold {
    std::uint8_t bits = 0;

    constexpr RandomThreshold() = default;
    explicit constexpr RandomThreshold(std::uint8_t raw) : bits(raw) {}

    // The compare value itself: a roll passes this rung when roll >= value().
    constexpr std::uint8_t value() const { return bits; }
};

static_assert(sizeof(RandomThreshold) == 1,
              "RandomThreshold must be byte-identical to the ROM byte");

}  // namespace ostinato
