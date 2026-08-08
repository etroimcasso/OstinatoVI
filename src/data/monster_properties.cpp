#include "data/monster_properties.h"

#include <array>
#include <cassert>
#include <cstddef>

#include "ostinato/element.h"
#include "ostinato/status_id.h"

namespace ostinato {

namespace {

// The 384-entry table. The generated rows carry the ROM record data;
// designated initializers at every row keep each field self-labeling, and
// each entry carries its identity as the MonsterId enumerator.
constexpr std::array<MonsterPropertiesEntry, 384> kMonsterProperties = {{
#include "data/generated/monster_prop_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal its
// array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kMonsterProperties.size(); ++i) {
        if (static_cast<std::size_t>(kMonsterProperties[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kMonsterProperties entry id fields must match array positions");

}  // namespace

const MonsterProperties& getMonsterProperties(MonsterId id) {
    const auto raw = static_cast<std::size_t>(id);
    assert(raw < kMonsterProperties.size() && "monster id out of range");
    return kMonsterProperties[raw].record;
}

std::span<const MonsterPropertiesEntry> monsterProperties() {
    return kMonsterProperties;
}

}  // namespace ostinato
