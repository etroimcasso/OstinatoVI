#include "data/monster_items.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 384-entry table. The generated rows carry the ROM record data;
// designated initializers at every row keep each field self-labeling, and
// each entry carries its identity as the MonsterId enumerator.
constexpr std::array<MonsterItemsEntry, 384> kMonsterItems = {{
#include "data/generated/monster_items_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal its
// array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kMonsterItems.size(); ++i) {
        if (static_cast<std::size_t>(kMonsterItems[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kMonsterItems entry id fields must match array positions");

}  // namespace

const MonsterItems& getMonsterItems(MonsterId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kMonsterItems.size() && "monster id out of range");
    return kMonsterItems[raw].record;
}

std::span<const MonsterItemsEntry> monsterItems() {
    return kMonsterItems;
}

}  // namespace ostinato
