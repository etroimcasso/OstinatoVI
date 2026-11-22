// The port's statuses over a scenario, their differences, and the status sweeps' draws.

#include "scenarios/status/status.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <string>

#include "battle/attack.h"
#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/command_executor.h"
#include "battle/items_obtained.h"
#include "battle/status.h"
#include "data/ai_scripts.h"
#include "data/attack_properties.h"
#include "game/character_record.h"
#include "ostinato/attack_flags.h"
#include "ostinato/status_set.h"

#include "cells/status/status.h"
#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/damage.h"
#include "scenarios/status/status_shared.h"
#include "trace/trace_parse.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace detail {

StatusChangesResult statusChangesOf(const battle::BattleState& state) {
    StatusChangesResult result;
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const battle::Battler& battler = state.battlers[i];
        result.status[i] = battler.status;
        result.statusesToSet[i] = battler.statusesToSet;
        result.statusesToClear[i] = battler.statusesToClear;
        result.statusForCounter[i] = battler.statusForCounter;
        result.pendingUpdates[i] = pendingByte(battler.pendingUpdates);
        result.menuMayOpen[i] = battler.menuMayOpen;
        result.emptyGaugeAfterAction[i] = battler.emptyGaugeAfterAction;
        result.phantasm[i] = battler.triggers.phantasm;
        result.sleepTimer[i] = battler.sleepTimer;
        result.stopTimer[i] = battler.stopTimer;
        result.reflectTimer[i] = battler.reflectTimer;
        result.freezeTimer[i] = battler.freezeTimer;
    }
    result.equipmentChangeFlags = state.equipmentChangeFlags;
    result.menuWindowDesign = state.menuWindowDesign;
    result.diedAwaitingCounter = state.diedAwaitingCounter;
    result.statusChanged = state.statusChanged;
    result.monstersStanding = state.monstersStanding.monstersOnly();
    result.monstersFallen = state.monstersFallen.monstersOnly();
    return result;
}
}  // namespace detail

StatusChangesResult portStatusChanges(DamageScenario scenario) {
    battle::applyStatusChanges(scenario.state);
    return statusChangesOf(scenario.state);
}

std::string differences(const StatusChangesResult& port, const StatusChangesResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        const std::string slot = " of battler " + std::to_string(i);
        note("statuses" + slot, statusesText(port.status[i]), statusesText(guest.status[i]));
        note("statuses to set" + slot, statusesText(port.statusesToSet[i]), statusesText(guest.statusesToSet[i]));
        note("statuses to clear" + slot, statusesText(port.statusesToClear[i]), statusesText(guest.statusesToClear[i]));
        note("statuses for a counterattack" + slot, statusesText(port.statusForCounter[i]),
             statusesText(guest.statusForCounter[i]));
        note("to work out again" + slot, std::to_string(port.pendingUpdates[i]), std::to_string(guest.pendingUpdates[i]));
        note("menu may open" + slot, std::to_string(port.menuMayOpen[i]), std::to_string(guest.menuMayOpen[i]));
        note("gauge empties after the action" + slot, std::to_string(port.emptyGaugeAfterAction[i]),
             std::to_string(guest.emptyGaugeAfterAction[i]));
        note("Phantasm" + slot, std::to_string(port.phantasm[i]), std::to_string(guest.phantasm[i]));
        note("sleep timer" + slot, std::to_string(port.sleepTimer[i]), std::to_string(guest.sleepTimer[i]));
        note("stop timer" + slot, std::to_string(port.stopTimer[i]), std::to_string(guest.stopTimer[i]));
        note("reflect timer" + slot, std::to_string(port.reflectTimer[i]), std::to_string(guest.reflectTimer[i]));
        note("freeze timer" + slot, std::to_string(port.freezeTimer[i]), std::to_string(guest.freezeTimer[i]));
    }
    for (std::size_t i = 0; i < battle::kPartySlots; ++i) {
        note("equipment to work out of party slot " + std::to_string(i), std::to_string(port.equipmentChangeFlags[i]),
             std::to_string(guest.equipmentChangeFlags[i]));
    }
    note("the windows' wallpaper", std::to_string(port.menuWindowDesign), std::to_string(guest.menuWindowDesign));
    note("died this action", std::to_string(maskOf(port.diedAwaitingCounter)),
         std::to_string(maskOf(guest.diedAwaitingCounter)));
    note("statuses changed", std::to_string(maskOf(port.statusChanged)), std::to_string(maskOf(guest.statusChanged)));
    note("monsters standing", std::to_string(maskOf(port.monstersStanding)),
         std::to_string(maskOf(guest.monstersStanding)));
    note("monsters fallen", std::to_string(maskOf(port.monstersFallen)), std::to_string(maskOf(guest.monstersFallen)));
    return out;
}
namespace detail {

std::optional<battle::BattlerSlot> statusCallTarget(const TracedCall& call) {
    std::set<int> touched;
    for (const auto* cells : {&call.inputs, &call.writes}) {
        for (const auto& [cell, value] : *cells) {
            if (const std::optional<int> battler = battlerOfCell(cell)) {
                touched.insert(*battler);
            }
            for (const std::uint32_t first : {kInflictable12, kInflictable34}) {
                if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
                    touched.insert(static_cast<int>((cell - first) / 2));
                }
            }
        }
    }
    if (touched.size() != 1) {
        return std::nullopt;
    }
    return battle::BattlerSlot::fromIndex(*touched.begin());
}
}  // namespace detail

