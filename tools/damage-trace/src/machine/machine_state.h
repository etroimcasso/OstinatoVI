// The trace tool's two file formats — a machine's state with the name of the cartridge it belongs to,
// and a script of the buttons held over a run with no window — and the cells a run with no window
// writes into work RAM as it goes.
//
// A state file opens with two lines of text and carries the engine's bytes to its end:
//
//     OstinatoVI machine state 1
//     cartridge US_1_1
//     <the bytes Vm::state() handed over>
//
// The engine refuses bytes that are not a state of its kind of machine; which cartridge a state belongs
// to is known only here, so a caller compares the file's cartridge with the image it hosts before it
// restores:
//
//     const trace::MachineState saved = trace::readMachineState(file);
//     if (!trace::stateIsOf(saved, *assets::identifyRom(image))) { /* refuse */ }
//     machine.hostRom(image);
//     if (!machine.restore(saved.bytes)) { /* refuse */ }
//
// An input script has one line per change, `<frame> <buttons>`: the buttons are `-` for none, or
// names from A B X Y L R Start Select Up Down Left Right joined by `+`, held from that frame until
// the next line's. `#` starts a comment, blank lines are passed over, and the frames strictly
// increase.
//
//     # Past the title screen.
//     120 Start
//     130 -
//     400 Up+A
//
// A cell write names a frame, an address in work RAM ($7E:0000-$7F:FFFF, the `$` optional) and the
// bytes, as an even number of hex digits, written from that address on. They are written before that
// frame runs, so the game sees them from that frame on:
//
//     const trace::CellWrite write = trace::readCellWrite("300", "$7E:3C9C", "E0");
//     const trace::WorkRamBytes::Map ram = trace::declareWorkRamBytes(machine);   // before run()
//     machine.run(retropp::Vm::Advance::OnTick);
//     for (std::uint64_t frame = 0; frame < frames; ++frame) {
//         trace::writeCells(machine, ram, writes, frame);
//         machine.advanceTick();
//     }
#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <span>
#include <string_view>
#include <vector>

#include "ostinato/game_version.h"
#include "retropp/memory_region.h"
#include "retropp/snes.h"
#include "retropp/vm.h"

namespace ostinato::trace {

// A state as a file holds it.
struct MachineState {
    GameVersion cartridge = GameVersion::US_1_1;
    std::vector<std::uint8_t> bytes;
};

// The name a state file gives a revision: `JP_1_0`, `US_1_0` or `US_1_1`.
[[nodiscard]] std::string_view cartridgeName(GameVersion cartridge);

// Writes the whole file, replacing one already there. Throws std::runtime_error naming the file when
// it cannot be written.
void writeMachineState(const std::filesystem::path& file, GameVersion cartridge,
                       std::span<const std::uint8_t> bytes);

// Throws std::runtime_error naming the file and the reason when it cannot be read, does not open with
// the mark, names no revision, or holds no bytes after its two lines.
[[nodiscard]] MachineState readMachineState(const std::filesystem::path& file);

// Whether the state was taken on this revision of the cartridge.
[[nodiscard]] bool stateIsOf(const MachineState& state, GameVersion cartridge);

// One line of a script: what is held from this frame on.
struct InputStep {
    std::uint64_t frame = 0;
    retropp::snes::Buttons buttons;
};

// Throws std::runtime_error naming the line for a name that is not a button, a frame that does not
// increase, or a line of any other shape.
[[nodiscard]] std::vector<InputStep> readInputScript(std::istream& script);

// What the script holds on a frame: the last line at or before it, and nothing before the first line.
[[nodiscard]] retropp::snes::Buttons buttonsAt(std::span<const InputStep> script, std::uint64_t frame);

// Bytes written into work RAM before a frame runs.
struct CellWrite {
    std::uint64_t frame = 0;
    std::uint32_t at = 0;  // the first byte's address, $7E:0000-$7F:FFFF
    std::vector<std::uint8_t> bytes;
};

// The three words of a write: `<frame> <$BB:AAAA> <bytes>`. Throws std::runtime_error naming the word
// for a frame that is not a number, an address outside work RAM or of any other shape, no bytes or an
// odd number of hex digits, and bytes running past $7F:FFFF.
[[nodiscard]] CellWrite readCellWrite(std::string_view frame, std::string_view address, std::string_view bytes);

// Work RAM declared one byte to an entry, so a write of any length lands at its own address on a
// running machine. Declare it before run().
struct WorkRamBytes {
    retropp::MemoryRegion bytes;

    using Map = retropp::RegionMapId<WorkRamBytes>;
};

[[nodiscard]] WorkRamBytes::Map declareWorkRamBytes(retropp::Vm& machine);

// Writes every write that names `frame`, in the order given. On a running machine the bytes land at
// the next step boundary, before the tick that follows; a later write to the same cell wins.
void writeCells(retropp::Vm& machine, const WorkRamBytes::Map& ram, std::span<const CellWrite> writes,
                std::uint64_t frame);

}  // namespace ostinato::trace
