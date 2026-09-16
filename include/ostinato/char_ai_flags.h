// A scripted battle setup's flag byte (CharAI byte 0). Two independent bits
// (char_ai.asm:16-18): one hides every character's name and gauge in the battle
// menu, the other hides the player's own party so only the setup's cast is
// shown. sizeof == 1 keeps it byte-identical to the ROM byte.
//
// These bit values are reused by a slot's own flag byte with entirely different
// meanings — see CharAiSlotCharacter, which is deliberately a separate type.
#pragma once

#include <cstdint>

namespace ostinato {

struct CharAiFlags {
    std::uint8_t bits = 0;

    constexpr CharAiFlags() = default;
    explicit constexpr CharAiFlags(std::uint8_t raw) : bits(raw) {}

    static constexpr CharAiFlags none() { return CharAiFlags{0x00}; }
    static constexpr CharAiFlags hideNames() { return CharAiFlags{0x01}; }
    static constexpr CharAiFlags hideParty() { return CharAiFlags{0x80}; }

    // Chained so a record carrying both bits still reads as the two things it
    // is rather than as a combined literal.
    constexpr CharAiFlags andHideNames() const {
        return CharAiFlags{static_cast<std::uint8_t>(bits | 0x01)};
    }
    constexpr CharAiFlags andHideParty() const {
        return CharAiFlags{static_cast<std::uint8_t>(bits | 0x80)};
    }

    // Bit 0: names and gauges are hidden for every character in the battle menu.
    constexpr bool hidesNames() const { return (bits & 0x01) != 0; }
    // Bit 7: the player's party is hidden, leaving only the setup's own cast.
    constexpr bool hidesPartyCharacters() const { return (bits & 0x80) != 0; }
};

static_assert(sizeof(CharAiFlags) == 1,
              "CharAiFlags must be byte-identical to the ROM byte");

}  // namespace ostinato