StatusEffectResult portStatusEffect(DamageScenario scenario, battle::BattlerSlot target) {
    scenario.attack.scratch.missedByEffect = {};
    battle::statusEffect(scenario.state, scenario.attack, scenario.attacker, target);
    const battle::Battler& battler = scenario.state.at(target);
    return StatusEffectResult{
        .statusesToSet = battler.statusesToSet,
        .statusesToClear = battler.statusesToClear,
        .missedByEffect = scenario.attack.scratch.missedByEffect,
        .missed = scenario.attack.scratch.missed,
    };
}

std::string differences(const StatusEffectResult& port, const StatusEffectResult& guest) {
    std::string out;
    const auto note = [&](const std::string& cell, const std::string& ours, const std::string& theirs) {
        if (ours != theirs) {
            out += cell + ": port " + ours + ", cartridge " + theirs + "\n";
        }
    };
    note("statuses to set", statusesText(port.statusesToSet), statusesText(guest.statusesToSet));
    note("statuses to clear", statusesText(port.statusesToClear), statusesText(guest.statusesToClear));
    note("missed by the effect", std::to_string(maskOf(port.missedByEffect)), std::to_string(maskOf(guest.missedByEffect)));
    note("shown Miss", std::to_string(maskOf(port.missed)), std::to_string(maskOf(guest.missed)));
    return out;
}

DamageScenario drawStatusChangesScenario(std::mt19937& rng) {
    DamageScenario s;
    battle::BattleState& state = s.state;
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        battle::Battler& battler = state.battlers[i];
        const bool character = battle::BattlerSlot::fromIndex(static_cast<int>(i)).isCharacter();
        battler.present = !oneIn(rng, 4);
        battler.status = anyStatuses(rng);
        battler.statusesToSet = anyStatuses(rng);
        battler.statusesToClear = anyStatuses(rng);
        battler.inflictableStatuses = anyStatuses(rng);
        battler.statusForCounter = anyStatuses(rng);
        battler.maxHp = static_cast<std::uint16_t>(character ? 1 + upTo(rng, kPartyMaxHp - 1) : 1 + upTo(rng, 65534));
        // Half the time the HP is within a step of an eighth of the maximum, where Near fatal turns.
        if ((rng() & 1) != 0) {
            const int eighth = battler.maxHp / 8;
            battler.hp = static_cast<std::uint16_t>(std::clamp(eighth - 1 + upTo(rng, 2), 0, static_cast<int>(battler.maxHp)));
        } else {
            battler.hp = static_cast<std::uint16_t>(upTo(rng, battler.maxHp));
        }
        battler.triggers.overcast = (rng() & 1) != 0;
        battler.triggers.phantasm = (rng() & 1) != 0;
        battler.triggers.retort = (rng() & 1) != 0;
        battler.retaliationPending = (rng() & 1) != 0;
        if ((rng() & 1) != 0) {
            battler.lastAttacker = anySlot(rng);
        }
        const std::uint8_t pending = byte(rng);
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
        battler.menuMayOpen = (rng() & 1) != 0;
        battler.emptyGaugeAfterAction = (rng() & 1) != 0;
        // Half the party slots ask for their equipment to be worked out again: half of those with a flag
        // of 1, as a fight leaves it, and half at the $FF a final-battle stage starts with, where a
        // status change carries into the next byte.
        if (character) {
            const std::uint32_t asks = rng();
            state.equipmentChangeFlags[i] =
                (asks & 1) == 0 ? 0 : (asks & 2) == 0 ? 1 : battle::kEquipmentChangeFlagFilled;
        }
        battler.sleepTimer = byte(rng);
        battler.stopTimer = byte(rng);
        battler.reflectTimer = byte(rng);
        battler.freezeTimer = byte(rng);
    }
    state.diedAwaitingCounter = anyBattlers(rng);
    state.statusChanged = anyBattlers(rng);
    state.monstersStanding = anyBattlers(rng).monstersOnly();
    state.monstersFallen = anyBattlers(rng).monstersOnly();
    return s;
}

TargetScenario drawStatusEffectScenario(std::mt19937& rng) {
    TargetScenario drawn;
    DamageScenario& s = drawn.scenario;
    s.attacker = anySlot(rng);
    drawn.target = anySlot(rng);
    AttackProperties& record = s.attack.record;
    record.traits.bits = byte(rng);
    record.element.bits = byte(rng);
    record.flags2.bits = byte(rng);
    // Never both status modes: no attack has them, and the cartridge would take the bytes after its
    // three-entry mode table as an address and run whatever code is there.
    if (record.flags2.has(AttackFlag2::REMOVE_STATUS) && record.flags2.has(AttackFlag2::TOGGLE_STATUS)) {
        record.flags2.clear((rng() & 1) != 0 ? AttackFlag2::REMOVE_STATUS : AttackFlag2::TOGGLE_STATUS);
    }
    record.misc.bits = byte(rng);
    record.statuses = anyStatuses(rng);
    s.attack.vanishApplies = (rng() & 1) != 0;
    s.attack.scratch.missed = anyBattlers(rng);
    battle::Battler& target = s.state.at(drawn.target);
    target.present = true;
    target.status = anyStatuses(rng);
    target.statusesToSet = anyStatuses(rng);
    target.statusesToClear = anyStatuses(rng);
    target.inflictableStatuses = (rng() & 1) != 0 ? anyStatuses(rng) : StatusSet{};
    target.undead = (rng() & 1) != 0;
    return drawn;
}

}  // namespace ostinato::trace
