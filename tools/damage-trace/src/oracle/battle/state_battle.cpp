// The battle's own state laid into the cells as the cartridge holds it: its flags and settings, the
// three lines and the player's records, the sets of battlers it keeps between steps, Mimic's memory,
// the tools the party owns and the end-of-battle events.
//
// A line and the records are rings: where one starts is a position in the ring and nothing the battle
// reads but the slots from it. Each is laid from the start the machine holds (in a comparison, the
// cartridge's): the slots from there hold the battlers, or the records, in order, and the end of a
// line is the start plus the battlers in it. The start is held by what is laid from it.

#include "oracle/damage_oracle.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "battle/action_queue.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/player_intake.h"
#include "ostinato/item_id.h"

#include "cells/battle/state.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {
constexpr std::uint16_t kNoBattlers = 0xFFFF;
constexpr std::uint8_t kRingMask = 0x18;  // the records' ring: four records, eight bytes apart
constexpr std::uint32_t kMimicRecordSize = 2;
}  // namespace

void DamageOracle::holdPosition(std::uint32_t at, std::uint32_t count) {
    if (capture_ == nullptr) {
        return;
    }
    for (std::uint32_t cell = at; cell < at + count; ++cell) {
        capture_->cells.erase(cell);
        capture_->disagreements.erase(cell);
        capture_->positions.insert(cell);
    }
}

std::uint8_t DamageOracle::positionByte(std::uint32_t at) {
    if (capture_ != nullptr && positionsFrom_ != nullptr) {
        return (*positionsFrom_)[at - kWorkRam];
    }
    return readByte(at);
}

void DamageOracle::layLine(std::uint32_t startCell, std::uint32_t ring, const battle::BattlerQueue& line) {
    const std::uint8_t start = positionByte(startCell);
    holdPosition(startCell, 1);
    for (std::size_t i = 0; i < line.size(); ++i) {
        const auto place = static_cast<std::uint8_t>(start + i);
        layByteAsTheCartridgeHoldsIt(ring + place, static_cast<std::uint8_t>(slotIndex(line.at(i))));
    }
    layByteAsTheCartridgeHoldsIt(startCell + 1, static_cast<std::uint8_t>(start + line.size()));
}

