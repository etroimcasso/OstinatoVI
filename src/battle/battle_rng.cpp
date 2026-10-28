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

std::optional<std::size_t> pickWeighted(BattleRng& rng, const std::array<RandomThreshold, 4>& weights,
                                        const std::array<bool, 4>& offered) {
    // The running total is a byte: a full row of offered weights wraps it.
    std::array<std::uint8_t, 4> reach{};
    std::uint8_t total = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (offered[i]) {
            total = static_cast<std::uint8_t>(total + weights[i].value() + 1);
        }
        reach[i] = total;
    }
    const std::uint8_t roll = rng.below(total);
    for (std::size_t i = 0; i < reach.size(); ++i) {
        if (roll < reach[i]) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace ostinato::battle
