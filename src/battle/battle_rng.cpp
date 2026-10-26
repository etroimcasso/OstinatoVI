#include "battle/battle_rng.h"

#include "data/rng_table.h"

namespace ostinato::battle {

std::uint8_t BattleRng::next() {
    ++position_;
    return rngByte(position_);
}

std::uint8_t BattleRng::below(std::uint8_t n) {
    return static_cast<std::uint8_t>((next() * n) >> 8);
}

bool BattleRng::coin() {
    return (next() & 1) != 0;
}

std::optional<BattlerSlot> BattleRng::pickFrom(BattlerSet set) {
    const int count = set.count();
    if (count == 0) {
        return std::nullopt;
    }
    int remaining = below(static_cast<std::uint8_t>(count));
    for (int index = 0; index < kBattlerSlots; ++index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (!set.has(slot)) {
            continue;
        }
        if (remaining == 0) {
            return slot;
        }
        --remaining;
    }
    return std::nullopt;
}

}  // namespace ostinato::battle
