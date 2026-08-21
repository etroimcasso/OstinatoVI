// The world-map actions that hand off to an event script: boarding or landing a
// vehicle, resting, and entering a location from the overworld. Each one names a
// fixed script through the kWorldVehicleEvents table in src/data/world_map.h.
//
// Enumerator names come from the scripts the world data references
// (world/world_data.asm:21-40); the consumers are spread across the world
// module, one per action.
#pragma once

#include <cstdint>

namespace ostinato {

enum class WorldVehicleEvent : std::uint8_t {
    // Boarding the airship from its deck (world/ctrl.asm:381).
    AIRSHIP_DECK = 0,
    // Resting in a tent on the overworld (world/world_start.asm:323).
    WORLD_TENT = 1,
    // Landing the airship (world/move.asm:1201).
    AIRSHIP_GROUND = 2,
    // Entering the Phoenix Cave (world/init.asm:1855).
    ENTER_PHOENIX_CAVE = 3,
    // Entering Kefka's Tower (world/init.asm:1839).
    ENTER_KEFKAS_TOWER = 4,
    // Entering Gogo's lair (world/world_start.asm:474).
    ENTER_GOGOS_LAIR = 5,
    // Defeating Doom Gaze (world/world_start.asm:181).
    DOOM_GAZE_DEFEATED = 6,
};

// The number of vehicle-event slots (VehicleEvent_00 .. VehicleEvent_06).
inline constexpr std::uint8_t kWorldVehicleEventCount = 7;

}  // namespace ostinato
