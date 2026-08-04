// A generic one-byte flag set over a bit-valued enum. The flag enum carries the
// upstream bit values; this template encapsulates the bit math so no call site
// ever open-codes a mask. sizeof == 1 keeps every instantiation byte-identical
// to the single ROM byte it represents. The bespoke wrappers (ElementSet,
// StatusSet) predate this template and stay as they are.
#pragma once

#include <concepts>
#include <cstdint>

namespace ostinato {

template <typename FlagT>
struct FlagSet {
    std::uint8_t bits = 0;

    constexpr bool has(FlagT flag) const {
        return (bits & static_cast<std::uint8_t>(flag)) != 0;
    }

    constexpr void set(FlagT flag) {
        bits |= static_cast<std::uint8_t>(flag);
    }

    constexpr void clear(FlagT flag) {
        bits &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(flag));
    }

    // OR-together builder: FlagSet<F>::of(F::A, F::B). Zero arguments yields
    // the empty set, matching an all-zero ROM byte.
    static constexpr FlagSet of(std::same_as<FlagT> auto... flags) {
        FlagSet result{};
        (result.set(flags), ...);
        return result;
    }
};

}  // namespace ostinato
