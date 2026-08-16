// The map's default-battle-background byte (MapProperties +2). Packs the battle
// background id with the bg3-foreground priority flag. map.asm:1249-1252 masks
// the low 7 bits (and #$7f) as the map's default battle background; map.asm:
// 1373-1376 reads bit 7 (and #$80 lsr2) into the bg3 tile-priority flags.
// sizeof == 1 keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

#include "ostinato/battle_background_id.h"

namespace ostinato {

struct MapBattleBackground {
    std::uint8_t bits = 0;

    constexpr MapBattleBackground() = default;
    explicit constexpr MapBattleBackground(std::uint8_t raw) : bits(raw) {}

    // Bits 0-6: the battle background shown for random battles on this map.
    constexpr BattleBackgroundId background() const {
        return static_cast<BattleBackgroundId>(bits & 0x7F);
    }
    // Bit 7: bg3 renders in the foreground (tile priority) on this map.
    constexpr bool bg3Foreground() const { return (bits & 0x80) != 0; }
};

static_assert(sizeof(MapBattleBackground) == 1,
              "MapBattleBackground must be byte-identical to the ROM byte");

}  // namespace ostinato
