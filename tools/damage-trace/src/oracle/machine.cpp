// The oracle's machine: hosting the cartridge, starting it as far as its reset code, calling a
// routine, the read-set gate, answering the graphics program, and runOnLoop().

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
#include "retropp/isa.h"
#include "retropp/memory_region.h"
#include "retropp/run_loop.h"
#include "retropp/snes.h"
#include "retropp/vm.h"

#include "cells/damage/per_target.h"
#include "oracle/routines.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;

DamageOracle::DamageOracle(std::span<const std::uint8_t> image) {
    machine_.hostRom(image);
    for (const RoutineOpening& routine : kOpenings) {
        const std::vector<std::uint8_t> bytes =
            machine_.read(retropp::MemoryRegion{.at = routine.at, .size = static_cast<std::uint32_t>(routine.length)});
        if (!std::equal(bytes.begin(), bytes.end(), routine.bytes.begin())) {
            throw std::runtime_error(std::string{"the cartridge does not hold "} + routine.name +
                                     " where the US cartridges do");
        }
    }

    machine_.registerEscapes(retropp::escapes(retropp::GuestEscape{
        .key = kResetKey, .at = kReset, .handler = [this](retropp::Vm&, std::uint32_t) { reset_ = true; }}));
    // The trace's watches cover the damage word and the multiplier's steps; their writes during a
    // call are what writes() hands back.
    trace_ = std::make_unique<DamageTrace>(
        machine_,
        [this](const std::string& line) {
            if (tracing_) {
                traced_.push_back(line);
            }
        },
        [] { return std::uint64_t{0}; },
        [this](std::uint32_t at, std::uint8_t value, bool write) {
            if (recording_ && write && (at == kDamage || at == kDamage + 1 || at == kMultiplier)) {
                writes_.push_back(WatchedWrite{.at = at, .value = value});
            }
            noteGuestAccess(at, value, write);
        });
    registerGate();

    const retropp::RoutineBinding byteWide{
        .inputs = {retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
        .isa = retropp::Isa::Wdc65816};
    calcMagicDmg_ = machine_.bindRoutine<Call>(kCalcMagicDmg, byteWide);
    calcDmg_ = machine_.bindRoutine<Call>(kCalcDmg, byteWide);
    relicDmgEffect_ = machine_.bindRoutine<Call>(kRelicDmgEffect, byteWide);
    applyDmgMult_ = machine_.bindRoutine<Multiply>(
        kApplyDmgMult, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                        .output = retropp::snes::C,
                        .isa = retropp::Isa::Wdc65816});
    applyAndShowDmg_ = machine_.bindRoutine<Call>(
        kApplyAndShowDmg, {.inputs = {retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                           .isa = retropp::Isa::Wdc65816});
    calcReflectDmg_ = machine_.bindRoutine<TargetCall>(
        kCalcReflectDmg,
        {.inputs = {retropp::snes::C, retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    calcTargetDmg_ = machine_.bindRoutine<TargetCall>(
        kCalcTargetDmg,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    updateStatus_ = machine_.bindRoutine<PassCall>(
        kUpdateStatus,
        {.inputs = {retropp::snes::P, retropp::snes::DB, retropp::snes::D}, .isa = retropp::Isa::Wdc65816});
    magicStatusEffect_ = machine_.bindRoutine<TargetCall>(
        kMagicStatusEffect,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    doAttackerEffect_ = machine_.bindRoutine<Call>(kDoAttackerEffect, byteWide);
    doTargetEffect_ = machine_.bindRoutine<TargetCall>(
        kDoTargetEffect,
        {.inputs = {retropp::snes::X, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    initGfxParams_ = machine_.bindRoutine<Call>(kInitGfxParams, byteWide);
    coverEffect_ = machine_.bindRoutine<Call>(kCoverEffect, byteWide);
    learnGenjuMagic_ = machine_.bindRoutine<TargetCall>(
        kLearnGenjuMagic,
        {.inputs = {retropp::snes::C, retropp::snes::X, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .isa = retropp::Isa::Wdc65816});
    doLevelUp_ = machine_.bindRoutine<Call>(kDoLevelUp, byteWide);

    // The AI's routines answer in the carry, read from the status byte they return with.
    checkAiTarget_ = machine_.bindRoutine<TargetsCall>(
        kCheckAiTarget,
        {.inputs = {retropp::snes::A, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
         .output = retropp::snes::P,
         .isa = retropp::Isa::Wdc65816});
    const std::vector<std::uint8_t> table = machine_.read(retropp::MemoryRegion{
        .at = kAiConditionTable, .size = static_cast<std::uint32_t>(kAiConditionRoutines.size() * 2)});
    for (std::size_t number = 0; number < kAiConditionRoutines.size(); ++number) {
        const auto word = static_cast<std::uint16_t>(table[number * 2] | table[number * 2 + 1] << 8);
        if (word != kAiConditionRoutines[number]) {
            throw std::runtime_error("the cartridge's table of AI conditions does not point where the US "
                                     "cartridges' does at condition " + std::to_string(number));
        }
        aiConditions_.push_back(machine_.bindRoutine<ConditionCall>(
            kBattleProgramBank | word,
            {.inputs = {retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
             .output = retropp::snes::P,
             .isa = retropp::Isa::Wdc65816}));
    }

    initAi_ = machine_.bindRoutine<SetupCall>(
        kInitAi, {.inputs = {retropp::snes::C, retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                  .isa = retropp::Isa::Wdc65816});
    execMonsterAction_ = machine_.bindRoutine<Call>(kExecMonsterAction, byteWide);
    execAiRetal_ = machine_.bindRoutine<Call>(kExecAiRetal, byteWide);
    for (std::size_t i = 0; i < kAiCommands.size(); ++i) {
        aiCommands_[i] = machine_.bindRoutine<Call>(
            kAiCommands[i], {.inputs = {retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                             .isa = retropp::Isa::Wdc65816});
    }
    for (std::size_t i = 0; i < kOwnActions.size(); ++i) {
        ownActions_[i] = machine_.bindRoutine<Call>(
            kOwnActions[i], {.inputs = {retropp::snes::Y, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                             .isa = retropp::Isa::Wdc65816});
    }
    execSelfAttack_ = machine_.bindRoutine<Call>(kExecSelfAttack, byteWide);
    execAttack_ = machine_.bindRoutine<Call>(kExecAttack, byteWide);

    addExp_ = machine_.bindRoutine<Call>(kAddExp, byteWide);
    learnItemMagic_ = machine_.bindRoutine<Call>(kLearnItemMagic, byteWide);
    checkLevelUp_ = machine_.bindRoutine<Call>(kCheckLevelUp, byteWide);
    learnAbilities_ = machine_.bindRoutine<AbilitiesCall>(
        kLearnAbilities, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                          .isa = retropp::Isa::Wdc65816});
    learnRage_ = machine_.bindRoutine<Call>(kLearnRage, byteWide);
    execBtlGfx_ = machine_.bindRoutine<AbilitiesCall>(
        kExecBtlGfx, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                      .isa = retropp::Isa::Wdc65816});

    // The graphics program's handlers, found through its own table.
    const std::vector<std::uint8_t> graphicsTable = machine_.read(retropp::MemoryRegion{
        .at = kGraphicsCommandTable, .size = static_cast<std::uint32_t>(kGraphicsCommandHandlers.size() * 2)});
    std::array<std::uint32_t, kGraphicsCommandHandlers.size()> handlers{};
    for (std::size_t command = 0; command < handlers.size(); ++command) {
        const auto word = static_cast<std::uint16_t>(graphicsTable[command * 2] | graphicsTable[command * 2 + 1] << 8);
        if (word != kGraphicsCommandHandlers[command]) {
            throw std::runtime_error("the cartridge's table of graphics commands does not point where the US "
                                     "cartridges' does at command " + std::to_string(command));
        }
        handlers[command] = kGraphicsProgramBank | word;
    }
    addItemObtained_ = machine_.bindRoutine<Call>(retropp::snes::rtl(handlers[kAddItemObtained]), byteWide);
    mergeItemsObtained_ = machine_.bindRoutine<Call>(retropp::snes::rtl(handlers[kMergeItemsObtained]), byteWide);
    wrapUp_ = machine_.bindRoutine<Call>(kWrapUp, byteWide);
    winBattle_ = machine_.bindRoutine<Call>(kWinBattle, byteWide);
    loseBattle_ = machine_.bindRoutine<AbilitiesCall>(
        kLoseBattle, {.inputs = {retropp::snes::C, retropp::snes::P, retropp::snes::DB, retropp::snes::D},
                      .isa = retropp::Isa::Wdc65816});
    updateSram_ = machine_.bindRoutine<Call>(kUpdateSram, byteWide);
    decompCutsceneProg_ = machine_.bindRoutine<Call>(kDecompCutsceneProg, byteWide);
    battleMain_ = machine_.bindRoutine<Call>(retropp::snes::rtl(kBattleExt), byteWide);
    initRam_ = machine_.bindRoutine<Call>(kInitRam, byteWide);
    initBattle_ = machine_.bindRoutine<Call>(kInitBattle, byteWide);
    battleLoop_ = machine_.bindRoutine<Call>(kBattleLoop, byteWide);
    updateBattleTime_ = machine_.bindRoutine<Call>(retropp::snes::rtl(kUpdateBattleTimeExt), byteWide);
    // A whole battle's steps end at the loop's head and at TerminateBattle's return, each answering in
    // place of the code there with the stack pointer the step left.
    const auto stepEnd = [this](bool closed) {
        return retropp::routine(retropp::RoutineBinding{.inputs = {retropp::snes::S}},
                                [this, closed](std::uint16_t stackPointer) { stepEnded(stackPointer, closed); });
    };
    machine_.registerEscapes(retropp::escapes(
        retropp::GuestEscape{.key = kLoopFrameWaitKey,
                             .at = kLoopFrameWait,
                             .handler = [this](retropp::Vm&, std::uint32_t) { reachedTheFrameWait(); },
                             .armed = false},
        retropp::GuestEscape{.key = kLoopHeadKey, .at = kBattleLoop, .replaces = stepEnd(false), .armed = false},
        retropp::GuestEscape{.key = kBattleClosedKey, .at = kTerminateBattleReturn, .replaces = stepEnd(true),
                             .armed = false},
        retropp::GuestEscape{.key = kTargetsCopyKey,
                             .at = kCreateActionTargetsCopy,
                             .handler = [this](retropp::Vm&, std::uint32_t) { targetsCopyNext(); },
                             .armed = false},
        retropp::GuestEscape{.key = kMagicEventUndodgeableKey,
                             .at = kMagicEventUndodgeable,
                             .handler = [this](retropp::Vm&, std::uint32_t) { magicEventUndodgeableNext(); },
                             .armed = false}));

    // While a reward routine runs, the graphics program's entry answers here: the command in the
    // accumulator, the cells the graphics program reads at their places.
    const auto cell = [](std::uint32_t at) { return retropp::Location::memory(kWorkRam | at); };
    machine_.registerEscapes(retropp::escapes(retropp::GuestEscape{
        .key = kGraphicsKey,
        .at = retropp::snes::rtl(kExecBtlGfxExt),
        .replaces = retropp::routine(
            retropp::RoutineBinding{.inputs = {retropp::snes::A, cell(0x0010), cell(0x2D6E), cell(0x2D72),
                                               cell(0x2E72), cell(0x2E76), cell(0x2F35), cell(0x2F39), cell(0x2F3D)}},
            [this](std::uint8_t command, std::uint8_t slot, std::uint32_t script, std::uint8_t scriptEnd,
                   std::uint32_t item, std::uint8_t itemLast, std::uint32_t first4, std::uint32_t middle4,
                   std::uint32_t last4) {
                GraphicsCall call{.command = command, .slot = slot};
                const auto unpack = [](auto& bytes, std::size_t from, std::uint32_t word) {
                    for (std::size_t i = 0; i < 4; ++i) {
                        bytes[from + i] = static_cast<std::uint8_t>(word >> (8 * i) & 0xFF);
                    }
                };
                unpack(call.script, 0, script);
                call.script[4] = scriptEnd;
                unpack(call.item, 0, item);
                call.item[4] = itemLast;
                unpack(call.messageVariables, 0, first4);
                unpack(call.messageVariables, 4, middle4);
                unpack(call.messageVariables, 8, last4);
                graphicsCalls_.push_back(call);
                answerGraphics(call);
            }),
        .armed = false}));
}

void DamageOracle::answerGraphics(const GraphicsCall& call) {
    answering_ = true;
    if (start_) {
        answerInBattle(call);
    }
    if (whole_) {
        answerInWholeBattle(call);
    }
    // The handlers are entered as the graphics program's entry leaves them: the command's place in its
    // table in X, the accumulator narrow and the index registers wide.
    const auto tableIndex = static_cast<std::uint16_t>(call.command * 2);
    switch (call.command) {
        case kAddItemObtained:
            addItemObtained_(tableIndex, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
            break;
        case kMergeItemsObtained:
            mergeItemsObtained_(tableIndex, kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
            break;
        case kMenuQueuePosition:
            unansweredGraphics_ = call.command;
            break;
        default:
            // Every other command waits on the screen or the sound, or does nothing on a parked machine.
            break;
    }
    answering_ = false;
}

void DamageOracle::registerGate() {
    // The trace's watches already answer for their ranges and hand every access to noteGuestAccess();
    // the gate watches the rest of the battle's work RAM, gap by gap.
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> taken = DamageTrace::watchedRanges();
    const auto handler = [this](bool write) {
        return [this, write](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
            noteGuestAccess(kWorkRam | (at & 0xFFFF), value, write);
            return retropp::AccessVerdict::proceed();
        };
    };
    retropp::WatchMap gaps;
    std::uint32_t from = kGateStart;
    for (const auto& [start, end] : taken) {
        if (start > from) {
            gaps.declarations.push_back(retropp::GuestWatch{.key = "read-set gate " + std::to_string(from),
                                                            .at = retropp::MemoryRegion{.at = from, .size = start - from},
                                                            .onRead = handler(false),
                                                            .onWrite = handler(true)});
        }
        from = std::max(from, end);
    }
    if (from < kGateEnd) {
        gaps.declarations.push_back(retropp::GuestWatch{.key = "read-set gate " + std::to_string(from),
                                                        .at = retropp::MemoryRegion{.at = from, .size = kGateEnd - from},
                                                        .onRead = handler(false),
                                                        .onWrite = handler(true)});
    }
    // A whole battle's accesses beyond the battle's cells, from past the trace's last watch to the end
    // of the second bank of work RAM. The gaps above walk on past $7E:3FFF to the trace's last watch, and
    // they and the trace's own watches hand every access to noteGuestAccess().
    const std::uint32_t past = std::max(from, kGateEnd);
    if (past < kSecondWorkRamBankEnd) {
        // Switched on for a whole battle only (battleStartAsItStands()): every other call leaves the
        // machine unwatched there.
        gaps.declarations.push_back(retropp::GuestWatch{
            .key = kBeyondKey,
            .at = retropp::MemoryRegion{.at = past, .size = kSecondWorkRamBankEnd - past},
            .onRead = [this](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
                noteAccessBeyond(at, value, false);
                return retropp::AccessVerdict::proceed();
            },
            .onWrite = [this](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
                noteAccessBeyond(at, value, true);
                return retropp::AccessVerdict::proceed();
            },
            .armed = false});
    }
    machine_.registerWatches(gaps);
}

DamageOracle::~DamageOracle() = default;

void DamageOracle::start() {
    // The machine runs on the game's own tick, a microsecond at a time, until the reset escape
    // fires, and is parked there.
    machine_.run(retropp::Vm::Advance::OnTick);
    for (int step = 0; step < kStartStepsAllowed && !reset_; ++step) {
        machine_.advanceTick(kStartStep);
    }
    machine_.stop();
    if (!reset_) {
        throw std::runtime_error("the cartridge did not reach its reset code");
    }
}

void DamageOracle::call(const std::function<void()>& routine) {
    if (!reset_) {
        throw std::logic_error("the oracle's cartridge has not been started (DamageOracle::start)");
    }
    if (beyondArmed_ && !start_) {
        machine_.watches()[kBeyondKey].armed(false);
        beyondArmed_ = false;
    }
    writes_.clear();
    trace_->forgetContextWrites();
    unwrittenReads_.clear();
    gateFresh_ = false;
    gateCalling_ = true;
    recording_ = true;
    routine();
    recording_ = false;
    gateCalling_ = false;
    gateFresh_ = true;
}

void runOnLoop(const std::function<void()>& work) {
    TickClock clock{retropp::TimingProfile::Snes.tickPeriod()};
    retropp::RunLoop loop{clock, retropp::TimingProfile::Snes};
    std::exception_ptr failure;
    bool done = false;
    loop.simTick([&](const retropp::InputState&) {
        if (!done) {
            done = true;
            try {
                work();
            } catch (...) {
                failure = std::current_exception();
            }
            loop.exitRequest();
        }
    });
    loop.renderLoop([] {});
    loop.exitAction([] { return retropp::ExitVerdict::Proceed; });
    loop.run();
    if (failure) {
        std::rethrow_exception(failure);
    }
}

}  // namespace ostinato::trace
