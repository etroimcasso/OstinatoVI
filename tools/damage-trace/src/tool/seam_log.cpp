// runHeadless() and runStateCheck(): a run with no window, logging the trace, the watched cells or
// what the graphics program hands the battle program (SeamLog, to seam.log), and a machine's state
// written, restored and run on beside the machine it came from.

#include "tool/seam_log.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "retropp/clock.h"
#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
#include "retropp/memory_region.h"
#include "retropp/raster_content.h"
#include "retropp/snes.h"
#include "retropp/vm.h"

#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/game_version.h"
#include "tool/cell_log.h"
#include "tool/pictures.h"
#include "tool/starting_state.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

namespace {

// What the graphics program hands the battle program: which of the battle's cells it writes, and which
// of those the battle program then reads. Logged to `seam.log`, one line each:
//
//     battle entered <frame>
//     graphics <frame> <command>
//     clock <frame>
//     write <frame> <writer> $7E:AAAA <byte>
//     read <frame> <reader> $7E:AAAA <byte> <writer>
//     battle returned <frame>
//
// The code running is told apart by where it is entered. The battle program runs from its entry
// ($C2:0000) to TerminateBattle's RTL ($C2:00CB); nothing outside it is logged. The graphics program runs from its entry
// ($C1:0000, ExecBtlGfx_ext) to the battle program's instruction after the call ($C2:6429); inside it,
// the battle program runs again from its clock's entry ($C2:0003, UpdateBattleTime_ext) to the graphics
// program's instruction after either call of it ($C1:0223 in BtlGfx_01, $C1:0249 in WaitFrame); the
// interrupt runs from BattleNMI ($C1:0BA7) to its RTI ($C1:0D4A). The command a call asks for is the
// first of the graphics program's handlers entered after its entry.
//
// A `write` is any write by the graphics program (`graphics`) or by the interrupt, named with the code
// it interrupted (`interrupt-in-graphics`, `interrupt-in-battle`, `interrupt-in-clock`), to
// $7E:0000-3FFF. A `read` is a read by the battle program (`battle`, or `clock` inside a graphics call)
// of a cell whose last write was one of those, named with that writer. Writes the battle program makes
// itself are not logged; they only mark a cell as the battle program's again. A write made by a memory
// transfer rather than by an instruction is not seen.
class SeamLog {
public:
    SeamLog(Vm& machine, const std::filesystem::path& directory, const std::uint64_t& frame)
        : log_(directory / "seam.log", std::ios::app), frame_(frame) {
        lastWriter_.fill(Who::Outside);
        const auto mark = [](const char* key, std::uint32_t at, auto handler) {
            return GuestEscape{.key = key, .at = at, .handler = [handler](Vm&, std::uint32_t) { handler(); }};
        };
        EscapeMap marks;
        marks.declarations.push_back(mark("seam: battle entry", kBattleEntry, [this] {
            log_ << "battle entered " << frame_ << '\n';
            running_.assign(1, Who::Battle);
            interrupted_.clear();
        }));
        marks.declarations.push_back(mark("seam: battle return", kBattleReturn, [this] {
            log_ << "battle returned " << frame_ << '\n';
            running_.assign(1, Who::Outside);
            interrupted_.clear();
        }));
        marks.declarations.push_back(mark("seam: graphics entry", kGraphicsEntry, [this] {
            running_.push_back(Who::Graphics);
            commandPending_ = true;
        }));
        marks.declarations.push_back(mark("seam: graphics return", kGraphicsReturn, [this] {
            if (top() == Who::Graphics) {
                running_.pop_back();
            }
        }));
        marks.declarations.push_back(mark("seam: clock entry", kClockEntry, [this] {
            if (top() == Who::Graphics) {
                log_ << "clock " << frame_ << '\n';
                running_.push_back(Who::Clock);
            }
        }));
        for (const std::uint32_t at : kClockReturns) {
            marks.declarations.push_back(mark(at == kClockReturns[0] ? "seam: clock return 1" : "seam: clock return 2", at,
                                              [this] {
                                                  if (top() == Who::Clock) {
                                                      running_.pop_back();
                                                  }
                                              }));
        }
        marks.declarations.push_back(mark("seam: interrupt entry", kInterruptEntry, [this] {
            interrupted_.push_back(top());
            running_.push_back(Who::Interrupt);
        }));
        marks.declarations.push_back(mark("seam: interrupt return", kInterruptReturn, [this] {
            if (top() == Who::Interrupt) {
                running_.pop_back();
                interrupted_.pop_back();
            }
        }));
        for (std::size_t command = 0; command < kGraphicsHandlers.size(); ++command) {
            handlerKeys_.push_back("seam: graphics command " + std::to_string(command));
            marks.declarations.push_back(GuestEscape{
                .key = handlerKeys_.back(),
                .at = kGraphicsEntry | std::uint32_t{kGraphicsHandlers[command]},
                .handler = [this, command](Vm&, std::uint32_t) {
                    if (commandPending_) {
                        commandPending_ = false;
                        char text[48];
                        std::snprintf(text, sizeof text, "graphics %llu %02zX", static_cast<unsigned long long>(frame_),
                                      command);
                        log_ << text << '\n';
                    }
                }});
        }
        machine.registerEscapes(marks);
        machine.registerWatches(watches(GuestWatch{
            .key = "seam: the battle's cells",
            .at = MemoryRegion{.at = 0x7E0000, .size = kCells},
            .onRead = [this](Vm&, std::uint32_t at, std::uint8_t value) {
                noteRead(at & 0xFFFF, value);
                return AccessVerdict::proceed();
            },
            .onWrite = [this](Vm&, std::uint32_t at, std::uint8_t value) {
                noteWrite(at & 0xFFFF, value);
                return AccessVerdict::proceed();
            }}));
    }

private:
    // The code running: whatever runs outside a battle, the battle program, the graphics program, the
    // battle program's clock inside a graphics call, and the interrupt — each interrupt write written
    // as what it interrupted.
    enum class Who : std::uint8_t {
        Outside, Battle, Graphics, Clock, Interrupt, InterruptInGraphics, InterruptInBattle, InterruptInClock,
    };

