// Hand-written port-design (PLAN phase-1.A D4). Not parser-emitted.
//
// A set of elemental affinities packed into one byte — bucket 2 (multi-component)
// per the data-surface discipline. The Element enum carries the upstream bit
// values (FIRE=0x01 … WATER=0x80); this wrapper encapsulates the bit math so no
// call site ever open-codes a mask. sizeof == 1 keeps it byte-identical to the
// single affinity byte the ROM stores.
#pragma once

#include <concepts>
#include <cstdint>

#include "ostinato/element.h"

namespace ostinato {

struct ElementSet {
    std::uint8_t bits = 0;

    // Element::NONE == 0, so has(NONE) is always false — the "no elements"
    // sentinel, matching the upstream meaning of an all-zero affinity byte.
    constexpr bool has(Element element) const {
        return (bits & static_cast<std::uint8_t>(element)) != 0;
    }

    constexpr void set(Element element) {
        bits |= static_cast<std::uint8_t>(element);
    }

    constexpr void clear(Element element) {
        bits &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(element));
    }

    // OR-together builder (added in phase 1.B): ElementSet::of(Element::FIRE,
    // Element::ICE). Zero arguments yields the empty set (the all-zero
    // affinity byte).
    static constexpr ElementSet of(std::same_as<Element> auto... elements) {
        ElementSet result{};
        (result.set(elements), ...);
        return result;
    }
};

static_assert(sizeof(ElementSet) == 1, "ElementSet must be byte-identical to the ROM affinity byte");

}  // namespace ostinato
