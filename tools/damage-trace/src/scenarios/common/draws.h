// The random draws every sweep makes its scenarios from: a byte, one chance in n, a sparse set of
// bits, a value up to a bound, a battler's slot, statuses and sets of battlers, and the party's
// limits they stay inside, in namespace detail.
#pragma once

#include <cstdint>
#include <random>

#include "battle/battler_set.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

namespace ostinato::trace::detail {

std::uint8_t byte(std::mt19937& rng);

// A generator for a draw that leaves `rng`'s own draws as they were: seeded from where `rng` stands
// and `place`, with `rng` not moved on. Two places drawn from one standing give unrelated draws.
std::mt19937 generatorOfItsOwn(const std::mt19937& rng, std::uint32_t place);

// A party member's HP and MP never pass these.
inline constexpr int kPartyMaxHp = 9999;
inline constexpr int kPartyMaxMp = 999;
inline constexpr int kPartyMaxLevel = 99;

// True one time in `n`.
bool oneIn(std::mt19937& rng, unsigned n);

// A byte with about a quarter of its bits set, so the flags it stands for come up one at a time.
std::uint8_t sparse(std::mt19937& rng);

int upTo(std::mt19937& rng, int most);

battle::BattlerSlot anySlot(std::mt19937& rng);

StatusSet anyStatuses(std::mt19937& rng);

battle::BattlerSet anyBattlers(std::mt19937& rng);

// A status, often one a selector or a condition asks about.
StatusId anyStatus(std::mt19937& rng);

}  // namespace ostinato::trace::detail
