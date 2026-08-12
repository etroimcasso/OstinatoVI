// The per-command targeting-init byte (CmdTargetTbl, c2/278a) — a packed
// carrier distinct from the TARGET byte on an attack/command record. InitTarget
// (battle_main.asm:6463) splits it into three fields:
//   * directBits    (& $E1) are copied straight into the targeting work byte;
//   * initialTarget (& $18) selects the initial cursor target;
//   * dispatchIndex (& $06) selects one of the init-target handlers.
// The three masks partition all eight bits ($E1 | $18 | $06 == $FF, no overlap).
// The individual meanings of the $E1 bits belong to the Phase-3 targeting
// consumer, so this wrapper carries the raw byte and exposes the three field
// reads. sizeof == 1 keeps it byte-identical to the ROM byte.
#pragma once

#include <cstdint>

namespace ostinato {

struct CommandTargetingInit {
    std::uint8_t bits = 0;

    constexpr CommandTargetingInit() = default;
    explicit constexpr CommandTargetingInit(std::uint8_t raw) : bits(raw) {}

    // Bits copied directly into the targeting work byte ($BA) at InitTarget.
    constexpr std::uint8_t directBits() const {
        return static_cast<std::uint8_t>(bits & 0xE1);
    }
    // The initial-target selector ($18 field; OR'd in >>1 by the consumer).
    constexpr std::uint8_t initialTarget() const {
        return static_cast<std::uint8_t>(bits & 0x18);
    }
    // The init-target handler index ($06 field; 0/2/4/6 into InitTargetTbl).
    constexpr std::uint8_t dispatchIndex() const {
        return static_cast<std::uint8_t>(bits & 0x06);
    }
};

static_assert(sizeof(CommandTargetingInit) == 1,
              "CommandTargetingInit must be byte-identical to the ROM byte");

}  // namespace ostinato