void DamageOracle::layBattleAsTheCartridgeHoldsIt(const battle::BattleState& state) {
    layByteAsTheCartridgeHoldsIt(kBattleFlags0,
                                 static_cast<std::uint8_t>((state.actorStillActing ? kStillActingBit : 0) |
                                                           (state.preemptive ? kPreemptiveBit : 0) |
                                                           (state.monsterListChanged ? kMonsterNamesChangedBit : 0) |
                                                           (state.runActionQueued ? kRunActionWaitingBit : 0)));
    layByteAsTheCartridgeHoldsIt(kLastAttackLevel, state.lastAttackLevel);
    layByteAsTheCartridgeHoldsIt(kGaugeShown, state.config.gaugeShown ? kGaugeShownByte : 0);
    layByteAsTheCartridgeHoldsIt(kTimeStoppedByMenu, state.timeStoppedByMenu ? 1 : 0);
    layWordAsTheCartridgeHoldsIt(kRouletteTarget,
                                 state.rouletteTarget ? maskOf(battle::BattlerSet::of(*state.rouletteTarget)) : kNoBattlers);
    layByteAsTheCartridgeHoldsIt(kPartyRunning, state.partyRunning ? 1 : 0);
    layWordAsTheCartridgeHoldsIt(kJoining, maskOf(state.joiningAfterAction));
    layByteAsTheCartridgeHoldsIt(kActingBattler, slotByte(state.actingBattler));
    layByteAsTheCartridgeHoldsIt(kCounteringBattler, slotByte(state.counteringBattler));
    layByteAsTheCartridgeHoldsIt(kPartyJustEscaped, static_cast<std::uint8_t>(maskOf(state.partyJustEscaped)));
    layWordAsTheCartridgeHoldsIt(kTurnCounter, state.turnCounter);
    layWordAsTheCartridgeHoldsIt(kAliveEnemyCharacters, maskOf(state.aliveEnemyCharacters));
    layWordAsTheCartridgeHoldsIt(kMenusToRefresh, maskOf(state.menusToRefresh));
    layWordAsTheCartridgeHoldsIt(kCounterAiNotRunState, static_cast<std::uint16_t>(~maskOf(state.counterAiRanThisStep)));
    layWordAsTheCartridgeHoldsIt(kNotAttackedThisStepState, static_cast<std::uint16_t>(~maskOf(state.attackedThisStep)));

    layLine(kAdvanceWaitLineStart, kAdvanceWaitLine, state.advanceWaitQueue);
    layLine(kActionLineStart, kActionLine, state.actionQueue);
    layLine(kCounterLineStart, kCounterLine, state.counterQueue);

    // The player's records, from where the next one is read.
    const std::uint8_t next = positionByte(kRecordsNext);
    holdPosition(kRecordsNext, 2);
    for (std::uint32_t k = 0; k < kPlayerRecordCount; ++k) {
        const std::uint32_t at = kPlayerRecords + ((next + k * kPlayerRecordSize) & kRingMask);
        if (k >= state.chosenActions.size()) {
            layByteAsTheCartridgeHoldsIt(at, kNoRecordWaiting);
            leaveOut(at + 1, kPlayerRecordSize - 1);
            continue;
        }
        const battle::PlayerAction& record = state.chosenActions[k];
        const std::uint16_t targets = maskOf(record.targets);
        const std::uint16_t second = maskOf(record.secondTargets);
        const std::array<std::uint8_t, kPlayerRecordSize> bytes{
            static_cast<std::uint8_t>(record.character.position()), static_cast<std::uint8_t>(record.command),
            static_cast<std::uint8_t>(record.attack),               static_cast<std::uint8_t>(targets & 0xFF),
            static_cast<std::uint8_t>(targets >> 8),                static_cast<std::uint8_t>(record.secondAttack),
            static_cast<std::uint8_t>(second & 0xFF),               static_cast<std::uint8_t>(second >> 8)};
        layAsTheCartridgeHoldsIt(at, bytes);
    }

    layByteAsTheCartridgeHoldsIt(kPartyRelicEffects2, state.partyRelicEffects2.bits);
    layByteAsTheCartridgeHoldsIt(kLastDance, state.lastDance ? static_cast<std::uint8_t>(*state.lastDance) : 0);
    layByteAsTheCartridgeHoldsIt(kPartyPresentAtStart, static_cast<std::uint8_t>(maskOf(state.partyPresentAtStart)));
    layByteAsTheCartridgeHoldsIt(kWaitMode, state.config.waitMode ? 1 : 0);
    layByteAsTheCartridgeHoldsIt(kMonsterSpeedFactor, state.config.monsterSpeedFactor());
    layByteAsTheCartridgeHoldsIt(kCounterPhase, state.counterPhase);
    layByteAsTheCartridgeHoldsIt(kLastRager, state.lastRager ? static_cast<std::uint8_t>(slotIndex(*state.lastRager)) : 0);
    layByteAsTheCartridgeHoldsIt(kBattleMayNotEnd, state.battleMayNotEnd ? kBattleMayNotEndByte : 0);

    std::uint8_t tools = 0;
    for (const battle::BattleItem& item : state.inventory.items) {
        const auto id = static_cast<std::uint8_t>(item.item);
        if (item.item != ItemId::EMPTY && id >= kFirstToolItem && id - kFirstToolItem < kToolCount) {
            tools |= static_cast<std::uint8_t>(1u << (id - kFirstToolItem));
        }
    }
    layByteAsTheCartridgeHoldsIt(kToolsOwned, tools);

    layByteAsTheCartridgeHoldsIt(kEndOfBattleEventsOn, state.finalBattleChain ? 0 : kEventsOnByte);
    layByteAsTheCartridgeHoldsIt(kFinalBattleScroll, state.finalBattleScroll.value_or(kEventsOnByte));

    // Mimic's memory: the last command and its targets; X-Magic's second, or Mimic's own command where
    // there is none; a Jump's command and targets, or Mimic's own command where the last was not one.
    const battle::MimicMemory& mimic = state.mimic;
    const std::array<std::uint8_t, kMimicRecordSize> last{static_cast<std::uint8_t>(mimic.last.command),
                                                          static_cast<std::uint8_t>(mimic.last.attack)};
    layAsTheCartridgeHoldsIt(kMimicLast, last);
    layWordAsTheCartridgeHoldsIt(kMimicLastTargets, maskOf(mimic.last.targets));
    if (mimic.secondSpell) {
        const std::array<std::uint8_t, kMimicRecordSize> second{static_cast<std::uint8_t>(mimic.secondSpell->command),
                                                                static_cast<std::uint8_t>(mimic.secondSpell->attack)};
        layAsTheCartridgeHoldsIt(kMimicSecond, second);
        layWordAsTheCartridgeHoldsIt(kMimicSecondTargets, maskOf(mimic.secondSpell->targets));
    } else {
        layByteAsTheCartridgeHoldsIt(kMimicSecond, static_cast<std::uint8_t>(BattleCommandId::MIMIC));
        leaveOut(kMimicSecond + 1, 3);
    }
    if (mimic.replayJump) {
        layByteAsTheCartridgeHoldsIt(kMimicJump, static_cast<std::uint8_t>(BattleCommandId::JUMP));
        layWordAsTheCartridgeHoldsIt(kMimicJumpTargets, maskOf(mimic.jumpTargets));
    } else {
        layByteAsTheCartridgeHoldsIt(kMimicJump, static_cast<std::uint8_t>(BattleCommandId::MIMIC));
        leaveOut(kMimicJumpTargets, 2);
    }
    leaveOut(kMimicJump + 1, 1);

    std::uint8_t desperation = 0;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.desperationUsed.has(battle::BattlerSlot::character(position))) {
            desperation |= static_cast<std::uint8_t>(1u << position);
        }
    }
    layByteAsTheCartridgeHoldsIt(kDesperationUsed, desperation);
    layWordAsTheCartridgeHoldsIt(kMorphCounterSpeed, state.morphCounterSpeed);
}

}  // namespace ostinato::trace
