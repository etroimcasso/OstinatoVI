#pragma once

#include <chrono>
#include <string_view>

namespace ostinato {

// The port's identity string: a real symbol and a consumer-testable entry
// point for the port library. Never empty.
[[nodiscard]] std::string_view portName() noexcept;

// One field frame: the game advances once per NTSC field, about 60.0988 times a second — 357,366
// cycles of the 236,250,000/11 Hz master clock (docs/features/build-system.md). A run loop
// driving the game ticks at this period.
inline constexpr std::chrono::nanoseconds kFieldTickPeriod{16'639'265};

}  // namespace ostinato
