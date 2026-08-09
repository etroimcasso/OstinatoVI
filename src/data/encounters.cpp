#include "data/encounters.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// rand/event battle groups: candidate formation words per group. Each entry
// carries its group index; each formation word names its formation through
// FormationRef::of in the generated rows.
constexpr std::array<RandomBattleGroupEntry, 256> kRandomBattleGroups = {{
#include "data/generated/rand_battle_group_data.inc"
}};

constexpr std::array<EventBattleGroupEntry, 256> kEventBattleGroups = {{
#include "data/generated/event_battle_group_data.inc"
}};

// world/sub group + rate tables: flat per-index byte tables (RNG-table shape,
// { .index, .value }). Each entry's index field must equal its array position.
constexpr std::array<WorldBattleGroupEntry, 512> kWorldBattleGroup = {{
#include "data/generated/world_battle_group_data.inc"
}};

constexpr std::array<SubBattleGroupEntry, 512> kSubBattleGroup = {{
#include "data/generated/sub_battle_group_data.inc"
}};

constexpr std::array<WorldBattleRateEntry, 128> kWorldBattleRate = {{
#include "data/generated/world_battle_rate_data.inc"
}};

constexpr std::array<SubBattleRateEntry, 128> kSubBattleRate = {{
#include "data/generated/sub_battle_rate_data.inc"
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

static_assert(indexMatchesPosition(kRandomBattleGroups),
              "kRandomBattleGroups index fields must match array positions");
static_assert(indexMatchesPosition(kEventBattleGroups),
              "kEventBattleGroups index fields must match array positions");
static_assert(indexMatchesPosition(kWorldBattleGroup),
              "kWorldBattleGroup index fields must match array positions");
static_assert(indexMatchesPosition(kSubBattleGroup),
              "kSubBattleGroup index fields must match array positions");
static_assert(indexMatchesPosition(kWorldBattleRate),
              "kWorldBattleRate index fields must match array positions");
static_assert(indexMatchesPosition(kSubBattleRate),
              "kSubBattleRate index fields must match array positions");

}  // namespace

const RandomBattleGroup& getRandomBattleGroup(std::uint8_t groupIndex) {
    assert(groupIndex < kRandomBattleGroups.size() &&
           "random battle group index out of range");
    return kRandomBattleGroups[groupIndex].record;
}

std::span<const RandomBattleGroupEntry> randomBattleGroups() {
    return kRandomBattleGroups;
}

const EventBattleGroup& getEventBattleGroup(std::uint8_t groupIndex) {
    assert(groupIndex < kEventBattleGroups.size() &&
           "event battle group index out of range");
    return kEventBattleGroups[groupIndex].record;
}

std::span<const EventBattleGroupEntry> eventBattleGroups() {
    return kEventBattleGroups;
}

std::uint8_t getWorldBattleGroup(WorldId world, std::uint8_t xSector,
                                 std::uint8_t ySector, std::uint8_t bgGroup) {
    const std::size_t idx = static_cast<std::size_t>(world) * 256 +
                            ySector * 32 + xSector * 4 + bgGroup;
    assert(idx < kWorldBattleGroup.size() && "world battle sector out of range");
    return kWorldBattleGroup[idx].value;
}

bool isVeldtSector(WorldId world, std::uint8_t xSector, std::uint8_t ySector,
                   std::uint8_t bgGroup) {
    return getWorldBattleGroup(world, xSector, ySector, bgGroup) == kVeldtSector;
}

std::span<const WorldBattleGroupEntry> worldBattleGroup() {
    return kWorldBattleGroup;
}

std::uint8_t getMapBattleGroup(std::uint16_t mapId) {
    assert(mapId < kSubBattleGroup.size() && "map id out of range");
    return kSubBattleGroup[mapId].value;
}

std::span<const SubBattleGroupEntry> subBattleGroup() { return kSubBattleGroup; }

WorldBattleRateClass getWorldBattleRate(WorldId world, std::uint8_t sector,
                                        std::uint8_t rateSlot) {
    const std::size_t idx = static_cast<std::size_t>(world) * 64 + sector;
    assert(idx < kWorldBattleRate.size() && "world rate sector out of range");
    const auto cls = static_cast<std::uint8_t>(
        (kWorldBattleRate[idx].value >> (rateSlot * 2)) & 0x03);
    return static_cast<WorldBattleRateClass>(cls);
}

std::span<const WorldBattleRateEntry> worldBattleRate() {
    return kWorldBattleRate;
}

SubBattleRateClass getMapBattleRate(std::uint16_t mapId) {
    const std::size_t idx = mapId >> 2;
    assert(idx < kSubBattleRate.size() && "map id out of range");
    const auto cls = static_cast<std::uint8_t>(
        (kSubBattleRate[idx].value >> ((mapId & 0x03) * 2)) & 0x03);
    return static_cast<SubBattleRateClass>(cls);
}

std::span<const SubBattleRateEntry> subBattleRate() { return kSubBattleRate; }

}  // namespace ostinato
