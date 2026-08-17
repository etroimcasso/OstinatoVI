#include "data/npc_properties.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The flat NPC records in physical order; records are shared across maps via the
// offset table. Each row is a named builder that packs to the exact ROM bytes.
constexpr std::array<NpcProperties, kNpcRecordCount> kNpcRecords = {{
#include "data/generated/npc_prop_data.inc"
}};

// The per-map NPC offset table: one entry per map slot plus a final end entry
// whose offset is the record count. 416 slots (one more than the 415 defined
// maps; world maps 0-2 and slot 415 are empty) + 1 end.
constexpr std::array<MapTriggerOffsetEntry, kNpcMapSlots + 1> kNpcOffsets = {{
#include "data/generated/npc_prop_offsets_data.inc"
}};

constexpr bool indexMatchesPosition() {
    for (std::size_t i = 0; i < kNpcOffsets.size(); ++i) {
        if (kNpcOffsets[i].index != i) {
            return false;
        }
    }
    return true;
}

constexpr bool offsetsMonotonic() {
    for (std::size_t i = 1; i < kNpcOffsets.size(); ++i) {
        if (kNpcOffsets[i].offset < kNpcOffsets[i - 1].offset) {
            return false;
        }
    }
    return true;
}

static_assert(indexMatchesPosition(),
              "kNpcOffsets index fields must match array positions");
static_assert(offsetsMonotonic(),
              "kNpcOffsets must be monotonic non-decreasing");
static_assert(kNpcOffsets.back().offset == kNpcRecordCount,
              "kNpcOffsets end marker must equal the record count");

}  // namespace

std::span<const NpcProperties> npcsForMap(std::uint16_t mapIndex) {
    assert(mapIndex + 1u < kNpcOffsets.size() && "map id out of range");
    const std::size_t begin = kNpcOffsets[mapIndex].offset;
    const std::size_t end = kNpcOffsets[mapIndex + 1].offset;
    return std::span<const NpcProperties>(kNpcRecords).subspan(begin,
                                                               end - begin);
}

std::span<const NpcProperties> npcRecords() { return kNpcRecords; }

std::span<const MapTriggerOffsetEntry> npcOffsets() { return kNpcOffsets; }

}  // namespace ostinato
