#include "data/map_triggers.h"

#include <array>
#include <cassert>
#include <cstddef>

#include "data/map_properties.h"  // kMapCount, kMapAddressSpace

namespace ostinato {

namespace {

// The flat record arrays in physical order; records are shared across maps.
constexpr std::array<TreasureProperty, kTreasureRecordCount> kTreasureProperties = {{
#include "data/generated/treasure_data.inc"
}};

constexpr std::array<LongEntrance, kLongEntranceRecordCount> kLongEntrances = {{
#include "data/generated/long_entrance_data.inc"
}};

constexpr std::array<ShortEntrance, kShortEntranceRecordCount> kShortEntrances = {{
#include "data/generated/short_entrance_data.inc"
}};

// The per-map offset tables: one entry per map slot plus a final end entry whose
// offset is the record count. Treasures cover kMapCount maps; entrances cover the
// full kMapAddressSpace slot count.
constexpr std::array<MapTriggerOffsetEntry, kMapCount + 1> kTreasureOffsets = {{
#include "data/generated/treasure_offsets_data.inc"
}};

constexpr std::array<MapTriggerOffsetEntry, kMapAddressSpace + 1>
    kLongEntranceOffsets = {{
#include "data/generated/long_entrance_offsets_data.inc"
}};

constexpr std::array<MapTriggerOffsetEntry, kMapAddressSpace + 1>
    kShortEntranceOffsets = {{
#include "data/generated/short_entrance_offsets_data.inc"
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
template <typename Table>
constexpr bool offsetsMonotonic(const Table& table) {
    for (std::size_t i = 1; i < table.size(); ++i) {
        if (table[i].offset < table[i - 1].offset) {
            return false;
        }
    }
    return true;
}

static_assert(indexMatchesPosition(kTreasureOffsets),
              "kTreasureOffsets index fields must match array positions");
static_assert(indexMatchesPosition(kLongEntranceOffsets),
              "kLongEntranceOffsets index fields must match array positions");
static_assert(indexMatchesPosition(kShortEntranceOffsets),
              "kShortEntranceOffsets index fields must match array positions");

static_assert(offsetsMonotonic(kTreasureOffsets),
              "kTreasureOffsets must be monotonic non-decreasing");
static_assert(offsetsMonotonic(kLongEntranceOffsets),
              "kLongEntranceOffsets must be monotonic non-decreasing");
static_assert(offsetsMonotonic(kShortEntranceOffsets),
              "kShortEntranceOffsets must be monotonic non-decreasing");

static_assert(kTreasureOffsets.back().offset == kTreasureRecordCount,
              "kTreasureOffsets end marker must equal the record count");
static_assert(kLongEntranceOffsets.back().offset == kLongEntranceRecordCount,
              "kLongEntranceOffsets end marker must equal the record count");
static_assert(kShortEntranceOffsets.back().offset == kShortEntranceRecordCount,
              "kShortEntranceOffsets end marker must equal the record count");

// Slice the records for one map out of its flat array via the offset table.
template <typename Record, std::size_t N, std::size_t M>
std::span<const Record> sliceForMap(const std::array<Record, N>& records,
                                    const std::array<MapTriggerOffsetEntry, M>& offsets,
                                    std::uint16_t mapIndex) {
    assert(mapIndex + 1u < offsets.size() && "map id out of range");
    const std::size_t begin = offsets[mapIndex].offset;
    const std::size_t end = offsets[mapIndex + 1].offset;
    return std::span<const Record>(records).subspan(begin, end - begin);
}

}  // namespace

std::span<const TreasureProperty> treasuresForMap(std::uint16_t mapIndex) {
    return sliceForMap(kTreasureProperties, kTreasureOffsets, mapIndex);
}

std::span<const LongEntrance> longEntrancesForMap(std::uint16_t mapIndex) {
    return sliceForMap(kLongEntrances, kLongEntranceOffsets, mapIndex);
}

std::span<const ShortEntrance> shortEntrancesForMap(std::uint16_t mapIndex) {
    return sliceForMap(kShortEntrances, kShortEntranceOffsets, mapIndex);
}

std::span<const TreasureProperty> treasureRecords() { return kTreasureProperties; }
std::span<const LongEntrance> longEntranceRecords() { return kLongEntrances; }
std::span<const ShortEntrance> shortEntranceRecords() { return kShortEntrances; }

std::span<const MapTriggerOffsetEntry> treasureOffsets() { return kTreasureOffsets; }
std::span<const MapTriggerOffsetEntry> longEntranceOffsets() {
    return kLongEntranceOffsets;
}
std::span<const MapTriggerOffsetEntry> shortEntranceOffsets() {
    return kShortEntranceOffsets;
}

}  // namespace ostinato
