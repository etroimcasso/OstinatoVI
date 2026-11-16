// The --check sweeps, each routine against the port over seeded scenarios, and the --replay of a
// recorded trace.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "oracle/damage_oracle.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

// One draw of one sweep: what was drawn, both answers where they are one number, every cell where
// the port's answer parts from the cartridge's, and for an attack's special effect on one target, the
// target it was worked out for (the oracle calls the routine with no statuses worked out before it, so
// its trace does not show the target).
// For a sweep with a reach table, the rows of it the draw reached.
struct DrawResult {
    std::string described;
    std::string answers;
    std::string differences;
    std::optional<battle::BattlerSlot> target{};
    std::vector<std::string_view> reached{};
};

using SweepDraw = DrawResult (*)(trace::DamageOracle&, std::mt19937&);
using ReachRows = std::span<const std::string_view> (*)();

// A sweep with `reach` prints, after its draws, how many reached each of its rows, and names a row no
// draw reached.
struct Sweep {
    std::string_view name;
    SweepDraw draw;
    ReachRows reach = nullptr;
};

// Every routine the oracle runs, by the name --only takes, in the order --check runs them.
extern const std::array<Sweep, 35> kSweeps;

struct CheckOptions {
    int draws = 0;
    std::uint32_t seed = trace::kSweepSeed;
    std::vector<std::string> only;  // the sweeps to run, by name; every one when empty
    bool replayTraced = false;      // also replay against the port the calls the cartridge made in each draw
};

int runCheck(std::span<const std::uint8_t> image, const std::filesystem::path& directory, const CheckOptions& options);

int runReplay(std::span<const std::uint8_t> image, const std::filesystem::path& directory,
              const std::filesystem::path& recording);

}  // namespace damage_trace
