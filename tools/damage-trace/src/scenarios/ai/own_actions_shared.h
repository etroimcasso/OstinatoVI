// An own action as the bytes the cartridge queues it as, and those bytes put into the cells: shared
// with the cartridge's side, in namespace detail.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "battle/battler_set.h"

#include "scenarios/common/bytes.h"
#include "scenarios/damage/attack.h"

namespace ostinato::trace::detail {

// The battle's own actions' bytes as running a command reads them: which of kOwnActions runs it, the
// command, its attack byte and its targets.
struct OwnActionBytes {
    std::size_t routine = 0;
    std::uint8_t command = 0;
    std::uint8_t attack = 0;
    std::uint16_t targets = 0;
};

OwnActionBytes ownActionBytes(const battle::Action& action);

void putOwnActionCells(AttackResult& result, const std::array<bool, battle::kBattlerSlots>& waiting,
                       const std::array<int, battle::kBattlerSlots>& multiplier);

}  // namespace ostinato::trace::detail
