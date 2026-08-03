#include "data/character.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 64-record table. The parser-emitted rows are the transcribed contract data;
// this array + its type are the port surface. Designated initializers at every
// row keep each field self-labeling; empty records are the ROM's zero-filled
// padding slots (all-zero, distinct from the 0xFF EMPTY/NONE sentinels).
constexpr std::array<CharacterBaseStats, 64> kCharacterBaseStats = {{
#include "data/generated/char_prop_data.inc"
}};

}  // namespace

const CharacterBaseStats& getCharacterBaseStats(CharacterPropId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kCharacterBaseStats.size() &&
           "CharacterPropId out of range (0..63)");
    return kCharacterBaseStats[index];
}

std::span<const CharacterBaseStats> characterBaseStats() {
    return kCharacterBaseStats;
}

}  // namespace ostinato
