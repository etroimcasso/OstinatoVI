// The cartridge's battle program run whole, a loop step at a time, with the graphics program's frames
// run as the scenario's model spends them.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/battler_set.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

#include "cells/battle/state.h"
#include "cells/damage/frame.h"
#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "oracle/routines.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;

namespace {

// The play clock's frames run 1 to 60: a frame that finds 60 sets 0, then steps (IncGameTime
// menu/menu_common.asm:3488-3519).
constexpr std::uint8_t kPlayClockFramesPerSecond = 60;
// The most of a script queue a battle script reaches: 64 commands of four bytes.
constexpr std::uint32_t kScriptQueueSpan = 0x100;

}  // namespace

WholeBattleResult DamageOracle::wholeBattle(const BattleScenario& scenario, std::span<const std::uint16_t> facings) {
    writeStartScenario(scenario.start);
    return runWholeBattle(scenario.model, scenario.firstStep, scenario.frameCap, false, facings,
                          scenario.cartridgeWrites);
}

WholeBattleResult DamageOracle::wholeBattleAsItStands(const PresentationModel& model, std::uint64_t frameCap) {
    return runWholeBattle(model, FirstStepLaid{}, frameCap, false);
}

WholeBattleResult DamageOracle::wholeBattleInOneCall(const BattleScenario& scenario) {
    writeStartScenario(scenario.start);
    return runWholeBattle(scenario.model, scenario.firstStep, scenario.frameCap, true);
}

WholeBattleResult DamageOracle::runWholeBattle(const PresentationModel& model, const FirstStepLaid& firstStep,
                                               std::uint64_t frameCap, bool inOneCall,
                                               std::span<const std::uint16_t> facings,
                                               std::span<const StepCellWrite> cartridgeWrites) {
    if (monsterSprites_ == nullptr) {
        throw std::logic_error("a whole battle needs the monsters' sprites (DamageOracle::useMonsterSprites)");
    }
    whole_.emplace();
    whole_->model = model;
    whole_->facings.assign(facings.begin(), facings.end());
    whole_->cartridgeWrites.assign(cartridgeWrites.begin(), cartridgeWrites.end());
    whole_->firstStep = firstStep;
    whole_->frameCap = frameCap;
    whole_->inOneCall = inOneCall;
    whole_->result.veldtBattlesBefore = readWriteBack().veldtBattles;
    writtenSincePoint_.clear();
    writtenBeyond_.clear();
    graphicsCalls_.clear();
    unansweredGraphics_.reset();
    keepTheStackPages();
    machine_.escapes()[kGraphicsKey].armed(true);
    machine_.escapes()[kLoopFrameWaitKey].armed(true);
    machine_.escapes()[kBattleClosedKey].armed(!inOneCall);
    machine_.escapes()[kTargetsCopyKey].armed(true);
    targetsCopy_ = TargetsCopy{};
    machine_.escapes()[kMagicEventUndodgeableKey].armed(true);
    magicEventUndodgeablePending_ = false;
    if (!beyondArmed_) {
        machine_.watches()[kBeyondKey].armed(true);
        beyondArmed_ = true;
    }
    // A step that does not come back round to the loop's head or close the battle was abandoned at the
    // engine's guard on a bound call: the battle goes no further.
    bool abandoned = false;
    call([&] {
        if (inOneCall) {
            battleMain_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage);
            whole_->result.ended = !graphicsCalls_.empty() && graphicsCalls_.back().command == kCloseBattle;
            abandoned = !whole_->result.ended;
            return;
        }
        // As BattleMain calls them (:56-64): both register widths narrow, data bank $7E.
        initRam_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage);
        initBattle_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage);
        while (!whole_->closed) {
            machine_.escapes()[kLoopHeadKey].armed(false);
            whole_->stepEnded = false;
            battleLoop_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage);
            if (!whole_->stepEnded) {
                abandoned = true;
                break;
            }
        }
    });
    machine_.escapes()[kGraphicsKey].armed(false);
    machine_.escapes()[kLoopFrameWaitKey].armed(false);
    machine_.escapes()[kLoopHeadKey].armed(false);
    machine_.escapes()[kBattleClosedKey].armed(false);
    machine_.escapes()[kTargetsCopyKey].armed(false);
    machine_.escapes()[kMagicEventUndodgeableKey].armed(false);
    WholeBattleResult result = std::move(whole_->result);
    whole_.reset();
    result.calls = graphicsCalls_;
    result.kept = readWriteBack();
    result.itemsObtainedTail = readItemsObtainedTail();
    if (inOneCall) {
        const std::vector<std::uint8_t> cells =
            machine_.read(retropp::MemoryRegion{.at = kWorkRam, .size = static_cast<std::uint32_t>(result.endCells.size())});
        std::copy(cells.begin(), cells.end(), result.endCells.begin());
        return result;
    }
    if (abandoned) {
        throw std::runtime_error("a loop step of the battle did not come back to the loop's head: the engine's "
                                 "guard on a bound call ended it at step " +
                                 std::to_string(result.steps.size()));
    }
    if (unansweredGraphics_) {
        throw std::runtime_error("the battle called graphics command " + std::to_string(*unansweredGraphics_) +
                                 ", which the oracle has no answer for");
    }
    return result;
}

