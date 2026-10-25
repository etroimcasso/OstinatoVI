// The battle's random numbers.
//
// Every random choice a battle makes reads the next entry of the shipped 256-entry RNG table
// (rngByte), from a position that advances one entry per draw and wraps after the last. The
// battle loop also moves the position on by one entry on every step it takes, without drawing,
// so the numbers a battle sees depend on how many steps it has run.
//
//     BattleRng rng(seed);
//     std::uint8_t r = rng.next();       // 0-255
//     std::uint8_t k = rng.below(8);     // 0-7
//     bool heads = rng.coin();
//     auto target = rng.pickFrom(aliveMonsters);
//
// The position is a byte: its width is the table's length, and wrapping is how the table repeats.
#pragma once

#include <cstdint>
#include <optional>

#include "battle/battler_set.h"

namespace ostinato::battle {

class BattleRng {
public:
    explicit constexpr BattleRng(std::uint8_t position = 0) : position_(position) {}

    // The position of the last entry read.
    constexpr std::uint8_t position() const { return position_; }

    // Move on one entry without drawing — the battle loop's once-per-step advance.
    void advance() { ++position_; }

    // The next entry: 0-255.
    std::uint8_t next();

    // A number from 0 to n - 1: the next entry scaled by n / 256. Always draws, even for n of 1,
    // and returns 0 for n of 0.
    std::uint8_t below(std::uint8_t n);

    // A coin flip: the low bit of the next entry.
    bool coin();

    // One member of `set`, each equally likely, counting the party's members first (by position)
    // and then the monsters'. An empty set draws nothing and returns nothing; a set of one still
    // draws.
    std::optional<BattlerSlot> pickFrom(BattlerSet set);

private:
    std::uint8_t position_;
};

}  // namespace ostinato::battle
