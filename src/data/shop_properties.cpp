#include "data/shop_properties.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 128-entry table. The generated rows carry the ROM record data;
// designated initializers at every row keep each field self-labeling, and
// each entry carries its identity as its shop index.
constexpr std::array<ShopEntry, 128> kShopProperties = {{
#include "data/generated/shop_prop_data.inc"
}};

// Self-consistency of the emitted rows: every entry's shopIndex field must
// equal its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kShopProperties.size(); ++i) {
        if (kShopProperties[i].shopIndex != i) {
            return false;
        }
    }
    return true;
}(), "kShopProperties entry shopIndex fields must match array positions");

}  // namespace

const ShopProperties& getShopProperties(std::uint8_t shopIndex) {
    assert(shopIndex < kShopProperties.size() &&
           "shop index out of range (0..127)");
    return kShopProperties[shopIndex].record;
}

std::span<const ShopEntry> shopProperties() {
    return kShopProperties;
}

}  // namespace ostinato
