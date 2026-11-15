// What the AI's wiring tests and its traced calls share: where the scripts a scripted battle gives a
// party member start. Included by tests/test_ai_trace.cpp and tests/oracle/test_ai_trace.cpp.
#pragma once

#include <cstdint>

namespace ostinato::test {

// The scripts a scripted battle gives a party member start here.
inline constexpr std::uint16_t kCharacterAiScripts = 256;

}  // namespace ostinato::test
