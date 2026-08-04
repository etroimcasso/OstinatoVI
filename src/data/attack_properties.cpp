#include "data/attack_properties.h"

#include <array>
#include <cstddef>

namespace ostinato {

namespace {

// The 256-entry EN table. The generated rows carry the ROM record data;
// designated initializers at every row keep each field self-labeling, and each
// entry carries its identity as the AttackId enumerator.
constexpr std::array<AttackPropertiesEntry, 256> kAttackPropertiesEn = {{
#include "data/generated/magic_prop_en_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal its
// array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kAttackPropertiesEn.size(); ++i) {
        if (static_cast<std::size_t>(kAttackPropertiesEn[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kAttackPropertiesEn entry id fields must match array positions");

}  // namespace

const AttackProperties& getAttackProperties(AttackId id) {
    // AttackId is uint8_t, so every value indexes the 256-entry table by
    // construction.
    return kAttackPropertiesEn[static_cast<std::size_t>(id)].record;
}

std::span<const AttackPropertiesEntry> attackPropertiesEn() {
    return kAttackPropertiesEn;
}

}  // namespace ostinato
