// The set of swdtechs or blitzes a character has learned (LearnAbilityTbl,
// event.asm:1228), one bit per ability in the order they are taught. Abilities
// are learned in order, so every reachable value is a run of low bits: a
// character who has reached the level of N abilities has the low N bits set.
#pragma once

#include <cstdint>

namespace ostinato {

struct AbilityLearnedSet {
    std::uint8_t bits = 0;

    constexpr AbilityLearnedSet() = default;
    explicit constexpr AbilityLearnedSet(std::uint8_t raw) : bits(raw) {}

    // Has the ability in `slot` (0-7, in teaching order) been learned?
    constexpr bool has(std::uint8_t slot) const {
        return slot < 8 && (bits & static_cast<std::uint8_t>(1u << slot)) != 0;
    }
    // How many abilities the set holds.
    constexpr int count() const {
        int n = 0;
        for (std::uint8_t slot = 0; slot < 8; ++slot) {
            if (has(slot)) {
                ++n;
            }
        }
        return n;
    }
};

static_assert(sizeof(AbilityLearnedSet) == 1,
              "AbilityLearnedSet must be byte-identical to the ROM byte");

}  // namespace ostinato
