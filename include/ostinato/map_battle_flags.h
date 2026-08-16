// The map's battle-flags byte (MapProperties +5). Only bit 7 has a field
// consumer: battle.asm:332-333 skips the random-battle roll unless it is set
// (tested via bpl). Bits 0-6 have no consumer and are preserved raw.
// sizeof == 1 keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

namespace ostinato {

struct MapBattleFlags {
    std::uint8_t bits = 0;

    constexpr MapBattleFlags() = default;
    explicit constexpr MapBattleFlags(std::uint8_t raw) : bits(raw) {}

    // Bit 7: random battles occur on this map.
    constexpr bool randomBattles() const { return (bits & 0x80) != 0; }
};

static_assert(sizeof(MapBattleFlags) == 1,
              "MapBattleFlags must be byte-identical to the ROM byte");

}  // namespace ostinato
