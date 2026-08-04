#include "data/character.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 64-entry table. The parser-emitted rows are the transcribed contract
// data; this array + its types are the port surface. Designated initializers
// at every row keep each field self-labeling; each entry carries its identity
// as the CharacterPropId enumerator. Empty records are the ROM's zero-filled
// padding slots (all-zero, distinct from the 0xFF EMPTY/NONE sentinels).
constexpr std::array<CharacterBaseStatsEntry, 64> kCharacterBaseStats = {{
#include "data/generated/char_prop_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal its
// array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kCharacterBaseStats.size(); ++i) {
        if (static_cast<std::size_t>(kCharacterBaseStats[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kCharacterBaseStats entry id fields must match array positions");

}  // namespace

const CharacterBaseStats& getCharacterBaseStats(CharacterPropId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kCharacterBaseStats.size() &&
           "CharacterPropId out of range (0..63)");
    return kCharacterBaseStats[index].record;
}

std::span<const CharacterBaseStatsEntry> characterBaseStats() {
    return kCharacterBaseStats;
}

}  // namespace ostinato
