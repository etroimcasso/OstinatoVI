// The battle's own actions run on the cartridge and read back as a whole attack.

#include "oracle/damage_oracle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"

#include "cells/ai/ai.h"
#include "cells/damage/frame.h"
#include "cells/damage/per_target.h"
#include "oracle/routines.h"
#include "scenarios/ai/own_actions_shared.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

// Per battler, at twice its index: Poison's multiplier, which grows at each beat.
constexpr std::uint32_t kPoisonMultiplier = kWorkRam + 0x3E24;
}  // namespace

AttackResult DamageOracle::ownAction(const DamageScenario& scenario, const battle::Action& action) {
    if (std::holds_alternative<battle::AiEffectAction>(action)) {
        return aiCommand(AiScenario{.state = scenario.state, .actor = scenario.attacker}, action).attack;
    }
    const OwnActionBytes bytes = ownActionBytes(action);
    const battle::BattleState& state = scenario.state;
    battle::AttackInFlight starting;
    starting.attackerLevel = state.lastAttackLevel;
    writeAttackCells(DamageScenario{.state = state, .attack = starting, .attacker = scenario.attacker});
    writeCommandStart();
    writeByte(kEnemyCount, state.enemyCount);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        writeByte(kStance + at, static_cast<std::uint8_t>(readByte(kStance + at) |
                                                          (battler.dotActionPending ? kDamageOverTimeWaitingBit : 0)));
        writeByte(kOnTheField + at, static_cast<std::uint8_t>(readByte(kOnTheField + at) |
                                                              (battler.heldByStatus ? kHeldByStatusBit : 0)));
        writeByte(kPoisonMultiplier + at, static_cast<std::uint8_t>(battler.poisonMultiplier));
    }
    // The action's bytes where its command reads them, and the command and attack bytes the action
    // leaves for the battle program as it runs.
    writeByte(kCommand, bytes.command);
    writeByte(kCommandAttack, bytes.attack);
    writeWord(kCommandTargets, bytes.targets);
    writeByte(kQueuedCommand, bytes.command);
    writeByte(kQueuedAttack, bytes.attack);
    call([&] {
        ownActions_[bytes.routine](slotIndex(scenario.attacker), kByteWideStatus, kBattleDataBank, kBattleDirectPage);
    });
    AttackResult result = readAttackResult(false);
    std::array<bool, battle::kBattlerSlots> waiting{};
    std::array<int, battle::kBattlerSlots> multiplier{};
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        waiting[i] = (readByte(kStance + at) & kDamageOverTimeWaitingBit) != 0;
        multiplier[i] = readByte(kPoisonMultiplier + at);
    }
    putOwnActionCells(result, waiting, multiplier);
    return result;
}

}  // namespace ostinato::trace
