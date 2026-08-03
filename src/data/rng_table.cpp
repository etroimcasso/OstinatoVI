#include "data/rng_table.h"

namespace ostinato {

std::uint8_t rngByte(std::uint8_t index) { return kRngTable[index].value; }

}  // namespace ostinato
