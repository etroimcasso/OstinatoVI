#include "data/world_map.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The modification chunks of both worlds, contiguous and in physical order;
// chunks belong to a world via the offset table.
constexpr std::array<WorldMapModification, kWorldModificationCount>
    kWorldMapModifications = {{
#include "data/generated/world_mod_data.inc"
}};

// The per-world modification offset table: one entry per world with a list plus
// a final end entry whose firstChunk is the chunk count.
constexpr std::array<WorldModDataEntry, kWorldModifiedWorldCount + 1>
    kWorldModOffsets = {{
#include "data/generated/world_mod_offsets_data.inc"
}};

// The script each world-map vehicle action runs, in ROM order.
constexpr std::array<WorldVehicleEventEntry, kWorldVehicleEventCount>
    kWorldVehicleEvents = {{
#include "data/generated/world_vehicle_events_data.inc"
}};

// floor(|sin(2*pi*degree/360)| * 255) for degree 0..270.
constexpr std::array<WorldSineEntry, kWorldSineLength> kWorldSine = {{
#include "data/generated/world_sine_data.inc"
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

// Offsets are monotonic non-decreasing (a world's slice is never negative
// length).
constexpr bool offsetsMonotonic(
    const std::array<WorldModDataEntry, kWorldModifiedWorldCount + 1>& table) {
    for (std::size_t i = 1; i < table.size(); ++i) {
        if (table[i].firstChunk < table[i - 1].firstChunk) {
            return false;
        }
    }
    return true;
}

// Vehicle-event rows are positional: row N is the action whose enumerator
// value is N.
constexpr bool vehicleEventsInEnumeratorOrder() {
    for (std::size_t i = 0; i < kWorldVehicleEvents.size(); ++i) {
        if (static_cast<std::size_t>(kWorldVehicleEvents[i].event) != i) {
            return false;
        }
    }
    return true;
}

static_assert(indexMatchesPosition(kWorldModOffsets),
              "kWorldModOffsets index fields must match array positions");
static_assert(indexMatchesPosition(kWorldSine),
              "kWorldSine index fields must match array positions");
static_assert(offsetsMonotonic(kWorldModOffsets),
              "kWorldModOffsets must be monotonic non-decreasing");
static_assert(kWorldModOffsets.back().firstChunk == kWorldModificationCount,
              "kWorldModOffsets end marker must equal the chunk count");
static_assert(vehicleEventsInEnumeratorOrder(),
              "kWorldVehicleEvents rows must sit at their enumerator's position");

// The angle reduction the world program applies before indexing: a single
// subtraction of 180, not a modulo (world/move.asm:35-37). Angles are degrees
// on a full turn, so one subtraction is enough.
constexpr std::uint16_t reduceAngle(std::uint16_t degrees) {
    return degrees >= 180 ? static_cast<std::uint16_t>(degrees - 180) : degrees;
}

// The cosine read is the sine read a quarter turn along (world/move.asm:41).
inline constexpr std::uint16_t kQuarterTurn = 90;

}  // namespace

std::span<const WorldMapModification> worldModifications(WorldMapId world) {
    const auto index = static_cast<std::size_t>(world);
    assert(index + 1u < kWorldModOffsets.size()
           && "world has no modification list");
    const std::size_t begin = kWorldModOffsets[index].firstChunk;
    const std::size_t end = kWorldModOffsets[index + 1].firstChunk;
    return std::span<const WorldMapModification>(kWorldMapModifications)
        .subspan(begin, end - begin);
}

EventScriptRef worldVehicleEvent(WorldVehicleEvent event) {
    const auto index = static_cast<std::size_t>(event);
    assert(index < kWorldVehicleEvents.size() && "vehicle event out of range");
    return kWorldVehicleEvents[index].script;
}

std::uint8_t worldSine(std::uint16_t degrees) {
    assert(degrees < 360 && "angle must be a degree on one turn");
    return kWorldSine[reduceAngle(degrees)].amplitude;
}

std::uint8_t worldCosine(std::uint16_t degrees) {
    assert(degrees < 360 && "angle must be a degree on one turn");
    return kWorldSine[reduceAngle(degrees) + kQuarterTurn].amplitude;
}

std::span<const WorldMapModification> worldModificationRecords() {
    return kWorldMapModifications;
}

std::span<const WorldModDataEntry> worldModificationOffsets() {
    return kWorldModOffsets;
}

std::span<const WorldVehicleEventEntry> worldVehicleEvents() {
    return kWorldVehicleEvents;
}

std::span<const WorldSineEntry> worldSineTable() { return kWorldSine; }

}  // namespace ostinato
