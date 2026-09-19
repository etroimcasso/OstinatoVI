// A battle background's graphics-block byte (BattleBGProp bytes 0-2). Packs a
// graphics-block id with the double-width flag. TfrBattleBGGfx
// (btlgfx_main.asm:4002-4006) skips the block when the whole byte is $FF and
// otherwise masks the low 7 bits (and #$7f) to index BattleBGGfxPtrs;
// btlgfx_main.asm:4050-4054 tests bit 7 (bmi) to transfer $2000 bytes instead
// of $1000. sizeof == 1 keeps it byte-identical to the ROM byte.
//
// Only the first of a record's three blocks carries the flag
// (battle_bg.asm:44); the second and third are plain ids.
#pragma once

#include <cstdint>

#include "ostinato/battle_background_graphics_id.h"

namespace ostinato {

struct BattleBackgroundGraphicsSlot {
    std::uint8_t bits = 0;

    constexpr BattleBackgroundGraphicsSlot() = default;
    explicit constexpr BattleBackgroundGraphicsSlot(std::uint8_t raw)
        : bits(raw) {}

    // A 64-tile block: the id alone, no flag.
    static constexpr BattleBackgroundGraphicsSlot single(
        BattleBackgroundGraphicsId id) {
        return BattleBackgroundGraphicsSlot{static_cast<std::uint8_t>(id)};
    }

    // A 128-tile block. The record's second block is not loaded when this is
    // set — the first block occupies both slots' worth of VRAM.
    static constexpr BattleBackgroundGraphicsSlot doubleWidth(
        BattleBackgroundGraphicsId id) {
        return BattleBackgroundGraphicsSlot{
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(id) | 0x80)};
    }

    // Bits 0-6: which graphics block this slot loads. Reads NONE when the byte
    // is the $FF sentinel, which is the loader's "do not load" case.
    constexpr BattleBackgroundGraphicsId id() const {
        return isEmpty() ? BattleBackgroundGraphicsId::NONE
                         : static_cast<BattleBackgroundGraphicsId>(bits & 0x7F);
    }

    // Bit 7: the block is 128 tiles rather than 64.
    constexpr bool isDoubleWidth() const { return (bits & 0x80) != 0; }

    // The whole byte is $FF: nothing is loaded into this slot. Note that a
    // slot holding 0 is NOT empty — it loads block 0.
    constexpr bool isEmpty() const { return bits == 0xFF; }
};

static_assert(sizeof(BattleBackgroundGraphicsSlot) == 1,
              "BattleBackgroundGraphicsSlot must be byte-identical to the ROM "
              "byte");

}  // namespace ostinato
