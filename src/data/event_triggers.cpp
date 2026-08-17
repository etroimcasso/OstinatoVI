#include "data/event_triggers.h"

#include <array>
#include <cassert>
#include <cstddef>

#include "data/map_properties.h"  // kMapAddressSpace

namespace ostinato {

namespace {

// The flat event trigger records in physical order; records are shared across
// maps via the offset table.
constexpr std::array<EventTrigger, kEventTriggerRecordCount> kEventTriggers = {{
#include "data/generated/event_trigger_data.inc"
}};

// The per-map trigger offset table: one entry per map slot plus a final end entry
// whose offset is the record count. 416 slots (one more than the 415 defined maps;
// the last slot is empty) + 1 end.
constexpr std::array<MapTriggerOffsetEntry, kEventTriggerMapSlots + 1>
    kEventTriggerOffsets = {{
#include "data/generated/event_trigger_offsets_data.inc"
}};

// The per-map startup events over the full 9-bit map space (512 wrapper-entry
// rows).
constexpr std::array<MapInitEventEntry, kMapAddressSpace> kMapInitEvents = {{
#include "data/generated/map_init_event_data.inc"
}};

template <typename Table>
constexpr bool indexMatchesPosition(const Table& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].index != i) {
            return false;
        }
    }
    return true;
}

// Offsets are monotonic non-decreasing (a map's slice is never negative-length).
constexpr bool offsetsMonotonic(
    const std::array<MapTriggerOffsetEntry, kEventTriggerMapSlots + 1>& table) {
    for (std::size_t i = 1; i < table.size(); ++i) {
        if (table[i].offset < table[i - 1].offset) {
            return false;
        }
    }
    return true;
}

static_assert(indexMatchesPosition(kEventTriggerOffsets),
              "kEventTriggerOffsets index fields must match array positions");
static_assert(indexMatchesPosition(kMapInitEvents),
              "kMapInitEvents index fields must match array positions");
static_assert(offsetsMonotonic(kEventTriggerOffsets),
              "kEventTriggerOffsets must be monotonic non-decreasing");
static_assert(kEventTriggerOffsets.back().offset == kEventTriggerRecordCount,
              "kEventTriggerOffsets end marker must equal the record count");

}  // namespace

std::span<const EventTrigger> eventTriggersForMap(std::uint16_t mapIndex) {
    assert(mapIndex + 1u < kEventTriggerOffsets.size() && "map id out of range");
    const std::size_t begin = kEventTriggerOffsets[mapIndex].offset;
    const std::size_t end = kEventTriggerOffsets[mapIndex + 1].offset;
    return std::span<const EventTrigger>(kEventTriggers).subspan(begin,
                                                                 end - begin);
}

EventScriptRef mapInitEvent(std::uint16_t mapIndex) {
    assert(mapIndex < kMapInitEvents.size() && "map id out of range");
    return kMapInitEvents[mapIndex].record;
}

std::span<const EventTrigger> eventTriggerRecords() { return kEventTriggers; }

std::span<const MapTriggerOffsetEntry> eventTriggerOffsets() {
    return kEventTriggerOffsets;
}

std::span<const MapInitEventEntry> mapInitEvents() { return kMapInitEvents; }

}  // namespace ostinato
