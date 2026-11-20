// The battle's own state laid into the cells as the cartridge holds it: its flags and settings, the
// three lines and the player's records, the sets of battlers it keeps between steps, Mimic's memory,
// the tools the party owns and the end-of-battle events.
//
// A line and the records are rings: where one starts is a position in the ring and nothing the battle
// reads but the slots from it. Each is laid from the start the machine holds (in a comparison, the
// cartridge's): the slots from there hold the battlers, or the records, in order, and the end of a
// line is the start plus the battlers in it and, in a comparison, the cartridge's empty places among
// them (layLine()). The start is held by what is laid from it.

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
#include "cells/status/effects.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {
constexpr std::uint16_t kNoBattlers = 0xFFFF;
constexpr std::uint8_t kRingMask = 0x18;  // the records' ring: four records, eight bytes apart
constexpr std::uint32_t kMimicRecordSize = 2;
// A line's ring: 256 places, its start and end a byte each; a place no battler holds is $FF.
constexpr std::uint32_t kRingPlaces = 256;
constexpr std::uint8_t kEmptyLinePlace = 0xFF;
// Each battler's damage taken and damage healed, a word each per battler, and the byte that marks
// nothing waiting.
constexpr std::size_t kDamageWordsBytes = 2 * 2 * battle::kBattlerSlots;
constexpr std::uint8_t kNothingWaiting = 0xFF;
constexpr std::uint32_t kNextAttackCostHigh = kWorkRam + 0x3A4D;
// The bits below bit 7 of the acting or counterattacking battler's byte while no battler is.
constexpr std::uint8_t kRotateLeavings = 0x7F;
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
    // The ring's places outside the line hold what the battlers who left it left there, or a search's
    // $FF (RemoveAllActions :1583-1591): no field determines them.
    leaveOut(ring, kRingPlaces);
    // A battler taken out of the line leaves its place holding $FF until the line's front passes it, and the
    // loop passes over such a place (RemoveAllActions :1583-1591; BattleLoop :127-128, :141-142). In a
    // comparison the cartridge's places of that kind, before its end, are laid as it holds them and the
    // line's battlers go in the others, in order; written into a machine, a line has none.
    const bool comparing = capture_ != nullptr && positionsFrom_ != nullptr;
    const std::uint8_t cartridgeEnd = positionByte(startCell + 1);
    std::uint8_t place = start;
    const auto passEmptyPlaces = [&] {
        while (comparing && place != cartridgeEnd && positionByte(ring + place) == kEmptyLinePlace) {
            layByteAsTheCartridgeHoldsIt(ring + place, kEmptyLinePlace);
            ++place;
        }
    };
    for (std::size_t i = 0; i < line.size(); ++i) {
        passEmptyPlaces();
        layByteAsTheCartridgeHoldsIt(ring + place, static_cast<std::uint8_t>(slotIndex(line.at(i))));
        ++place;
    }
    passEmptyPlaces();
    layByteAsTheCartridgeHoldsIt(startCell + 1, place);
}

void DamageOracle::layBattleAsTheCartridgeHoldsIt(const battle::BattleState& state) {
    layByteAsTheCartridgeHoldsIt(kBattleFlags0,
                                 static_cast<std::uint8_t>((state.actorStillActing ? kStillActingBit : 0) |
                                                           (state.preemptive ? kPreemptiveBit : 0) |
                                                           (state.monsterListChanged ? kMonsterNamesChangedBit : 0) |
                                                           (state.runActionQueued ? kRunActionWaitingBit : 0)));
    layByteAsTheCartridgeHoldsIt(kLastAttackLevel, state.lastAttackLevel);
    layByteAsTheCartridgeHoldsIt(kAttackRecordFlags, state.lastAttackPhysical ? kPhysicalHitBit : 0);
    // Shadow available, among the field's other characters, which the battle never reads.
    const std::uint8_t available = readByte(kAvailableCharacters);
    layByteAsTheCartridgeHoldsIt(kAvailableCharacters,
                                 static_cast<std::uint8_t>(state.shadowAvailable ? available | kShadowAvailableBit
                                                                                 : available & ~kShadowAvailableBit));
    layByteAsTheCartridgeHoldsIt(kGaugeShown, state.config.gaugeShown ? kGaugeShownByte : 0);
    layByteAsTheCartridgeHoldsIt(kTimeStoppedByMenu, state.timeStoppedByMenu ? 1 : 0);
    layWordAsTheCartridgeHoldsIt(kRouletteTarget,
                                 state.rouletteTarget ? maskOf(battle::BattlerSet::of(*state.rouletteTarget)) : kNoBattlers);
    layByteAsTheCartridgeHoldsIt(kPartyRunning, state.partyRunning ? kPartyRunsByte : 0);
    // Gau just turned up (GauAppears :12014-12015), until the loop's next step drops the party's choices
    // (:81-84); between steps the byte's other bits are an attack's and are clear.
    layByteAsTheCartridgeHoldsIt(kAimFlags, state.gauJoining ? kGauJoiningBit : 0);
    layWordAsTheCartridgeHoldsIt(kJoining, maskOf(state.joiningAfterAction));
    // The battler acting, and the one counterattacking: with none, bit 7 is set (ExecAction's sec; ror
    // :195-196, :12411-12412) and the other bits are the rotate's leavings, the slot last kept halved. The
    // one reader tests bit 7 and, set, goes to a line that loads the byte again (ldx; bpl :133-135, :88-89).
    layByteAsTheCartridgeHoldsIt(kActingBattler, slotByte(state.actingBattler));
    layByteAsTheCartridgeHoldsIt(kCounteringBattler, slotByte(state.counteringBattler));
    if (!state.actingBattler) {
        leaveOutBits(kActingBattler, kRotateLeavings);
    }
    if (!state.counteringBattler) {
        leaveOutBits(kCounteringBattler, kRotateLeavings);
    }
    layByteAsTheCartridgeHoldsIt(kPartyJustEscaped, static_cast<std::uint8_t>(maskOf(state.partyJustEscaped)));
    layWordAsTheCartridgeHoldsIt(kTurnCounter, state.turnCounter);
    // The frame count the graphics program's interrupt steps, and its value when battle time last ran.
    layByteAsTheCartridgeHoldsIt(kFrameCounter, state.frameCount);
    layByteAsTheCartridgeHoldsIt(kLastClockFrame, state.lastTimeUpdateFrame);
    // A story countdown that ran out (bit 5 of the field's timer flags; the other bits are the field's).
    layByteAsTheCartridgeHoldsIt(kFieldTimerFlags, state.eventTimerExpired ? kCountdownRanOutBit : 0);
    // Between actions no damage waits to be shown: the start's fill, and after every attack's numerals
    // the apply-and-show routine marks every target's taken and healed words empty (:16265-16271). No
    // follow-up spell or message waits either: the fill, and each attack's use of them stores $FF again
    // (ExecAttack :8111-8118 for the message).
    std::array<std::uint8_t, kDamageWordsBytes> noDamage{};
    noDamage.fill(kNothingWaiting);
    layAsTheCartridgeHoldsIt(kTaken, noDamage);
    layByteAsTheCartridgeHoldsIt(kFollowUpSpell, kNothingWaiting);
    layByteAsTheCartridgeHoldsIt(kMessage, kNothingWaiting);
    // The high byte of the next attack's cost: the attack takes the cost sixteen bits wide and clears it
    // (CalcAttackEffect :8147, :8161-8166), while every action stores only its low byte
    // (InitPlayerAction :408-409), so it holds the start's zero fill throughout.
    layByteAsTheCartridgeHoldsIt(kNextAttackCostHigh, 0);
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
