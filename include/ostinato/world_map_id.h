// Which world map the world-map program is running. The index lives at $1f64
// and selects the graphics, tilemap, palette, and song set at load time; the
// modification lists and tile-property tables are keyed by it.
//
// Only WORLD_OF_BALANCE and WORLD_OF_RUIN load through the standard world
// path — SERPENT_TRENCH takes its own init (world/init.asm:83), and
// PARTY_DEFEATED is parked in the index on a game over rather than loaded as a
// map. Data keyed by this enum therefore covers the first two values only; see
// worldModifications() for the range it accepts.
#pragma once

#include <cstdint>

namespace ostinato {

enum class WorldMapId : std::uint8_t {
    // The overworld before the apocalypse — graphics/tilemap set 1
    // (world/init.asm:326, :354).
    WORLD_OF_BALANCE = 0,
    // The overworld after it — graphics/tilemap set 2, selected by every
    // "branch if world of ruin" test in the load path (world/init.asm:876).
    WORLD_OF_RUIN = 1,
    // The Serpent Trench ride, which loads through InitSnakeRoad instead
    // (world/init.asm:83; world/interrupt.asm:148 tests for it by value).
    SERPENT_TRENCH = 2,
    // Written by PartyDefeated on a game over (world/init.asm:1804) as the
    // world program hands control back to the event script. No map loads
    // under it and no table is keyed by it.
    PARTY_DEFEATED = 3,
};

}  // namespace ostinato
