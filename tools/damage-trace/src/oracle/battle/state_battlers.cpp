// Each battler's clocks, counters and own cells, laid into the cells as the cartridge holds them: the
// ATB gauge and its rate, the advance wait and its counter, the status timers, the damage-over-time
// beat and multiplier, the Morph gauge and the condemned count, the speed, the equipment's statuses
// and relics, the run's progress, the experience, the dance and the name it is listed under.

#include "oracle/damage_oracle.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "ostinato/item_id.h"

#include "cells/battle/state.h"
#include "cells/status/effects.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {
// A word naming no monster.
constexpr std::uint16_t kNoName = 0xFFFF;
// A dance not chosen.
constexpr std::uint8_t kNoDance = 0xFF;
// The equipment's statuses the cells hold: statuses 2 and 3 (StatusSet's second and third bytes).
constexpr std::size_t kStatus2Byte = 1;
constexpr std::size_t kStatus3Byte = 2;
}  // namespace

void DamageOracle::layBattlersAsTheCartridgeHoldsIt(const battle::BattleState& state) {
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);

        layWordAsTheCartridgeHoldsIt(kGauge + at, battler.gauge.counter);
        layByteAsTheCartridgeHoldsIt(kAdvanceWaitDuration + at, battler.advanceWait.value_or(kNoAdvanceWait));
        layWordAsTheCartridgeHoldsIt(kAdvanceWaitCounter + at, battler.advanceWaitCounter);
        layWordAsTheCartridgeHoldsIt(kAtbRateCell + at, battler.atbRate);
        layByteAsTheCartridgeHoldsIt(kStatusTimer + at, battler.statusTimer);
        layByteAsTheCartridgeHoldsIt(kStatusTimerStep + at, battler.statusTimerStep);
        layByteAsTheCartridgeHoldsIt(kDamageOverTimePhase + at, battler.damageOverTimePhase);
        layByteAsTheCartridgeHoldsIt(kMorphGauge + at, battler.morphGauge);
        layByteAsTheCartridgeHoldsIt(kCondemnedNumber + at, battler.condemnedNumber);
        layByteAsTheCartridgeHoldsIt(kSpeed + at, battler.speed);
        layByteAsTheCartridgeHoldsIt(kSpeedAgain + at,
                                     battle::BattlerSlot::fromIndex(static_cast<int>(i)).isMonster() ? 0 : battler.speed);
        layByteAsTheCartridgeHoldsIt(kRunProgress + at, static_cast<std::uint8_t>(battler.runProgress));
        layByteAsTheCartridgeHoldsIt(kRunFactor + at, battler.runFactor);
        layWordAsTheCartridgeHoldsIt(kExperience + at, battler.experience);
        layByteAsTheCartridgeHoldsIt(kPoisonMultiplier + at, static_cast<std::uint8_t>(battler.poisonMultiplier));
        layByteAsTheCartridgeHoldsIt(kDanceDanced + at, battler.dance ? static_cast<std::uint8_t>(*battler.dance) : kNoDance);
        layWordAsTheCartridgeHoldsIt(kNameShownAs + at,
                                     battler.nameGroup ? static_cast<std::uint16_t>(*battler.nameGroup) : kNoName);

        // A battler with no party member in it wears nothing: its relic and equipment cells hold the
        // start's zero fill (InitBattle :6056-6060), which no field determines.
        if (battler.actor) {
            layByteAsTheCartridgeHoldsIt(kEquipmentStatus23 + at, battler.equipmentStatuses.bytes[kStatus2Byte]);
            layByteAsTheCartridgeHoldsIt(kEquipmentStatus23 + at + 1, battler.equipmentStatuses.bytes[kStatus3Byte]);
            layByteAsTheCartridgeHoldsIt(kRelics + at, static_cast<std::uint8_t>(battler.relics[0]));
            layByteAsTheCartridgeHoldsIt(kRelics + at + 1, static_cast<std::uint8_t>(battler.relics[1]));
        } else {
            leaveOut(kEquipmentStatus23 + at, 2);
            leaveOut(kRelics + at, 2);
        }

        // The counterattacks waiting. A battle's start has none; a list of them is laid with the
        // actions' (writeTargetEffectCells()), which lays only the turn's.
        if (!battler.counterActions.empty()) {
            throw std::logic_error("a battle state with counterattacks waiting cannot be laid: the counterattack lists "
                                   "are not written");
        }
        layByteAsTheCartridgeHoldsIt(kFirstCounterAction + at, kFreeSlot);
    }
}

}  // namespace ostinato::trace
