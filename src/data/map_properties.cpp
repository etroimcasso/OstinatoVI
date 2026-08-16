#include "data/map_properties.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 415 map-properties records in map-id order; each entry's index field must
// equal its array position.
constexpr std::array<MapPropertiesEntry, kMapCount> kMapProperties = {{
#include "data/generated/map_properties_data.inc"
}};

// The 21 parallax records in parallax-index order.
constexpr std::array<MapParallaxEntry, kParallaxCount> kMapParallax = {{
#include "data/generated/map_parallax_data.inc"
}};

// The 128-byte new-game initial NPC event-bit seed block
// (constexpr std::uint8_t kInitialNpcSwitches[128]).
#include "data/generated/init_npc_switch_data.inc"

static_assert(sizeof(kInitialNpcSwitches) == kInitialNpcSwitchBytes,
              "kInitialNpcSwitches must be the 128-byte seed block");

template <typename Table>
constexpr bool indexMatchesPosition(const Table& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].index != i) {
            return false;
        }
    }
    return true;
}

static_assert(indexMatchesPosition(kMapProperties),
              "kMapProperties index fields must match array positions");
static_assert(indexMatchesPosition(kMapParallax),
              "kMapParallax index fields must match array positions");

}  // namespace

const MapProperties& mapProperties(std::uint16_t index) {
    assert(index < kMapProperties.size() && "map id out of range");
    return kMapProperties[index].record;
}

std::span<const MapPropertiesEntry> mapProperties() { return kMapProperties; }

const MapParallax& mapParallax(std::uint8_t index) {
    assert(index < kMapParallax.size() && "parallax index out of range");
    return kMapParallax[index].record;
}

std::span<const MapParallaxEntry> mapParallax() { return kMapParallax; }

std::span<const std::uint8_t> initialNpcSwitches() {
    return kInitialNpcSwitches;
}

}  // namespace ostinato
