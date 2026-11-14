// The cartridge's script readied, and a turn's and a counterattack's run of it, over a battle.

#include "oracle/damage_oracle.h"

#include <cstdint>
#include <string>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/ai.h"
#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "data/ai_scripts.h"
#include "data/char_ai.h"

#include "cells/ai/ai.h"
#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "oracle/routines.h"
#include "scenarios/ai/ai_run_shared.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

constexpr std::uint32_t kActionList = kWorkRam + 0x32CC;
constexpr std::uint32_t kCounterList = kWorkRam + 0x32CD;
// The actions waiting (kActionNext, kActionCommand, kActionTargets and kActionMp above): 128 entries.
constexpr std::uint32_t kActionEntries = 128;
constexpr std::uint8_t kNoEntry = 0xFF;
// Where each spell sits in a party member's spell list, one byte per attack ($FF: not in it).
constexpr std::uint32_t kSpellListPlaces = kWorkRam + 0x3084;
constexpr std::uint32_t kSpellListPlaceCount = 256;
// Quick's turns left (its battler is kQuickTarget above).
constexpr std::uint32_t kQuickTurnsLeft = kWorkRam + 0x3402;
constexpr std::uint32_t kAiRestrictedOnEntry = kWorkRam + 0x3A98;
}  // namespace

void DamageOracle::writeAiScriptCells(const battle::BattleState& state) {
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        const auto at = static_cast<std::uint32_t>(index * 2);
        const AiScriptPlaces places = portScriptPlaces(state, slot);
        writeWord(kTurnPlace + at, places.turn);
        writeWord(kCounterPlace + at, places.counter);
        writeByte(kTurnSkipped + at, places.turnSkipped);
        writeByte(kCounterSkipped + at, places.counterSkipped);
        writeWord(kTurnWaitPlace + at, places.turnWaitPlace);
        writeWord(kCounterWaitPlace + at, places.counterWaitPlace);
    }
}

void DamageOracle::emptyActionLists(battle::BattlerSlot actor) {
    const std::vector<std::uint8_t> free(kActionEntries, kNoEntry);
    writeBytes(retropp::MemoryRegion{.at = kActionNext, .size = kActionEntries}, free);
    const auto at = static_cast<std::uint32_t>(actor.index() * 2);
    writeByte(kActionList + at, kNoEntry);
    writeByte(kCounterList + at, kNoEntry);
    writeByte(kCounterQueueStart, 0);
    writeByte(kCounterQueueEnd, 0);
}

void DamageOracle::writeAiRunCells(const AiScenario& scenario, bool counter) {
    const battle::BattleState& state = scenario.state;
    // The aim's cells first, so the AI's own reach flag and battler cells are the ones that stand.
    writeAimCells(DamageScenario{.state = state, .attacker = scenario.actor});
    writeAiCells(scenario);
    writeAiScriptCells(state);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::Battler& battler = state.at(battle::BattlerSlot::fromIndex(index));
        const auto at = static_cast<std::uint32_t>(index * 2);
        writeByte(kAdvanceWait + at, battler.advanceWait.value_or(kUnsetWait));
        writeByte(kActionList + at, kNoEntry);
        writeByte(kCounterList + at, kNoEntry);
        writeWord(kCounterStatus12 + at,
                  static_cast<std::uint16_t>(battler.statusForCounter.bytes[0] | battler.statusForCounter.bytes[1] << 8));
        writeWord(kCounterStatus34 + at,
                  static_cast<std::uint16_t>(battler.statusForCounter.bytes[2] | battler.statusForCounter.bytes[3] << 8));
    }
    emptyActionLists(scenario.actor);
    // No party member's spell list holds a spell: a party member's spell costs nothing.
    const std::vector<std::uint8_t> noSpells(kSpellListPlaceCount, kNoEntry);
    writeBytes(retropp::MemoryRegion{.at = kSpellListPlaces, .size = kSpellListPlaceCount}, noSpells);
    writeByte(kBattleFlags, counter ? kCounterattackRunningBit : 0);
    writeWord(kCounterAiNotRun, static_cast<std::uint16_t>(~maskOf(state.counterAiRanThisStep)));
    writeWord(kDiedAwaitingCounter, maskOf(state.diedAwaitingCounter));
    writeWord(kNotAttackedThisStep, static_cast<std::uint16_t>(~maskOf(state.attackedThisStep)));
    writeByte(kQuickTarget, state.quickTarget ? static_cast<std::uint8_t>(slotIndex(*state.quickTarget)) : kNoOne);
    writeByte(kQuickTurnsLeft, state.quickTurnsLeft.value_or(0));
    writeWord(kAiRestrictedOnEntry, 0);
    writeWord(kTargets, counter ? maskOf(scenario.answering) : 0);
}

