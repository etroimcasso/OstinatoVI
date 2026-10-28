// A battle command's aiming byte (CmdTargetTbl, C2/278A): the aiming rules a command starts
// from and which of the four ways of loading its attack it uses. It is a different byte from the
// command's targeting byte (Targeting) and is split three ways when the command is aimed
// (battle_main.asm:6463-6478):
//   * the $E1 bits are rules of their own: no party targets (bit 7), a random target (bit 6),
//     fall back to the command's earlier targets when none are left (bit 5), and never the user
//     (bit 0);
//   * the $18 bits are two more rules: bit 4, the command may be aimed at the fallen, and bit 3,
//     a target that is gone is not replaced;
//   * the $06 bits pick how the command's attack is loaded: a plain hit, an item, a spell, or a
//     thrown item or tool.
// The three masks partition all eight bits ($E1 | $18 | $06 == $FF, no overlap). sizeof == 1
// keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

namespace ostinato {

// How a command's attack is loaded when it is aimed.
enum class CommandAttackLoad : std::uint8_t {
    PlainHit,     // the "Battle" attack: Fight, Steal, Morph, Row, Jump and the other commands with no spell
    Item,         // the item in hand: Item
    Spell,        // the spell or skill chosen: Magic, Lore, Blitz, Dance, Summon and the rest
    ThrownItem,   // the item thrown or the tool used: Throw, Tools
};

struct CommandTargetingInit {
    std::uint8_t bits = 0;

    constexpr CommandTargetingInit() = default;
    explicit constexpr CommandTargetingInit(std::uint8_t raw) : bits(raw) {}

    // The $E1 field.
    constexpr std::uint8_t directBits() const {
        return static_cast<std::uint8_t>(bits & 0xE1);
    }
    // The $18 field.
    constexpr std::uint8_t initialTarget() const {
        return static_cast<std::uint8_t>(bits & 0x18);
    }
    // The $06 field: 0, 2, 4 or 6.
    constexpr std::uint8_t dispatchIndex() const {
        return static_cast<std::uint8_t>(bits & 0x06);
    }

    constexpr bool noCharacterTargets() const { return (bits & 0x80) != 0; }
    constexpr bool randomTarget() const { return (bits & 0x40) != 0; }
    constexpr bool fallBackToEarlierTargets() const { return (bits & 0x20) != 0; }
    constexpr bool mayTargetTheFallen() const { return (bits & 0x10) != 0; }
    constexpr bool keepsTargetsThatAreGone() const { return (bits & 0x08) != 0; }
    constexpr bool excludesTheUser() const { return (bits & 0x01) != 0; }

    constexpr CommandAttackLoad attackLoad() const {
        return static_cast<CommandAttackLoad>(dispatchIndex() >> 1);
    }
};

static_assert(sizeof(CommandTargetingInit) == 1,
              "CommandTargetingInit must be byte-identical to the ROM byte");

}  // namespace ostinato
