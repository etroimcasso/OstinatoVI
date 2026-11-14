// What taking and showing the damage leaves on the port's side, read back as the cells the cartridge
// holds: shared with the whole attack's comparison, in namespace detail.
#pragma once

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/items_obtained.h"

#include "scenarios/common/bytes.h"
#include "scenarios/damage/damage.h"

namespace ostinato::trace::detail {

// What the port's state and script hold after the damage is taken and shown.
ApplyDamageResult portApplied(const battle::BattleState& state, const battle::AttackInFlight& attack,
                              const battle::BattleScriptBuilder& script);

}  // namespace ostinato::trace::detail
