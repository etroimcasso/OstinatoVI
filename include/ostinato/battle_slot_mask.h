// A set of battle slots, one bit per slot (JokerTargetTbl, c2/4e52). The battle
// field holds four character slots and six monster slots, so a mask of $0F
// selects every character and $3F selects every monster. Carrying the byte keeps
// it byte-identical to the ROM; has() is how you read it.
#pragma once

#include <cstdint>

namespace ostinato {

struct BattleSlotMask {
    std::uint8_t bits = 0;

    constexpr BattleSlotMask() = default;
    explicit constexpr BattleSlotMask(std::uint8_t raw) : bits(raw) {}

    // Is `slot` in the set? Slots are numbered from 0 within their side.
    constexpr bool has(std::uint8_t slot) const {
        return slot < 8 && (bits & static_cast<std::uint8_t>(1u << slot)) != 0;
    }
};

static_assert(sizeof(BattleSlotMask) == 1,
              "BattleSlotMask must be byte-identical to the ROM byte");

}  // namespace ostinato
