// What the statuses taking effect leave on the port's side, and the target of a traced status call:
// shared with the whole attack and the trace's parser, in namespace detail.
#pragma once

#include <optional>

#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"

#include "cells/status/status.h"
#include "scenarios/common/bytes.h"
#include "scenarios/status/status.h"
#include "trace/trace_parse.h"

namespace ostinato::trace::detail {

using namespace cells;

// What the state holds of the statuses taking effect, read as a StatusChangesResult.
StatusChangesResult statusChangesOf(const battle::BattleState& state);

// The target of an attack's statuses on one target, as its call shows it: the one battler whose cells
// it touched.
std::optional<battle::BattlerSlot> statusCallTarget(const TracedCall& call);

}  // namespace ostinato::trace::detail
