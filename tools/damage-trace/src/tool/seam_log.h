// The run with no window (--headless) and the state check (--check-state).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/game_version.h"
#include "tool/cell_log.h"
#include "tool/pictures.h"
#include "tool/starting_state.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

int runHeadless(std::span<const std::uint8_t> image, const std::filesystem::path& directory, std::uint64_t frames,
                const std::optional<StartingState>& from, std::span<const trace::InputStep> script,
                std::span<const std::uint64_t> pictureFrames, std::span<const trace::CellWrite> writes,
                std::span<const WatchedCells> watched, bool seam);

// A machine run for `ticks` from power-on and one restored from its state through a file, both run on
// past the state's moment by kCarryOnTicks: the two must stand on the same memory and picture, and one
// tick short of that they must not, so the comparison is seen to see a tick.
int runStateCheck(std::span<const std::uint8_t> image, GameVersion cartridge, const std::filesystem::path& directory,
                  std::uint64_t ticks);

}  // namespace damage_trace
