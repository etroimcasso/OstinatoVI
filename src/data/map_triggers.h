// Map trigger family: the treasures and entrances placed on each field map. All
// three tables are pointer-table streams — the records live in one flat array
// (shared across maps), and a per-map offset table names where each map's slice
// begins. The row data is generated (src/data/generated/*.inc); this header owns
// the offset-entry type and the accessors.
//
// Treasures cover the 415 defined maps (kMapCount); entrances cover the full
// 9-bit 512-slot address space (kMapAddressSpace) — see map_properties.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/map_entrance.h"
#include "ostinato/treasure_property.h"

namespace ostinato {

// The number of distinct records in each trigger table (records are shared
// across maps via the offset tables).
inline constexpr std::size_t kTreasureRecordCount = 286;
inline constexpr std::size_t kLongEntranceRecordCount = 152;
inline constexpr std::size_t kShortEntranceRecordCount = 1129;

// One entry of a per-map trigger offset table: the map id as a typed identity
// field alongside the record-array index at which that map's records begin. The
// final entry's index is the map-slot count and its offset is the record count
// (end marker); a map's records are the half-open slice
// [offset[map], offset[map + 1]). A compile-time assert verifies index == array
// position.
struct MapTriggerOffsetEntry {
    std::uint16_t index;   // map id
    std::uint16_t offset;  // first record index into the table's record array
};

// --- accessors ---------------------------------------------------------------

// The treasures placed on a map (0-414). Maps with no treasure return an empty
// span. mapIndex must be in range.
std::span<const TreasureProperty> treasuresForMap(std::uint16_t mapIndex);

// The long / short entrances on a map (0-511; the entrance tables cover the full
// addressable map space). Maps with no entrance of that kind return an empty
// span. mapIndex must be in range.
std::span<const LongEntrance> longEntrancesForMap(std::uint16_t mapIndex);
std::span<const ShortEntrance> shortEntrancesForMap(std::uint16_t mapIndex);

// The flat record arrays (whole-table iteration / corpus tests).
std::span<const TreasureProperty> treasureRecords();
std::span<const LongEntrance> longEntranceRecords();
std::span<const ShortEntrance> shortEntranceRecords();

// The per-map offset tables (map-slot count + 1 end entry): 416 / 513 / 513.
std::span<const MapTriggerOffsetEntry> treasureOffsets();
std::span<const MapTriggerOffsetEntry> longEntranceOffsets();
std::span<const MapTriggerOffsetEntry> shortEntranceOffsets();

}  // namespace ostinato
