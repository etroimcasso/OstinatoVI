// The cells the trace and its parsers both read: the params block, the script queue, the reflected
// attack's aiming rule and the blocks' attacker, in namespace cells. A file that names them says `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstdint>

namespace ostinato::trace::cells {

inline constexpr std::uint32_t kParamsCells = 0x7E00A0;
// The script queue's whole span, and the aiming rule set while a spell bounces off Reflect.
inline constexpr std::uint32_t kQueueSpan = 256;
inline constexpr std::uint8_t kReflectedRule = 0x10;
inline constexpr std::uint32_t kParamsSize = 16;
inline constexpr std::uint32_t kBlocksSize = 256;
// The attacker is the second byte of a params block.
inline constexpr std::uint32_t kBlockAttacker = 1;

}  // namespace ostinato::trace::cells
