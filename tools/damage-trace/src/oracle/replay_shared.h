// The attacker of a traced call that came from a bounce: shared with the special effects' replay, in
// namespace detail.
#pragma once

#include "battle/battler_set.h"

#include "cells/damage/per_target.h"
#include "scenarios/common/bytes.h"
#include "trace/cells.h"

namespace ostinato::trace::detail {

using namespace cells;

// Who a bounce's call is made for. A spell bounced off Reflect: the lowest slot whose byte in the
// bounce's block is set is the battler it bounced off this time. A Launcher's or Super Ball's hits:
// the lowest battler in the bounces that hit (bytes 14-15), which takes its bit just before the call,
// the bounce going last slot first — for their own hits, the attacker.
battle::BattlerSlot bounceAttacker(const TracedCall& call);

}  // namespace ostinato::trace::detail
