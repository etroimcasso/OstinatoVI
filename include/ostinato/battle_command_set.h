// A four-byte set of battle commands: one bit per BATTLE_CMD value 0-31. The
// bit order is the ROM's GetBitPtr layout (battle_main.asm:13474) — command n
// lives in byte n/8, bit 1<<(n%8), little-endian across the four bytes. The
// muddled/berserk/retarget command tables are stored this way; has() answers
// "is this command in the set?" and matches the ROM's membership test bit-for-
// bit. sizeof == 4 keeps it byte-identical to the ROM's four-byte mask.
#pragma once

#include <array>
#include <concepts>
#include <cstdint>

#include "ostinato/battle_command_id.h"

namespace ostinato {

struct BattleCommandSet {
    std::array<std::uint8_t, 4> bytes = {};

    // Is command `cmd` a member of the set? Byte n/8, bit n%8 (GetBitPtr order).
    constexpr bool has(BattleCommandId cmd) const {
        const auto n = static_cast<std::uint8_t>(cmd);
        return (bytes[n >> 3] & static_cast<std::uint8_t>(1u << (n & 0x07))) != 0;
    }

    constexpr void set(BattleCommandId cmd) {
        const auto n = static_cast<std::uint8_t>(cmd);
        bytes[n >> 3] |= static_cast<std::uint8_t>(1u << (n & 0x07));
    }

    // Builder over the member commands: BattleCommandSet::of(FIGHT, STEAL, ...).
    // Zero arguments yields the empty set (an all-zero ROM mask).
    static constexpr BattleCommandSet of(std::same_as<BattleCommandId> auto... cmds) {
        BattleCommandSet result{};
        (result.set(cmds), ...);
        return result;
    }
};

static_assert(sizeof(BattleCommandSet) == 4,
              "BattleCommandSet must be byte-identical to the ROM's four-byte "
              "command mask");

}  // namespace ostinato
