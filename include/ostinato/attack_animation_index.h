// A reference from the usable-item animation table (ItemAnimPtrs) into the
// AttackAnimProp table. In the ROM each entry is a pre-multiplied byte offset
// (row * 14) loaded straight into the animation-data pointer (CmdAnim_01,
// btlgfx_main.asm:27959-27972), with $ffff meaning "no animation". Storing the
// pre-multiplied offset would bake the record width into the data (a mechanism
// shape), so this wrapper carries the row index instead — the parser divides
// the ROM word by 14 on the way in, and the test re-multiplies to prove byte
// identity. sizeof == 2 keeps the wrapper the width of the ROM word.
#pragma once

#include <cstdint>

namespace ostinato {

struct AttackAnimationIndex {
    // The AttackAnimProp row index, or 0xFFFF for "no animation". Real indices
    // are far below the sentinel (the corpus uses 337-402).
    std::uint16_t raw = 0xFFFF;

    // $ffff in the ROM: this item has no attack animation.
    constexpr bool isNone() const { return raw == 0xFFFF; }
    // The AttackAnimProp row index (valid only when !isNone()).
    constexpr std::uint16_t index() const { return raw; }

    // Builder so every entry names a decimal row index — never a pre-multiplied
    // hex offset.
    static constexpr AttackAnimationIndex of(std::uint16_t index) {
        return AttackAnimationIndex{index};
    }
    // The $ffff sentinel, so a "no animation" entry is a named value.
    static const AttackAnimationIndex NONE;
};

inline constexpr AttackAnimationIndex AttackAnimationIndex::NONE =
    AttackAnimationIndex{0xFFFF};

static_assert(sizeof(AttackAnimationIndex) == 2,
              "AttackAnimationIndex must be the width of the ROM word");
static_assert(AttackAnimationIndex::of(402).index() == 402 &&
                  !AttackAnimationIndex::of(402).isNone() &&
                  AttackAnimationIndex::NONE.isNone(),
              "AttackAnimationIndex must round-trip the row index and sentinel");

}  // namespace ostinato
