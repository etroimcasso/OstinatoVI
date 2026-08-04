// Hand-written port-design (PLAN phase-1.B D3; row shape per the house
// self-labeling rule). The rows are parser-emitted
// (src/data/generated/battle_magic_points_data.inc); this header owns the
// entry type, the array, and the accessor.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ostinato {

// One entry of the per-battle magic-points table: the battle (formation)
// index and the magic-point award stored there. Identity is a field, never a
// comment — every generated row reads { .battleIndex = N, .magicPoints = M }
// (both decimal: the identity and a semantic magnitude), so no value is
// positionally opaque.
struct BattleMagicPointsEntry {
    std::uint16_t battleIndex;
    std::uint8_t magicPoints;
};

// kBattleMagicPoints — the per-battle magic-points table
// (battle_magic_points.dat, ROM DF/B400), 512 entries in battle-index order.
// The original's reward logic guards the read: formations >= 512 award 0
// magic points without touching the table. That guard is consumer logic
// (ported with the battle reward phase), NOT data-layer scope — this table is
// strictly the 512 ROM entries and the accessor asserts its bound.
inline constexpr std::array<BattleMagicPointsEntry, 512> kBattleMagicPoints = {{
#include "data/generated/battle_magic_points_data.inc"
}};

// Self-consistency of the emitted rows: every entry's battleIndex field must
// equal its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kBattleMagicPoints.size(); ++i) {
        if (kBattleMagicPoints[i].battleIndex != i) {
            return false;
        }
    }
    return true;
}(), "kBattleMagicPoints entry battleIndex fields must match array positions");

// The magic-point award the original reads at BattleMagicPoints+index.
// Callers must pre-apply the >= 512 guard (reward logic); the accessor
// debug-asserts the strict bound.
std::uint8_t magicPointsForBattle(std::uint16_t battleIndex);

}  // namespace ostinato
