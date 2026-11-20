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
// For a sweep with a reach table, the rows of it the draw reached, and for a draw aimed at one, what it
// was aimed at and whether it missed.
struct DrawResult {
    std::string described;
    std::string answers;
    std::string differences;
    std::optional<battle::BattlerSlot> target{};
    std::vector<std::string_view> reached{};
    std::optional<std::string_view> aimedAt{};
    bool aimMissed = false;
};

using SweepDraw = DrawResult (*)(trace::DamageOracle&, std::mt19937&);
using ReachRows = std::span<const std::string_view> (*)();

// The most draws a brief check (--brief) runs of a sweep that names no fewer.
inline constexpr int kBriefDraws = 20;

// A sweep with `reach` prints, after its draws, how many reached each of its rows, and names a row no
// draw reached; then, for each row its draws were aimed at, how many were and how many missed. A brief check draws at most `briefDraws` of it: a sweep whose draws take seconds each
// names fewer.
struct Sweep {
    std::string_view name;
    SweepDraw draw;
    ReachRows reach = nullptr;
    int briefDraws = kBriefDraws;
};

// Every routine the oracle runs, by the name --only takes, in the order --check runs them.
extern const std::array<Sweep, 36> kSweeps;

struct CheckOptions {
    int draws = 0;
    std::uint32_t seed = trace::kSweepSeed;
    std::vector<std::string> only;  // the sweeps to run, by name; every one when empty
    bool replayTraced = false;      // also replay against the port the calls the cartridge made in each draw
    bool brief = false;             // each sweep draws no more than its own briefDraws
};

int runCheck(std::span<const std::uint8_t> image, const std::filesystem::path& directory, const CheckOptions& options);

int runReplay(std::span<const std::uint8_t> image, const std::filesystem::path& directory,
              const std::filesystem::path& recording);

}  // namespace damage_trace
