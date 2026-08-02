#pragma once

#include <string_view>

namespace ostinato {

// The port's identity string. Phase-0 placeholder surface: it gives the port
// library a real symbol and a first consumer-testable entry point. Never empty.
[[nodiscard]] std::string_view portName() noexcept;

}  // namespace ostinato
