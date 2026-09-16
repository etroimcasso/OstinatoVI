// A character slot's graphics byte in a scripted battle setup
// (char_ai.asm:28). Either a specific sprite sheet, or the $FF sentinel meaning
// "whatever the slot's character properties already use". sizeof == 1 keeps it
// byte-identical to the ROM byte.
#pragma once

#include <cstdint>

#include "ostinato/character_gfx_id.h"

namespace ostinato {

struct CharAiSlotGraphics {
    std::uint8_t bits = 0xFF;

    constexpr CharAiSlotGraphics() = default;
    explicit constexpr CharAiSlotGraphics(std::uint8_t raw) : bits(raw) {}

    // Draw the slot with the graphics its character properties name.
    static constexpr CharAiSlotGraphics fromCharacter() {
        return CharAiSlotGraphics{0xFF};
    }
    // Draw the slot with a specific sprite sheet instead.
    static constexpr CharAiSlotGraphics of(CharacterGfxId graphics) {
        return CharAiSlotGraphics{static_cast<std::uint8_t>(graphics)};
    }

    // No sheet is named here; the character's own graphics are used.
    constexpr bool usesCharacterDefault() const { return bits == 0xFF; }

    // The sheet this slot draws with. Only meaningful when
    // usesCharacterDefault() is false.
    constexpr CharacterGfxId id() const {
        return static_cast<CharacterGfxId>(bits);
    }
};

static_assert(sizeof(CharAiSlotGraphics) == 1,
              "CharAiSlotGraphics must be byte-identical to the ROM byte");

}  // namespace ostinato
