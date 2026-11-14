// Conversions between the port's values and the bytes the cartridge keeps them in: a battler's slot
// as an index, a bit or a byte, battler sets as masks, links, statuses waiting, a command, the
// triggers and a special effect's index. Every side of the oracle reads and writes cells through
// these, in namespace detail.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "ostinato/battle_command_id.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"

namespace ostinato::trace::detail {

using namespace cells;

std::uint8_t markByte(const battle::Battler& battler);

std::uint16_t slotIndex(battle::BattlerSlot slot);

// A battler set as the battle program holds it: a word with the party members' bits in its low byte
// and the monsters' in its high byte.
std::uint16_t maskOf(battle::BattlerSet set);

battle::BattlerSet setOf(std::uint16_t mask);

battle::SpecialEffect effectAt(std::uint8_t index);

// A battler named by a byte holding its slot index times two, or no one.
std::optional<battle::BattlerSlot> slotTimesTwo(std::uint8_t value);

// The battler a per-battler cell belongs to, if it is one.
std::optional<int> battlerOfCell(std::uint32_t cell);

battle::BattlerSlot slotNamed(std::uint8_t index, const char* what);

// What is to be worked out again, as the cartridge's byte holds it.
std::uint8_t pendingByte(const battle::PendingUpdates& pending);

void writeThreeBytes(std::array<std::uint8_t, 3>& bytes, std::uint32_t value);

std::uint8_t slotByte(const std::optional<battle::BattlerSlot>& slot);

std::uint8_t linkByte(const std::optional<battle::BattlerSlot>& slot, bool ending);

std::uint8_t commandByte(const std::variant<BattleCommandId, battle::SpecialAnimation>& what);

std::uint8_t triggersByte(const battle::BattlerTriggers& triggers);

battle::BattlerSet seizedSet(const battle::BattleState& state);

std::optional<battle::BattlerSlot> slotOrNone(std::uint8_t value);

constexpr std::uint8_t commandByte(BattleCommandId command) {
    return static_cast<std::uint8_t>(command);
}

}  // namespace ostinato::trace::detail
