// The draws draws.h declares.

#include "scenarios/common/draws.h"

#include <array>
#include <cstdint>
#include <random>

#include "battle/battler_set.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::trace {

namespace detail {

std::uint8_t byte(std::mt19937& rng) {
    return static_cast<std::uint8_t>(rng() & 0xFF);
}

std::mt19937 generatorOfItsOwn(const std::mt19937& rng, std::uint32_t place) {
    std::mt19937 standing = rng;
    std::seed_seq mixed{static_cast<std::uint32_t>(standing()), static_cast<std::uint32_t>(standing()), place};
    return std::mt19937{mixed};
}

bool oneIn(std::mt19937& rng, unsigned n) {
    return rng() % n == 0;
}

std::uint8_t sparse(std::mt19937& rng) {
    return static_cast<std::uint8_t>(byte(rng) & byte(rng));
}

int upTo(std::mt19937& rng, int most) {
    return static_cast<int>(rng() % static_cast<unsigned>(most + 1));
}

battle::BattlerSlot anySlot(std::mt19937& rng) {
    return battle::BattlerSlot::fromIndex(static_cast<int>(rng() % battle::kBattlerSlots));
}

StatusSet anyStatuses(std::mt19937& rng) {
    return StatusSet{{byte(rng), byte(rng), byte(rng), byte(rng)}};
}

battle::BattlerSet anyBattlers(std::mt19937& rng) {
    return battle::BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x0F)),
                              BattleSlotMask(static_cast<std::uint8_t>(byte(rng) & 0x3F))};
}

StatusId anyStatus(std::mt19937& rng) {
    constexpr std::array<StatusId, 4> asked{StatusId::DEAD, StatusId::REFLECT, StatusId::HIDE, StatusId::DANCE};
    return oneIn(rng, 2) ? asked[rng() % asked.size()] : static_cast<StatusId>(rng() % 32);
}

}  // namespace detail

}  // namespace ostinato::trace
