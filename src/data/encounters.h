// Field encounters: the tables that decide which formation the player runs
// into on the world map and in maps, and how often. Two group families
// (rand/event) name their candidate formations; two group-index tables
// (world/sub) point at a rand group per sector or map; two rate tables carry
// the per-sector/per-map battle-frequency class. Five small inline tables from
// field/battle.asm (battle backgrounds, rate-slot / bg-group selectors, and the
// per-charm-state counter increments) round out the selection math. The row
// data is generated (src/data/generated/*.inc); this header owns the record
// types, the enums, the inline tables, and the accessors.
//
// The selection *algorithm* (sector math, RNG rolls, the 80/80/80/16 slot odds)
// is a runtime consumer added later — see docs/contracts/encounters.md. This
// layer is the static data plus the field accessors that read one entry.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/battle_background_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"

namespace ostinato {

// Which world map the party is on ($1F64). The world-map group/rate tables are
// indexed [world][...]; both worlds share the same 256-entry sector space.
enum class WorldId : std::uint8_t {
    WORLD_OF_BALANCE = 0,
    WORLD_OF_RUIN = 1,
};

// The charm relic in effect ($11DF), selecting a row of the rate-increment
// tables. Charm Bangle halves the encounter rate; the Moogle Charm zeroes it
// (no random battles). The fourth row is unused ROM data.
enum class CharmState : std::uint8_t {
    NONE = 0,
    CHARM_BANGLE = 1,
    MOOGLE_CHARM = 2,
    UNUSED_3 = 3,
};

// A world-map sector's battle-frequency class (`field/battle.asm:250`
// "normal, low, high, none"). Class NONE means the sector has no random
// battles; the data selects only 0-2 corpus-wide.
enum class WorldBattleRateClass : std::uint8_t {
    NORMAL = 0,
    LOW = 1,
    HIGH = 2,
    NONE = 3,
};

// A map's battle-frequency class (`field/battle.asm:258`
// "normal, low, high, very high"); the data selects only 0-2 corpus-wide.
enum class SubBattleRateClass : std::uint8_t {
    NORMAL = 0,
    LOW = 1,
    HIGH = 2,
    VERY_HIGH = 3,
};

// The 16-bit random-battle counter increment for each rate class, one row per
// CharmState. These are magnitudes added to the step counter (`$1F6E`), not bit
// fields — decimal values, not raw bytes.
struct BattleRateIncrements {
    std::array<std::uint16_t, 4> byRate;
};

// The five inline field/battle.asm tables (battle backgrounds + rate-slot /
// bg-group selectors + the two rate-increment tables). Public constants like
// kRngTable; consumed by the selection math and verified in full by the tests.
#include "data/generated/encounter_bg_tables_data.inc"

// One random-battle group: four candidate formation words (RandBattleGroup,
// ROM CF/4800). The selection picks one of the four at 80/80/80/16-in-256 odds
// (consumer math); a word's randomizePlus3 flag adds a random 0-3 to the index.
struct RandomBattleGroup {
    std::array<FormationRef, 4> formations;
};
static_assert(sizeof(RandomBattleGroup) == 8,
              "RandomBattleGroup must be byte-identical to four ROM formation "
              "words");

// One event-battle group: two candidate formation words (EventBattleGroup, ROM
// CF/5000). The selection picks between them at 3/4-1/4 odds (consumer math).
struct EventBattleGroup {
    std::array<FormationRef, 2> formations;
};
static_assert(sizeof(EventBattleGroup) == 4,
              "EventBattleGroup must be byte-identical to two ROM formation "
              "words");

// Table entries: the group's identity (its 0-255 index) as a typed field
// alongside the sizeof-locked record — never a bare comment. A compile-time
// assert verifies index == array position.
struct RandomBattleGroupEntry {
    std::uint16_t index;
    RandomBattleGroup record;
};
struct EventBattleGroupEntry {
    std::uint16_t index;
    EventBattleGroup record;
};

// One entry of a flat per-index byte table (RNG-table shape): the position and
// the raw ROM byte at it, so no value is positionally opaque.
struct WorldBattleGroupEntry {
    std::uint16_t index;
    std::uint8_t value;  // rand-group index, or kVeldtSector
};
struct SubBattleGroupEntry {
    std::uint16_t index;
    std::uint8_t value;  // rand-group index for this map
};
struct WorldBattleRateEntry {
    std::uint16_t index;
    std::uint8_t value;  // four packed 2-bit rate classes
};
struct SubBattleRateEntry {
    std::uint16_t index;
    std::uint8_t value;  // four maps' packed 2-bit rate classes
};

// The world_battle_group value that marks a Veldt sector (no ROM formation
// group; the formation is chosen from the RAM encounter list at run time).
inline constexpr std::uint8_t kVeldtSector = 0xFF;

// --- rand/event groups: the candidate formations of a group ------------------

const RandomBattleGroup& getRandomBattleGroup(std::uint8_t groupIndex);
std::span<const RandomBattleGroupEntry> randomBattleGroups();

const EventBattleGroup& getEventBattleGroup(std::uint8_t groupIndex);
std::span<const EventBattleGroupEntry> eventBattleGroups();

// --- world/sub groups: which rand group a sector or map uses -----------------

// The rand-group index for a world-map sector. xSector/ySector are 0-7 (the
// high 3 bits of the party's X/Y within the map), bgGroup 0-3 (from
// kBattleBgGroupOffset). The flat index is world*256 + ySector*32 + xSector*4 +
// bgGroup, transcribed from field/battle.asm:120-135. Returns kVeldtSector for
// a Veldt sector — test with isVeldtSector.
std::uint8_t getWorldBattleGroup(WorldId world, std::uint8_t xSector,
                                 std::uint8_t ySector, std::uint8_t bgGroup);
bool isVeldtSector(WorldId world, std::uint8_t xSector, std::uint8_t ySector,
                   std::uint8_t bgGroup);
std::span<const WorldBattleGroupEntry> worldBattleGroup();

// The rand-group index for a map (mapId 0-511; `$0082`).
std::uint8_t getMapBattleGroup(std::uint16_t mapId);
std::span<const SubBattleGroupEntry> subBattleGroup();

// --- world/sub rates: the battle-frequency class of a sector or map ----------

// The rate class of a world-map sector. sector is 0-63 (the sector bits >> 2),
// rateSlot 0-3 (from kBattleBgRateSlot). The byte is at world*64 + sector; the
// class is the rateSlot-th 2-bit field, per field/battle.asm:142-153.
WorldBattleRateClass getWorldBattleRate(WorldId world, std::uint8_t sector,
                                        std::uint8_t rateSlot);
std::span<const WorldBattleRateEntry> worldBattleRate();

// The rate class of a map (mapId 0-511). The byte is at mapId >> 2; the class
// is the (mapId & 3)-th 2-bit field, per field/battle.asm:353-367.
SubBattleRateClass getMapBattleRate(std::uint16_t mapId);
std::span<const SubBattleRateEntry> subBattleRate();

}  // namespace ostinato