    static constexpr std::uint32_t kCells = 0x4000;
    // TerminateBattle's RTL (battle/battle_main.asm:154), the battle program's one way out.
    static constexpr std::uint32_t kBattleReturn = 0xC200CB;
    static constexpr std::uint32_t kGraphicsEntry = 0xC10000;
    static constexpr std::uint32_t kGraphicsReturn = 0xC26429;
    static constexpr std::uint32_t kClockEntry = 0xC20003;
    static constexpr std::array<std::uint32_t, 2> kClockReturns{0xC10223, 0xC10249};
    static constexpr std::uint32_t kInterruptEntry = 0xC10BA7;
    static constexpr std::uint32_t kInterruptReturn = 0xC10D4A;
    // BtlGfx_00 to BtlGfx_0d, as the graphics program's table at $C1:0008 points.
    static constexpr std::array<std::uint16_t, 14> kGraphicsHandlers{{
        0x01EC, 0x01FB, 0x0257, 0x024C, 0x950F, 0x01CC, 0x0197, 0x0170, 0x0117, 0x00AF, 0x00AB, 0x0171, 0x002D, 0x0024,
    }};

    std::ofstream log_;
    const std::uint64_t& frame_;
    std::vector<Who> running_{Who::Outside};
    std::vector<Who> interrupted_;
    bool commandPending_ = false;
    std::vector<std::string> handlerKeys_;
    std::array<Who, kCells> lastWriter_{};

    [[nodiscard]] Who top() const { return running_.back(); }

    // Who a write is made by: the interrupt's as what it interrupted.
    [[nodiscard]] Who writer() const {
        if (top() != Who::Interrupt) {
            return top();
        }
        switch (interrupted_.back()) {
            case Who::Graphics: return Who::InterruptInGraphics;
            case Who::Clock: return Who::InterruptInClock;
            default: return Who::InterruptInBattle;
        }
    }

    static const char* name(Who who) {
        switch (who) {
            case Who::Outside: return "outside";
            case Who::Battle: return "battle";
            case Who::Graphics: return "graphics";
            case Who::Clock: return "clock";
            case Who::Interrupt: return "interrupt";
            case Who::InterruptInGraphics: return "interrupt-in-graphics";
            case Who::InterruptInBattle: return "interrupt-in-battle";
            case Who::InterruptInClock: return "interrupt-in-clock";
        }
        return "?";
    }

    static bool battleProgram(Who who) { return who == Who::Battle || who == Who::Clock; }
    static bool graphicsSide(Who who) { return who != Who::Outside && !battleProgram(who); }

    void noteWrite(std::uint32_t cell, std::uint8_t value) {
        if (cell >= kCells) {
            return;
        }
        const Who who = writer();
        lastWriter_[cell] = who;
        if (graphicsSide(who)) {
            char text[80];
            std::snprintf(text, sizeof text, "write %llu %s $7E:%04X %02X", static_cast<unsigned long long>(frame_),
                          name(who), cell, value);
            log_ << text << '\n';
        }
    }

    void noteRead(std::uint32_t cell, std::uint8_t value) {
        if (cell >= kCells || !battleProgram(top()) || !graphicsSide(lastWriter_[cell])) {
            return;
        }
        char text[96];
        std::snprintf(text, sizeof text, "read %llu %s $7E:%04X %02X %s", static_cast<unsigned long long>(frame_),
                      name(top()), cell, value, name(lastWriter_[cell]));
        log_ << text << '\n';
    }
};

}  // namespace