AiScriptPlaces DamageOracle::readScriptPlaces(battle::BattlerSlot slot) {
    const auto at = static_cast<std::uint32_t>(slot.index() * 2);
    return AiScriptPlaces{.turn = readWord(kTurnPlace + at),
                          .counter = readWord(kCounterPlace + at),
                          .turnSkipped = readByte(kTurnSkipped + at),
                          .turnWaitPlace = readWord(kTurnWaitPlace + at),
                          .counterSkipped = readByte(kCounterSkipped + at),
                          .counterWaitPlace = readWord(kCounterWaitPlace + at)};
}

AiRunResult DamageOracle::readAiRunResult(battle::BattlerSlot actor) {
    const auto at = static_cast<std::uint32_t>(actor.index() * 2);
    const auto list = [&](std::uint32_t first) {
        std::vector<std::string> actions;
        std::uint8_t entry = readByte(first);
        for (std::uint32_t guard = 0; entry != kNoEntry && guard < kActionEntries; ++guard) {
            const std::uint32_t place = static_cast<std::uint32_t>(entry) * 2;
            actions.push_back(cellsText(readCells(readByte(kActionCommand + place), readByte(kActionCommand + place + 1),
                                                  readWord(kActionTargets + place), readByte(kActionMp + place))));
            const std::uint8_t next = readByte(kActionNext + entry);
            if (next == entry) {
                break;
            }
            entry = next;
        }
        return actions;
    };
    AiRunResult result;
    result.actions = list(kActionList + at);
    result.counterActions = list(kCounterList + at);
    const std::uint8_t wait = readByte(kAdvanceWait + at);
    if (wait != kUnsetWait) {
        result.advanceWait = wait;
    }
    result.script = readScriptPlaces(actor);
    result.targetingTarget = readTargetingTarget(actor);
    result.counterAiRanThisStep = setOf(static_cast<std::uint16_t>(~readWord(kCounterAiNotRun)));
    result.diedAwaitingCounter = setOf(readWord(kDiedAwaitingCounter));
    result.attackedThisStep = setOf(static_cast<std::uint16_t>(~readWord(kNotAttackedThisStep)));
    const std::uint8_t end = readByte(kCounterQueueEnd);
    for (std::uint8_t i = readByte(kCounterQueueStart); i < end; ++i) {
        result.counterQueue.push_back(readByte(kCounterQueue + i) / 2);
    }
    result.rngPosition = readByte(kRngPosition);
    return result;
}

AiScriptPlaces DamageOracle::readyAi(const AiScenario& scenario, battle::BattlerSlot slot) {
    writeAiScriptCells(scenario.state);
    const std::uint16_t script = scenario.state.at(slot).aiScript.value_or(0);
    call([&] { initAi_(script, slotIndex(slot), kWideIndexStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedInitAi);
    return readScriptPlaces(slot);
}

AiRunResult DamageOracle::aiTurn(const AiScenario& scenario) {
    writeAiRunCells(scenario, false);
    call([&] { execMonsterAction_(slotIndex(scenario.actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedExecMonsterAction);
    return readAiRunResult(scenario.actor);
}

AiRunResult DamageOracle::nextAiTurn(battle::BattlerSlot actor) {
    emptyActionLists(actor);
    writeWord(kTargets, 0);
    writeByte(kBattleFlags, 0);
    call([&] { execMonsterAction_(slotIndex(actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedExecMonsterAction);
    return readAiRunResult(actor);
}

AiRunResult DamageOracle::aiCounter(const AiScenario& scenario) {
    writeAiRunCells(scenario, true);
    call([&] { execAiRetal_(slotIndex(scenario.actor), kByteWideStatus, kBattleDataBank, kBattleDirectPage); });
    trace_->returnToFrame(kTracedExecAiRetal);
    return readAiRunResult(scenario.actor);
}

}  // namespace ostinato::trace
