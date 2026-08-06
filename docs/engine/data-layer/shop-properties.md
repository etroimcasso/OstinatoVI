# Shop properties

## Public surface

```cpp
#include "data/shop_properties.h"

const ostinato::ShopProperties& shop = ostinato::getShopProperties(4);
shop.config.type();             // kShopTypeItem
shop.config.priceAdjustment();  // kPriceAdjustEdgarMinus50
shop.items[0];                  // ItemId::TONIC

for (const auto& entry : ostinato::shopProperties()) {
    // entry.shopIndex — 0..127
    // entry.record    — ShopProperties
}
```

## The record

```cpp
struct ShopConfig {
    std::uint8_t packed;
    constexpr std::uint8_t type() const;             // bits 0-2
    constexpr std::uint8_t priceAdjustment() const;  // bits 3-5
    static constexpr ShopConfig of(std::uint8_t type,
                                   std::uint8_t priceAdjustment);
};

struct ShopProperties {
    ShopConfig config;              // +0
    std::array<ItemId, 8> items;    // +1..8
};
static_assert(sizeof(ShopProperties) == 9);
```

One 9-byte record per shop, byte-identical to the ROM's `shop_prop` record: a
packed config byte plus eight item slots. Unused trailing slots hold
`ItemId::EMPTY`.

The config codes are plain `uint8_t` values with named constants (the ROM has
no symbol set for them; the type names are the shop menu's own display
strings):

- **Shop types** — `kShopTypeWeapon` (1), `kShopTypeArmor` (2),
  `kShopTypeItem` (3), `kShopTypeRelics` (4), `kShopTypeVendor` (5), and
  `kShopTypeUnused` (0, only on the empty records).
- **Price adjustments** — `kPriceAdjustNone`, `kPriceAdjustPlus50`,
  `kPriceAdjustPlus100`, `kPriceAdjustMinus50`, `kPriceAdjustFemaleMinus50`
  (−50% when the character fronting the menu is female, +50% male),
  `kPriceAdjustMaleMinus50` (the inverse), and `kPriceAdjustEdgarMinus50`
  (−50% when Edgar fronts the menu — the Figaro Castle shops). The shipped
  data uses only `None` and `EdgarMinus50`; the other behaviors exist in the
  game's dispatch and are available to custom rows.

## The table and the index space

```cpp
struct ShopEntry { std::uint8_t shopIndex; ShopProperties record; };

const ShopProperties& getShopProperties(std::uint8_t shopIndex);  // asserts < 128
std::span<const ShopEntry> shopProperties();                      // all 128
```

Shops are addressed by plain table position — the index event scripts pass to
the shop menu. The game defines 87 shops (0-86); records 87-127 are unused
fill (config 0, all slots empty) and free for new shops. `getShopProperties`
asserts the index bound in debug builds.

## Backing data / where to change

Rows live in `src/data/generated/shop_prop_data.inc` (included into the array
in `src/data/shop_properties.cpp`). To change a shop's inventory, edit its
row's `.items` list (up to eight `ItemId`s, pad the tail with
`ItemId::EMPTY`); to change its pricing or name, rebuild its `.config` with
`ShopConfig::of(type, priceAdjustment)`. A compile-time assert verifies every
row's `.shopIndex` matches its array position. A deliberate change must also
update the matching row in `tests/fixtures/shop_prop_expected.h` (original
ROM values).

## What's tested

`tests/test_shop_properties.cpp` — every one of the 128 records
`memcmp`-compared in full against the fixture; semantic spot-checks
hand-traced from the ROM bytes (the Narshe weapon shop, the Figaro Castle
item shop with Edgar's discount, the lone Vendor, an unused record); and the
`ShopConfig` builder round-trip.
