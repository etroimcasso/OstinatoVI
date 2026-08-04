#include "data/battle_magic_points.h"

#include <cassert>
#include <cstddef>

namespace ostinato {

std::uint8_t magicPointsForBattle(std::uint16_t battleIndex) {
    assert(battleIndex < kBattleMagicPoints.size() &&
           "battle index out of range (0..511) — the >= 512 guard is "
           "consumer reward logic");
    return kBattleMagicPoints[static_cast<std::size_t>(battleIndex)].magicPoints;
}

}  // namespace ostinato
