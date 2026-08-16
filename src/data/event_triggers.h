// Per-map event data accessors: the tile-triggered events on each field map and
// the startup event per map. The event triggers are a pointer-table stream — the
// records live in one flat array (shared across maps), and a per-map offset table
// names where each map's slice begins (the same MapTriggerOffsetEntry shape as the
// map trigger family). The map-init events are a flat per-map array. The row data
// is generated (src/data/generated/*.inc); this header owns the accessors.
//
// The trigger ptr table has 416 map slots — one more than the 415 defined maps
// (kMapCount) — with the last slot empty; the map-init array covers the full 9-bit
// 512-slot address space (kMapAddressSpace). See map_properties.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/event_trigger.h"
#include "data/map_triggers.h"  // MapTriggerOffsetEntry

namespace ostinato {

// The number of distinct event trigger records (shared across maps via the offset
// table) and the number of trigger map slots (one more than kMapCount).
inline constexpr std::size_t kEventTriggerRecordCount = 1164;
inline constexpr std::size_t kEventTriggerMapSlots = 416;

// --- accessors ---------------------------------------------------------------

// The event triggers on a map (0-415). Maps with no trigger return an empty span.
// mapIndex must be in range.
std::span<const EventTrigger> eventTriggersForMap(std::uint16_t mapIndex);

// The startup event for a map (0-511; the map-init array covers the full
// addressable map space). mapIndex must be in range.
EventScriptRef mapInitEvent(std::uint16_t mapIndex);

// The flat record array + the per-map offset table (416 map slots + 1 end entry).
std::span<const EventTrigger> eventTriggerRecords();
std::span<const MapTriggerOffsetEntry> eventTriggerOffsets();

// The whole per-map startup-event table (512 wrapper-entry rows).
std::span<const MapInitEventEntry> mapInitEvents();

}  // namespace ostinato