void DamageOracle::answerInWholeBattle(const GraphicsCall& call) {
    WholeBattleRun& run = *whole_;
    if (call.command == kOpenBattleScene) {
        layTheOpening();
        for (const Frame frame : run.model.openingFrames()) {
            runFrame(frame);
        }
        return;
    }
    // The loop's frame wait (:72-73): every path back to the loop's head passes it first. The cells are
    // read before its frame runs, and in a stepped battle the loop's head answers from here on.
    if (run.atFrameWait && call.command == kOneFrame) {
        run.atFrameWait = false;
        // The final battle's next stage: the scroll has played, and the party may run again (:22102).
        run.runningStopped = false;
        if (run.result.steps.empty()) {
            // The start's own read-set: everything it read before writing, up to its first frame wait.
            run.result.unwrittenReads = unwrittenReads();
            writeFirstStep(run.firstStep);
        }
        CartridgeStep step = std::move(run.pending);
        run.pending = CartridgeStep{};
        const std::vector<std::uint8_t> cells =
            machine_.read(retropp::MemoryRegion{.at = kWorkRam, .size = static_cast<std::uint32_t>(step.cells.size())});
        std::copy(cells.begin(), cells.end(), step.cells.begin());
        step.frame = run.result.frames;
        step.firstCall = graphicsCalls_.size() - 1;
        run.result.steps.push_back(std::move(step));
        writtenSincePoint_.clear();
        for (const StepCellWrite& write : run.cartridgeWrites) {
            if (write.step + 1 == run.result.steps.size()) {
                writeBytes(retropp::MemoryRegion{.at = write.at, .size = static_cast<std::uint32_t>(write.bytes.size())},
                           write.bytes);
            }
        }
        if (!run.inOneCall) {
            machine_.escapes()[kLoopHeadKey].armed(true);
        }
        if (run.result.frames >= run.frameCap && !run.result.timedOut) {
            // Past the frame cap: the cartridge's own event 4 ends the battle at the loop's end check.
            run.result.timedOut = true;
            writeByte(kEndOfBattleEventsOn, kEventsOnByte);
            writeByte(kEndOfBattleEvent, kEndTheBattleNow);
        }
        runFrame(Frame{.clockCalled = true});
        return;
    }
    // The wrap-up's frame waits (:12076-12080): the same routine as the loop's, a frame with the clock
    // whoever asks (BtlGfx_01 btlgfx_main.asm:392-409).
    if (call.command == kOneFrame) {
        runFrame(Frame{.clockCalled = true});
        return;
    }
    if (call.command == kPlayBattleScript) {
        const std::vector<std::uint8_t> queue = machine_.read(retropp::MemoryRegion{.at = kQueue, .size = kScriptQueueSpan});
        for (const Frame frame : run.model.scriptFrames(run.scripts++, scriptStopsTime(queue))) {
            runFrame(frame);
        }
        // The final battle's next stage: the scroll lays out its monsters (GfxCmd_12 :22142-22157) and the
        // stage's graphics are readied as the opening readies them (_c10076 from :22093).
        if (queue.front() == kFinalBattleScrollCommand) {
            layTheMonsters();
        }
        return;
    }
    // The celebration (BtlGfx_08 :243-274): the party's running stops for the rest of the battle (:245),
    // and, unless the fanfare is off (:248-250), battle event 1 plays — a script's frames, time stopped.
    if (call.command == kVictory) {
        run.runningStopped = true;
        if ((readByte(kFormationFlags) & kFanfareOffBit) == 0) {
            for (const Frame frame : run.model.scriptFrames(run.scripts++, true)) {
                runFrame(frame);
            }
        }
        return;
    }
    // The final battle's scroll (BtlGfx_0c :120-149) stops the party's running until the next stage's
    // scroll has played (:137, :22102).
    if (call.command == kScrollFinalBattle) {
        run.runningStopped = true;
    }
}

