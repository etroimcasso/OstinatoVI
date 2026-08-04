#pragma once

#include <string_view>

namespace ostinato {

// The port's identity string: a real symbol and a consumer-testable entry
// point for the port library. Never empty.
[[nodiscard]] std::string_view portName() noexcept;

}  // namespace ostinato
