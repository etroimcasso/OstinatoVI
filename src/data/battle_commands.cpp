#include "data/battle_commands.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 30 real commands, keyed by BattleCommandId. Each row maps a command to
// its properties; the generated rows carry the command as a typed field.
constexpr std::array<BattleCommandPropertiesEntry, 30> kBattleCommandProperties =
    {{
#include "data/generated/battle_cmd_prop_data.inc"
    }};

// Every command-keyed table is in command-id order: the entry at position i is
// command i (FIGHT..MAGITEK are contiguous 0..29). This is the invariant the
// accessors rely on to index by command.
template <typename Table>
constexpr bool commandMatchesPosition(const Table& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].command != static_cast<BattleCommandId>(i)) {
            return false;
        }
    }
    return true;
}

static_assert(commandMatchesPosition(kBattleCommandProperties),
              "kBattleCommandProperties must be in command-id order");
static_assert(commandMatchesPosition(kCommandAdvanceWait),
              "kCommandAdvanceWait must be in command-id order");
static_assert(commandMatchesPosition(kCommandTargetingInit),
              "kCommandTargetingInit must be in command-id order");

}  // namespace

const BattleCommandProperties& battleCommandProperties(BattleCommandId cmd) {
    const auto i = static_cast<std::size_t>(cmd);
    assert(i < kBattleCommandProperties.size() && "battle command out of range");
    return kBattleCommandProperties[i].record;
}

std::span<const BattleCommandPropertiesEntry> battleCommandProperties() {
    return kBattleCommandProperties;
}

std::uint8_t commandAdvanceWait(BattleCommandId cmd) {
    const auto i = static_cast<std::size_t>(cmd);
    assert(i < kCommandAdvanceWait.size() && "battle command out of range");
    return kCommandAdvanceWait[i].wait;
}

CommandTargetingInit commandTargetingInit(BattleCommandId cmd) {
    const auto i = static_cast<std::size_t>(cmd);
    assert(i < kCommandTargetingInit.size() && "battle command out of range");
    return kCommandTargetingInit[i].init;
}

}  // namespace ostinato