void DamageOracle::runFrame(Frame frame) {
    WholeBattleRun& run = *whole_;
    if (run.model.timerRunsOutAt == run.result.frames) {
        writeByte(kFieldTimerFlags, static_cast<std::uint8_t>(readByte(kFieldTimerFlags) | kCountdownRanOutBit));
    }
    // The interrupt's body (BattleNMI btlgfx_main.asm:1603-1617, :1751): the party running or not, the
    // frame counter steps, and the play clock's frames.
    const bool runningDisabled = (readByte(kFormationAudioFlags) & kRunningDisabledBit) != 0;
    writeByte(kPartyRunning,
              run.model.partyRuns(run.result.frames, runningDisabled, run.runningStopped, frame) ? kPartyRunsByte : 0);
    writeByte(kFrameCounter, static_cast<std::uint8_t>(readByte(kFrameCounter) + 1));
    std::uint8_t played = readByte(kPlayClockFrames);
    if (played == kPlayClockFramesPerSecond) {
        played = 0;
    }
    writeByte(kPlayClockFrames, static_cast<std::uint8_t>(played + 1));
    // The facings the graphics program hands over on every frame wait, just before it calls the clock.
    if (!run.facings.empty()) {
        writeWord(kFacingRight, run.facings[std::min<std::size_t>(run.result.frames, run.facings.size() - 1)]);
    }
    ++run.result.frames;
    if (frame.clockCalled) {
        // The clock is the battle program's: its reads and writes are the battle's, not the answer's.
        const bool wasAnswering = std::exchange(answering_, false);
        updateBattleTime_(0, kByteWideStatus, kBattleDataBank, kBattleDirectPage);
        answering_ = wasAnswering;
    }
}

void DamageOracle::writeFirstStep(const FirstStepLaid& laid) {
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const std::uint32_t at = detail::slotIndex(battle::BattlerSlot::fromIndex(index));
        const StatusSet& added = laid.statusesAdded[static_cast<std::size_t>(index)];
        const std::array<std::uint32_t, 4> statusCells{kStatus12 + at, kStatus12 + at + 1, kStatus34 + at,
                                                       kStatus34 + at + 1};
        for (std::size_t byte = 0; byte < statusCells.size(); ++byte) {
            if (added.bytes[byte] != 0) {
                writeByte(statusCells[byte], static_cast<std::uint8_t>(readByte(statusCells[byte]) | added.bytes[byte]));
            }
        }
        // Muddled with what its set handler leaves (SetStatus_0d :11366-11383), as layFirstStep() lays it.
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        if (added.has(StatusId::CONFUSE)) {
            const std::uint16_t mask = detail::maskOf(battle::BattlerSet::of(slot));
            writeWord(kFlippedSet, static_cast<std::uint16_t>(readWord(kFlippedSet) | mask));
            if (slot.isCharacter()) {
                writeWord(kStatusChangedSet, static_cast<std::uint16_t>(readWord(kStatusChangedSet) | mask));
            }
        }
        if (const std::optional<std::uint16_t> hp = laid.hp[static_cast<std::size_t>(index)]) {
            writeWord(kHp + at, *hp);
        }
    }
    // A morph under way, as UpdateMorph leaves a member it has just morphed (:1679-1756), as layFirstStep()
    // lays it: the status, Morph traded for Revert in the member's commands, the member, the meter and its
    // speed.
    if (laid.morph) {
        const std::uint32_t at = detail::slotIndex(laid.morph->member);
        const StatusSet morph = StatusSet::of(StatusId::MORPH);
        writeByte(kStatus34 + at, static_cast<std::uint8_t>(readByte(kStatus34 + at) | morph.bytes[2]));
        writeByte(kStatus34 + at + 1, static_cast<std::uint8_t>(readByte(kStatus34 + at + 1) | morph.bytes[3]));
        const std::uint32_t rows = kCommandMenus + at / 2 * kCommandMenuSize;
        for (std::uint32_t row = rows; row < rows + kCommandMenuSize; row += kCommandRowSize) {
            if (readByte(row) == static_cast<std::uint8_t>(BattleCommandId::MORPH)) {
                writeByte(row, static_cast<std::uint8_t>(BattleCommandId::REVERT));
            }
        }
        writeByte(kMorphedPartyMember, static_cast<std::uint8_t>(at));
        writeWord(kMorphMeter, laid.morph->counter);
        writeWord(kMorphCounterSpeed, laid.morph->speed);
    }
}

void DamageOracle::targetsCopyNext() {
    if (whole_ && !answering_) {
        targetsCopy_ = TargetsCopy{.stage = TargetsCopy::Stage::Reading};
    }
}

void DamageOracle::magicEventUndodgeableNext() {
    if (whole_ && !answering_) {
        magicEventUndodgeablePending_ = true;
    }
}

void DamageOracle::reachedTheFrameWait() {
    if (whole_) {
        whole_->atFrameWait = true;
    }
}

void DamageOracle::stepEnded(std::uint16_t stackPointer, bool closed) {
    if (!whole_) {
        return;
    }
    WholeBattleRun& run = *whole_;
    run.result.stackPointers.insert(stackPointer);
    run.stepEnded = true;
    if (closed) {
        run.closed = true;
        run.result.ended = true;
        const std::vector<std::uint8_t> cells = machine_.read(
            retropp::MemoryRegion{.at = kWorkRam, .size = static_cast<std::uint32_t>(run.result.endCells.size())});
        std::copy(cells.begin(), cells.end(), run.result.endCells.begin());
    }
}

}  // namespace ostinato::trace
