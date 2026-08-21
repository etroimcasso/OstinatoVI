// World-map data accessors: the story-gated tilemap modifications each world
// applies at load time, the event scripts the world-map vehicle actions run, and
// the sine table the world program uses for vehicle movement and Mode 7 camera
// work. The row data is generated (src/data/generated/*.inc); this header owns
// the entry types and the accessors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/event_script_ref.h"
#include "ostinato/world_map_id.h"
#include "ostinato/world_map_modification.h"
#include "ostinato/world_vehicle_event.h"

namespace ostinato {

// The number of modification chunks across all worlds, and the number of worlds
// that have a modification list. The Serpent Trench and the game-over index have
// none — the ROM pointer table's third entry is the end terminator, not a third
// world (world/world_data.asm:18).
inline constexpr std::size_t kWorldModificationCount = 18;
inline constexpr std::size_t kWorldModifiedWorldCount = 2;

// Degrees covered by the sine table: 0..270, so a cosine read at index + 90
// stays in range for any reduced angle.
inline constexpr std::size_t kWorldSineLength = 271;

// One entry of the per-world modification offset table: the world, and the chunk
// its list begins at. The final entry is the end marker — its index is the world
// count and its firstChunk the chunk count — so a world's chunks are the
// half-open slice [firstChunk[world], firstChunk[world + 1]).
struct WorldModDataEntry {
    std::uint16_t index;
    std::uint16_t firstChunk;
};

// One entry of the vehicle-event table: the action, and the script it runs.
struct WorldVehicleEventEntry {
    WorldVehicleEvent event;
    EventScriptRef script;
};

// One entry of the sine table: the degree, and the amplitude stored for it.
// Identity is a field, never a comment — every generated row reads
// { .index = N, .amplitude = M }, so no value is positionally opaque.
struct WorldSineEntry {
    std::uint16_t index;
    std::uint8_t amplitude;
};

// --- accessors ---------------------------------------------------------------

// The tilemap modifications a world applies at load time, in the order
// ModifyMap walks them. Only WORLD_OF_BALANCE and WORLD_OF_RUIN have a list;
// any other world is out of range.
std::span<const WorldMapModification> worldModifications(WorldMapId world);

// The script a world-map vehicle action runs.
EventScriptRef worldVehicleEvent(WorldVehicleEvent event);

// The sine amplitude at an angle in degrees (0-359), and the cosine amplitude
// at the same angle. Both are magnitudes: the table stores |sin| only, and the
// world program applies the sign itself from the angle's quadrant
// (world/move.asm:43-65). The angle is reduced the way the consumer reduces it,
// by a single subtraction of 180 (world/move.asm:35-37).
std::uint8_t worldSine(std::uint16_t degrees);
std::uint8_t worldCosine(std::uint16_t degrees);

// The flat chunk array + the per-world offset table (one entry per world with a
// list, plus a final end entry).
std::span<const WorldMapModification> worldModificationRecords();
std::span<const WorldModDataEntry> worldModificationOffsets();

// The whole vehicle-event table (7 rows) and the whole sine table (271 rows).
std::span<const WorldVehicleEventEntry> worldVehicleEvents();
std::span<const WorldSineEntry> worldSineTable();

}  // namespace ostinato
