// The cartridge's statuses run over a scenario, and a traced status call replayed.

#include "oracle/damage_oracle.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/status.h"
#include "data/ai_scripts.h"
#include "game/character_record.h"
#include "ostinato/status_set.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"
#include "oracle/routines.h"
#include "scenarios/common/bytes.h"
#include "scenarios/status/status_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

constexpr std::uint32_t kSleepTimer = kWorkRam + 0x3CF9;
constexpr std::uint32_t kStopTimer = kWorkRam + 0x3AF1;
constexpr std::uint32_t kReflectTimer = kWorkRam + 0x3F0C;
constexpr std::uint32_t kFreezeTimer = kWorkRam + 0x3F0D;
constexpr std::uint32_t kForCounter12 = kWorkRam + 0x3E60;
constexpr std::uint32_t kForCounter34 = kWorkRam + 0x3E74;
constexpr std::uint32_t kDiedThisAction = kWorkRam + 0x3A56;
}  // namespace

void DamageOracle::writeStatusCells(const DamageScenario& scenario) {
    const battle::BattleState& state = scenario.state;
    writeByte(kMonstersStanding, state.monstersStanding.monsters.bits);
    writeByte(kMonstersFallen, state.monstersFallen.monsters.bits);
    writeWord(kDiedThisAction, maskOf(state.diedAwaitingCounter));
    writeWord(kStatusChangedSet, maskOf(state.statusChanged));
    writeInFlightWord(kMissed, maskOf(scenario.attack.scratch.missed));
    writeInFlightWord(kMissedByEffect, 0);
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        const battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        writeWord(kBattlerBit + at, maskOf(battle::BattlerSet::of(slot)));
        writeByte(kInflictable12 + at, battler.inflictableStatuses.bytes[0]);
        writeByte(kInflictable12 + at + 1, battler.inflictableStatuses.bytes[1]);
        writeByte(kInflictable34 + at, battler.inflictableStatuses.bytes[2]);
        writeByte(kInflictable34 + at + 1, battler.inflictableStatuses.bytes[3]);
        std::uint8_t flags = battler.present ? kPresentBit : 0;
        flags |= battler.menuMayOpen ? kMenuMayOpenBit : 0;
        writeByte(kOnTheField + at, flags);
        std::uint8_t stance = 0;
        stance |= battler.defending ? kDefendingBit : 0;
        stance |= battler.backRow ? kBackRowBit : 0;
        stance |= battler.instantDeathProtected ? kInstantDeathProtectedBit : 0;
        stance |= battler.emptyGaugeAfterAction ? kEmptyGaugeBit : 0;
        writeByte(kStance + at, stance);
        writeByte(kToUpdate + at, pendingByte(battler.pendingUpdates));
        writeByte(kTriggers + at, battler.triggers.retort ? kRetortBit : 0);
        std::uint8_t triggers = battler.triggers.overcast ? kOvercastBit : 0;
        triggers |= battler.triggers.phantasm ? kPhantasmBit : 0;
        writeByte(kMoreTriggers + at, triggers);
        writeByte(kSleepTimer + at, battler.sleepTimer);
        writeByte(kStopTimer + at, battler.stopTimer);
        writeByte(kReflectTimer + at, battler.reflectTimer);
        writeByte(kFreezeTimer + at, battler.freezeTimer);
        writeByte(kForCounter12 + at, battler.statusForCounter.bytes[0]);
        writeByte(kForCounter12 + at + 1, battler.statusForCounter.bytes[1]);
        writeByte(kForCounter34 + at, battler.statusForCounter.bytes[2]);
        writeByte(kForCounter34 + at + 1, battler.statusForCounter.bytes[3]);
        if (slot.isCharacter()) {
            writeByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position()), battler.equipmentChanged ? 1 : 0);
        }
    }
}

StatusChangesResult DamageOracle::statusChanges(const DamageScenario& scenario) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeStatusCells(scenario);
    call([&] { updateStatus_(kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedRoutines[3]);
    return readStatusChangesResult();
}

StatusChangesResult DamageOracle::readStatusChangesResult() {
    StatusChangesResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const auto readStatuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        result.status[i] = readStatuses(kStatus12, kStatus34);
        result.statusesToSet[i] = readStatuses(kToSet12, kToSet34);
        result.statusesToClear[i] = readStatuses(kToClear12, kToClear34);
        result.statusForCounter[i] = readStatuses(kForCounter12, kForCounter34);
        result.pendingUpdates[i] = readByte(kToUpdate + at);
        result.menuMayOpen[i] = (readByte(kOnTheField + at) & kMenuMayOpenBit) != 0;
        result.emptyGaugeAfterAction[i] = (readByte(kStance + at) & kEmptyGaugeBit) != 0;
        result.phantasm[i] = (readByte(kMoreTriggers + at) & kPhantasmBit) != 0;
        if (slot.isCharacter()) {
            result.equipmentChanged[i] = readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0;
        }
        result.sleepTimer[i] = readByte(kSleepTimer + at);
        result.stopTimer[i] = readByte(kStopTimer + at);
        result.reflectTimer[i] = readByte(kReflectTimer + at);
        result.freezeTimer[i] = readByte(kFreezeTimer + at);
    }
    result.diedAwaitingCounter = setOf(readWord(kDiedThisAction));
    result.statusChanged = setOf(readWord(kStatusChangedSet));
    result.monstersStanding.monsters = BattleSlotMask(readByte(kMonstersStanding));
    result.monstersFallen.monsters = BattleSlotMask(readByte(kMonstersFallen));
    return result;
}