int runHeadless(std::span<const std::uint8_t> image, const std::filesystem::path& directory, std::uint64_t frames,
                const std::optional<StartingState>& from, std::span<const trace::InputStep> script,
                std::span<const std::uint64_t> pictureFrames, std::span<const trace::CellWrite> writes,
                std::span<const WatchedCells> watched, bool seam) {
    std::ofstream log{directory / "damage-trace.log", std::ios::app};
    int result = 0;
    trace::runOnLoop([&] {
        Vm::SNES machine{VmConfig{.video = true}};
        machine.hostRom(image);
        std::uint64_t frame = 0;
        // The seam's watch covers the cells the trace watches, so a run logs one or the other.
        std::optional<SeamLog> seamLog;
        std::optional<CellLog> cells;
        std::optional<trace::DamageTrace> traced;
        if (seam) {
            seamLog.emplace(machine, directory, frame);
        } else {
            cells.emplace(machine, directory, watched, frame);
            traced.emplace(machine,
                           [&](const std::string& line) {
                               log << line << '\n';
                               if (line.find(" enter ") != std::string::npos) {
                                   std::printf("%s\n", line.c_str());
                               }
                           },
                           [&] { return frame; },
                           [&](std::uint32_t at, std::uint8_t value, bool write) {
                               if (write) {
                                   cells->traceWrite(at, value);
                               }
                           });
        }
        if (from) {
            if (!machine.restore(from->state.bytes)) {
                std::fprintf(stderr, "%s is not a state this machine takes\n", from->file.string().c_str());
                result = 1;
                return;
            }
            log << "state restored from " << from->file.string() << '\n';
        }
        const trace::WorkRamBytes::Map ram = trace::declareWorkRamBytes(machine);
        machine.run(Vm::Advance::OnTick);
        for (; frame < frames; ++frame) {
            trace::writeCells(machine, ram, writes, frame);
            // The trace's watches do not see these writes, so it is told of them.
            for (const trace::CellWrite& write : writes) {
                if (write.frame != frame || !traced) {
                    continue;
                }
                for (std::size_t i = 0; i < write.bytes.size(); ++i) {
                    traced->noteWrite(write.at + static_cast<std::uint32_t>(i), write.bytes[i]);
                }
            }
            machine.buttons(snes::Ports{.one = trace::buttonsAt(script, frame), .two = std::nullopt});
            machine.advanceTick();
            if (cells && !watched.empty()) {
                cells->logFrame(machine, ram);
            }
            if (std::find(pictureFrames.begin(), pictureFrames.end(), frame) != pictureFrames.end()) {
                const std::filesystem::path file = directory / ("frame-" + std::to_string(frame) + ".ppm");
                if (!writePicture(file, machine.video())) {
                    std::fprintf(stderr, "%s could not be written\n", file.string().c_str());
                    result = 1;
                }
            }
        }
        machine.stop();
        log << "stopped at frame " << frame << '\n';
    });
    std::printf("%llu frames traced to %s\n", static_cast<unsigned long long>(frames),
                (directory / "damage-trace.log").string().c_str());
    return result;
}

int runStateCheck(std::span<const std::uint8_t> image, GameVersion cartridge, const std::filesystem::path& directory,
                  std::uint64_t ticks) {
    constexpr std::uint64_t kCarryOnTicks = 240;
    const std::filesystem::path file = directory / "check.state";
    int result = 1;
    trace::runOnLoop([&] {
        Vm::SNES source{VmConfig{.video = true}};
        source.hostRom(image);
        const RegionMapId<WorkRam> sourceRam = declareWorkRam(source);
        source.run(Vm::Advance::OnTick);
        for (std::uint64_t tick = 0; tick < ticks; ++tick) {
            source.advanceTick();
        }
        trace::writeMachineState(file, cartridge, source.state());

        const trace::MachineState saved = trace::readMachineState(file);
        Vm::SNES restored{VmConfig{.video = true}};
        restored.hostRom(image);
        const RegionMapId<WorkRam> restoredRam = declareWorkRam(restored);
        if (!restored.restore(saved.bytes)) {
            std::fprintf(stderr, "%s is not a state this machine takes\n", file.string().c_str());
            return;
        }
        restored.run(Vm::Advance::OnTick);
        for (std::uint64_t tick = 0; tick < kCarryOnTicks; ++tick) {
            restored.advanceTick();
        }
        for (std::uint64_t tick = 0; tick + 1 < kCarryOnTicks; ++tick) {
            source.advanceTick();
        }
        const std::vector<std::uint8_t> restoredCells = restored.read(restoredRam, &WorkRam::cells);
        const bool shortDiffers = source.read(sourceRam, &WorkRam::cells) != restoredCells;
        source.advanceTick();
        const bool memorySame = source.read(sourceRam, &WorkRam::cells) == restoredCells;
        const RasterContent theirs = source.video();
        const RasterContent ours = restored.video();
        const bool pictureSame = std::equal(theirs.pixels.begin(), theirs.pixels.end(), ours.pixels.begin(), ours.pixels.end());
        source.stop();
        restored.stop();
        std::printf("state of %llu bytes: one tick short the memory %s; at %llu ticks on, memory %s, picture %s\n",
                    static_cast<unsigned long long>(saved.bytes.size()), shortDiffers ? "differs" : "DOES NOT DIFFER",
                    static_cast<unsigned long long>(kCarryOnTicks), memorySame ? "same" : "DIFFERS",
                    pictureSame ? "same" : "DIFFERS");
        result = shortDiffers && memorySame && pictureSame ? 0 : 1;
    });
    return result;
}

}  // namespace damage_trace
