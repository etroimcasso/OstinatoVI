// Shared helpers for the items-obtained tests: a place of the list no item's record could fill.
#pragma once

#include <cstdint>

#include "battle/items_obtained.h"
#include "ostinato/item_id.h"

namespace ostinato::test {

// A place of the items obtained no item's record could fill: a flags byte with bits no record sets ($07)
// and a fifth byte with its high four ($F0), as another program can leave the last three places between
// battles (docs/contracts/battle_rewards.md § D.8).
inline constexpr battle::ItemRecord kNoRecordSaysThis{static_cast<std::uint8_t>(ItemId::POTION), 0x07, 0x00, 5, 0xF0};

}  // namespace ostinato::test
