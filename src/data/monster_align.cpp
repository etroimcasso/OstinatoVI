#include "data/monster_align.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 256-entry table. The generated rows carry the ROM data; each entry
// carries its identity as the MonsterId enumerator and its value as the
// MonsterVerticalAlignment enumerator.
constexpr std::array<MonsterAlignEntry, 256> kMonsterAlignments = {{
#include "data/generated/monster_align_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal
// its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kMonsterAlignments.size(); ++i) {
        if (static_cast<std::size_t>(kMonsterAlignments[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kMonsterAlignments entry id fields must match array positions");

}  // namespace

MonsterVerticalAlignment getMonsterAlignment(MonsterId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kMonsterAlignments.size() &&
           "monster id out of the alignment table's 8-bit index space");
    return kMonsterAlignments[raw].alignment;
}

std::span<const MonsterAlignEntry> monsterAlignments() {
    return kMonsterAlignments;
}

}  // namespace ostinato
