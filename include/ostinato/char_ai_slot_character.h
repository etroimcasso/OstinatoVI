// A character slot's first byte in a scripted battle setup (char_ai.asm:23-27).
// Packs which character properties the slot uses into the low six bits, with two
// flags above them: the slot acts as an enemy (facing the other way and fighting
// the party), and the character is not in the party (excluded from entry and
// victory animations, and its name and gauge hidden). The whole byte is $FF when
// the slot is unused. sizeof == 1 keeps it byte-identical to the ROM byte.
//
// Bits 6 and 7 are not the same flags as the record's own byte carries — see
// CharAiFlags, which is deliberately a separate type despite sharing bit values.
#pragma once

#include <cstdint>

#include "ostinato/character_prop_id.h"

namespace ostinato {

struct CharAiSlotCharacter {
    std::uint8_t bits = 0xFF;

    constexpr CharAiSlotCharacter() = default;
    explicit constexpr CharAiSlotCharacter(std::uint8_t raw) : bits(raw) {}

    // A character fighting on the player's side, in the party.
    static constexpr CharAiSlotCharacter ally(CharacterPropId character) {
        return CharAiSlotCharacter{static_cast<std::uint8_t>(character)};
    }
    // A character fighting against the party.
    static constexpr CharAiSlotCharacter enemy(CharacterPropId character) {
        return CharAiSlotCharacter{
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(character) | 0x40)};
    }
    // A character on the player's side who is not a party member.
    static constexpr CharAiSlotCharacter notInParty(CharacterPropId character) {
        return CharAiSlotCharacter{
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(character) | 0x80)};
    }
    // An opposing character who is not a party member.
    static constexpr CharAiSlotCharacter absentEnemy(CharacterPropId character) {
        return CharAiSlotCharacter{
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(character) | 0xC0)};
    }

    // The whole byte is the sentinel: this slot holds no character, and the
    // party character in the corresponding position is used instead.
    constexpr bool isEmpty() const { return bits == 0xFF; }

    // Bits 0-5: which character-properties record the slot fights with. Only
    // meaningful when the slot is not empty.
    constexpr CharacterPropId characterProperties() const {
        return static_cast<CharacterPropId>(bits & 0x3F);
    }

    // Bit 6: the slot faces the other way and acts as an enemy.
    constexpr bool isEnemy() const { return !isEmpty() && (bits & 0x40) != 0; }
    // Bit 7: the character is not in the party.
    constexpr bool isNotInParty() const {
        return !isEmpty() && (bits & 0x80) != 0;
    }
};

static_assert(sizeof(CharAiSlotCharacter) == 1,
              "CharAiSlotCharacter must be byte-identical to the ROM byte");

}  // namespace ostinato
