#include "data/monster_attacks.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The three tables. The generated rows carry the ROM record data;
// designated initializers at every row keep each slot self-labeling, and
// each entry carries its identity as the MonsterId enumerator.
constexpr std::array<MonsterRageEntry, 256> kMonsterRages = {{
#include "data/generated/monster_rage_data.inc"
}};

constexpr std::array<MonsterSketchEntry, 384> kMonsterSketches = {{
#include "data/generated/monster_sketch_data.inc"
}};

constexpr std::array<MonsterControlEntry, 384> kMonsterControls = {{
#include "data/generated/monster_control_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal
// its array position, checked at compile time for all three tables.
template <typename Table>
constexpr bool idsMatchPositions(const Table& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (static_cast<std::size_t>(table[i].id) != i) {
            return false;
        }
    }
    return true;
}

static_assert(idsMatchPositions(kMonsterRages),
              "kMonsterRages entry id fields must match array positions");
static_assert(idsMatchPositions(kMonsterSketches),
              "kMonsterSketches entry id fields must match array positions");
static_assert(idsMatchPositions(kMonsterControls),
              "kMonsterControls entry id fields must match array positions");

}  // namespace

const MonsterRage& getMonsterRage(MonsterId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kMonsterRages.size() &&
           "monster id out of the rage table's 8-bit index space");
    return kMonsterRages[raw].record;
}

const MonsterSketch& getMonsterSketch(MonsterId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kMonsterSketches.size() && "monster id out of range");
    return kMonsterSketches[raw].record;
}

const MonsterControl& getMonsterControl(MonsterId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kMonsterControls.size() && "monster id out of range");
    return kMonsterControls[raw].record;
}

std::span<const MonsterRageEntry> monsterRages() { return kMonsterRages; }

std::span<const MonsterSketchEntry> monsterSketches() {
    return kMonsterSketches;
}

std::span<const MonsterControlEntry> monsterControls() {
    return kMonsterControls;
}

}  // namespace ostinato
