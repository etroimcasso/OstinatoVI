#include "data/item_properties.h"

#include <array>
#include <cstddef>

namespace ostinato {

namespace {

// The 256-entry EN table. The generated rows carry the ROM record data;
// designated initializers at every row keep each field self-labeling, and each
// entry carries its identity as the ItemId enumerator.
constexpr std::array<ItemPropertiesEntry, 256> kItemPropertiesEn = {{
#include "data/generated/item_prop_en_data.inc"
}};

// Self-consistency of the emitted rows: every entry's id field must equal its
// array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kItemPropertiesEn.size(); ++i) {
        if (static_cast<std::size_t>(kItemPropertiesEn[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kItemPropertiesEn entry id fields must match array positions");

}  // namespace

const ItemProperties& getItemProperties(ItemId id) {
    // ItemId is uint8_t, so every value indexes the 256-entry table by
    // construction.
    return kItemPropertiesEn[static_cast<std::size_t>(id)].record;
}

std::span<const ItemPropertiesEntry> itemPropertiesEn() {
    return kItemPropertiesEn;
}

}  // namespace ostinato