StatusEffectResult DamageOracle::statusEffect(const DamageScenario& scenario, battle::BattlerSlot target) {
    writeScenario(scenario);
    writeTargetCells(scenario);
    writeStatusCells(scenario);
    call([&] {
        magicStatusEffect_(slotIndex(scenario.attacker), slotIndex(target), kWideAccumulatorStatus, kBattleDataBank,
                           kBattleDirectPage);
    });
    trace_->returnToFrame(kTracedRoutines[4]);
    return readStatusEffectResult(target);
}

StatusEffectResult DamageOracle::readStatusEffectResult(battle::BattlerSlot target) {
    const auto at = static_cast<std::uint32_t>(slotIndex(target));
    return StatusEffectResult{
        .statusesToSet = StatusSet{{readByte(kToSet12 + at), readByte(kToSet12 + at + 1), readByte(kToSet34 + at),
                                    readByte(kToSet34 + at + 1)}},
        .statusesToClear = StatusSet{{readByte(kToClear12 + at), readByte(kToClear12 + at + 1),
                                      readByte(kToClear34 + at), readByte(kToClear34 + at + 1)}},
        .missedByEffect = setOf(readWord(kMissedByEffect)),
        .missed = setOf(readWord(kMissed)),
    };
}

DamageScenario DamageOracle::readStatusScenario() {
    DamageScenario s = readScenario();
    battle::BattleState& state = s.state;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const auto at = static_cast<std::uint32_t>(i * 2);
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(static_cast<int>(i));
        const auto statuses = [&](std::uint32_t first12, std::uint32_t first34) {
            return StatusSet{{readByte(first12 + at), readByte(first12 + at + 1), readByte(first34 + at),
                              readByte(first34 + at + 1)}};
        };
        battler.inflictableStatuses = statuses(kInflictable12, kInflictable34);
        battler.statusForCounter = statuses(kForCounter12, kForCounter34);
        const std::uint8_t flags = readByte(kOnTheField + at);
        battler.present = (flags & kPresentBit) != 0;
        battler.menuMayOpen = (flags & kMenuMayOpenBit) != 0;
        battler.emptyGaugeAfterAction = (readByte(kStance + at) & kEmptyGaugeBit) != 0;
        const std::uint8_t pending = readByte(kToUpdate + at);
        battler.pendingUpdates = battle::PendingUpdates{
            .leaveTheField = (pending & 0x01) != 0,
            .enabledMagic = (pending & 0x80) != 0,
            .removeAllActions = (pending & 0x40) != 0,
            .startCondemned = (pending & 0x20) != 0,
            .stopCondemned = (pending & 0x10) != 0,
            .commandList = (pending & 0x08) != 0,
            .speed = (pending & 0x04) != 0,
            .morph = (pending & 0x02) != 0,
        };
        battler.triggers.retort = (readByte(kTriggers + at) & kRetortBit) != 0;
        const std::uint8_t triggers = readByte(kMoreTriggers + at);
        battler.triggers.overcast = (triggers & kOvercastBit) != 0;
        battler.triggers.phantasm = (triggers & kPhantasmBit) != 0;
        battler.sleepTimer = readByte(kSleepTimer + at);
        battler.stopTimer = readByte(kStopTimer + at);
        battler.reflectTimer = readByte(kReflectTimer + at);
        battler.freezeTimer = readByte(kFreezeTimer + at);
        if (slot.isCharacter()) {
            battler.equipmentChanged = readByte(kEquipmentChanged + static_cast<std::uint32_t>(slot.position())) != 0;
        }
    }
    state.diedAwaitingCounter = setOf(readWord(kDiedThisAction));
    state.statusChanged = setOf(readWord(kStatusChangedSet));
    state.monstersStanding.monsters = BattleSlotMask(readByte(kMonstersStanding));
    state.monstersFallen.monsters = BattleSlotMask(readByte(kMonstersFallen));
    return s;
}

std::string DamageOracle::replayStatusCall(const TracedCall& call, const TracedRoutine& routine) {
    // The battle as the oracle writes a default scenario, then every cell the routine read as it read it.
    const DamageScenario plain;
    writeScenario(plain);
    writeTargetCells(plain);
    writeApplyCells(plain);
    writeStatusCells(plain);
    for (const auto& [cell, value] : call.inputs) {
        writeByte(cell, value);
    }
    const DamageScenario s = readStatusScenario();
    // The cartridge's answer: the scenario's cells as the oracle writes them, then the routine's writes.
    const auto guestCells = [&] {
        writeScenario(s);
        writeTargetCells(s);
        writeStatusCells(s);
        for (const auto& [cell, value] : call.writes) {
            writeByte(cell, value);
        }
    };

    if (routine.entry == kUpdateStatus) {
        const StatusChangesResult port = portStatusChanges(s);
        guestCells();
        return differences(port, readStatusChangesResult());
    }

    const std::optional<battle::BattlerSlot> found = statusCallTarget(call);
    if (!found) {
        throw std::runtime_error("MagicStatusEffect touched more than one battler's cells, or none; its target is the "
                                 "one it touches");
    }
    const battle::BattlerSlot target = *found;
    const StatusEffectResult port = portStatusEffect(s, target);
    guestCells();
    return differences(port, readStatusEffectResult(target));
}

}  // namespace ostinato::trace
