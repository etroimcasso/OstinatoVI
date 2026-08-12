// The battle-usability bits an item's type contributes (ItemTypeMaskTbl,
// c2/5549). When the menu builds an item's battle state it shifts this byte left
// once and merges the result into the item's usability flags; the bit shifted
// out decides whether the equippable-characters calculation runs at all.
// The individual merged bits belong to the Phase-3 menu consumer, so this
// wrapper carries the raw byte and exposes the two reads that are pinned.
#pragma once

#include <cstdint>

namespace ostinato {

struct ItemTypeBattleFlags {
    std::uint8_t bits = 0;

    constexpr ItemTypeBattleFlags() = default;
    explicit constexpr ItemTypeBattleFlags(std::uint8_t raw) : bits(raw) {}

    // The bits merged into the item's battle-usability flags (this byte << 1).
    constexpr std::uint8_t mergedBits() const {
        return static_cast<std::uint8_t>(bits << 1);
    }
    // The high bit, which shifts into carry and short-circuits the
    // equippable-characters calculation for this item type.
    constexpr bool skipsEquippableCheck() const { return (bits & 0x80) != 0; }
};

static_assert(sizeof(ItemTypeBattleFlags) == 1,
              "ItemTypeBattleFlags must be byte-identical to the ROM byte");

}  // namespace ostinato
