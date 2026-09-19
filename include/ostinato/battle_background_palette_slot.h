// A battle background's palette byte (BattleBGProp byte 5). Packs the palette
// id with the wavy-effect flag. btlgfx_main.asm:4115-4118 reads the byte for
// both purposes: bit 7 selects the HDMA wavy background (the desert case named
// in the source comment), and the low bits index the palette loaded into BG
// palettes 5-7. sizeof == 1 keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

#include "ostinato/battle_background_palette_id.h"

namespace ostinato {

struct BattleBackgroundPaletteSlot {
    std::uint8_t bits = 0;

    constexpr BattleBackgroundPaletteSlot() = default;
    explicit constexpr BattleBackgroundPaletteSlot(std::uint8_t raw)
        : bits(raw) {}

    // The palette, drawn without the wavy effect.
    static constexpr BattleBackgroundPaletteSlot plain(
        BattleBackgroundPaletteId id) {
        return BattleBackgroundPaletteSlot{static_cast<std::uint8_t>(id)};
    }

    // The palette, with the background's wavy HDMA effect applied.
    static constexpr BattleBackgroundPaletteSlot wavy(
        BattleBackgroundPaletteId id) {
        return BattleBackgroundPaletteSlot{
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(id) | 0x80)};
    }

    // Bits 0-6: the palette loaded into BG palettes 5, 6 and 7.
    constexpr BattleBackgroundPaletteId id() const {
        return static_cast<BattleBackgroundPaletteId>(bits & 0x7F);
    }

    // Bit 7: the background is drawn with the wavy effect.
    constexpr bool hasWavyEffect() const { return (bits & 0x80) != 0; }
};

static_assert(sizeof(BattleBackgroundPaletteSlot) == 1,
              "BattleBackgroundPaletteSlot must be byte-identical to the ROM "
              "byte");

}  // namespace ostinato
