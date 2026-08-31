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

// Where one patch writes, in tilemap columns and rows. The world tilemap is
// 256 columns wide, so a row step is a whole 256 from the destination word
// (world/init.asm:1976-1979).
struct WorldTileDestination {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// The tiles one modification chunk stamps onto the world map, read in place.
//
// A patch is a rectangle: where it goes, how big it is, and its tiles row by
// row. The bytes stay where they are — this is a view over the tile pool, not a
// copy — so it is only as good as the span it was built from.
//
// The record is a destination word, a byte packing width into its high nybble
// and height into its low one, then width x height tiles in row-major order
// (world/init.asm:1957-1982).
class WorldTilePatch {
public:
    WorldTilePatch() = default;

    // A patch over `bytes`, which must begin at the record's first byte and run
    // at least to its last. A span too short to hold the header, or too short
    // for the tiles the header claims, yields an empty patch (valid() == false)
    // rather than reading past the end.
    explicit WorldTilePatch(std::span<const std::uint8_t> bytes);

    // Whether the span held a whole record. Everything below reads as zero or
    // empty when it did not.
    bool valid() const { return !bytes_.empty(); }

    WorldTileDestination destination() const;
    std::uint8_t width() const;
    std::uint8_t height() const;

    // The tiles, row-major: row r spans [r * width(), (r + 1) * width()).
    std::span<const std::uint8_t> tiles() const;

private:
    std::span<const std::uint8_t> bytes_{};
};

// The pool of patch tiles the modification chunks point into.
//
// A chunk's WorldTilePatchRef counts from the start of the modification block,
// which begins with the per-world chunk lists; the tiles follow them. So a ref
// is resolved by subtracting how far into that block the pool itself starts —
// the arithmetic the world program does inline (world/init.asm:1954-1956), kept
// here so no consumer repeats it.
struct WorldTilePool {
    // The pool's bytes, and how far into the modification block they begin.
    std::span<const std::uint8_t> bytes{};
    std::uint16_t offsetInBlock = 0;

    // The patch a chunk's ref names. A ref pointing before the pool or past its
    // end yields an invalid patch.
    WorldTilePatch patchAt(WorldTilePatchRef ref) const;
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
