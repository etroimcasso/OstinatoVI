// Reading the cartridge image, the state a run carries on from, and the trace in a window.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

// The battle program's entry — `Battle_ext: jmp BattleMain` (battle/battle_main.asm:39-40; the program
// sits at $C2:0000).
inline constexpr std::uint32_t kBattleEntry = 0xC20000;

// How many frames a scripted run goes on past its script's last line looking for a battle.
inline constexpr std::uint64_t kRunOutFrames = 600;

std::vector<std::uint8_t> readRom(const std::filesystem::path& file);

// A state the machine carries on from, in place of power-on.
struct StartingState {
    std::filesystem::path file;
    trace::MachineState state;
};

int runTrace(std::span<const std::uint8_t> image, const std::filesystem::path& directory,
             std::optional<std::uint64_t> quitAfterFrames, const std::optional<StartingState>& from);

}  // namespace damage_trace
