// The battle's hooks with a stand-in for its statuses taking effect, so the preview can fight a
// battle to its end before the battle applies statuses itself: after each attack, every battler's
// statuses to set are put on it and then its statuses to clear taken off, and both are emptied. A
// battler the damage brought to 0 HP is set to die, so it falls at once. It stands in until the
// battle applies statuses itself, and lives here so nothing in the game depends on it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "battle/battle_hooks.h"
#include "battle/battle_state.h"
#include "ostinato/status_set.h"

namespace ostinato::preview {

struct PendingStatusesStandIn : battle::BattleHooks {
    void applyPendingStatuses(battle::BattleState& state) override {
        for (battle::Battler& battler : state.battlers) {
            for (std::size_t i = 0; i < battler.status.bytes.size(); ++i) {
                battler.status.bytes[i] = static_cast<std::uint8_t>(
                    (battler.status.bytes[i] | battler.statusesToSet.bytes[i]) & ~battler.statusesToClear.bytes[i]);
            }
            battler.statusesToSet = {};
            battler.statusesToClear = {};
        }
    }
};

}  // namespace ostinato::preview
