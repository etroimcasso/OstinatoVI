// Shop properties: the 128-record shop_prop table — one 9-byte record per
// shop: a packed config byte plus eight item slots. The row data is generated
// (src/data/generated/shop_prop_data.inc); this header owns the record type,
// its packed config wrapper, the entry type, and the accessors.
//
// The table is version-invariant upstream (a plain .incbin,
// src/menu/shop.asm:2305-2310; ROM C4/7AC0). The shop menu indexes it with a
// x9 hardware multiply (shop.asm:1794-1801) and reads the config byte twice —
// as the shop type (shop.asm:1802) and as the price adjustment (shop.asm:900)
// — then the eight item-id bytes (shop.asm:819).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/item_id.h"

namespace ostinato {

// Shop-type codes: config bits 0-2, drawn through the shop-name text table
// (shop.asm:1802-1812 masks $07 and indexes ShopTypeTextTbl; the names below
// are the table's own EN display strings, SHOP_TYPE_1..5 in
// src/menu/menu_text_en.inc:282-286). Code 0 has no text-table entry and
// appears only on the 41 empty records (all eight slots empty).
inline constexpr std::uint8_t kShopTypeUnused = 0;
inline constexpr std::uint8_t kShopTypeWeapon = 1;
inline constexpr std::uint8_t kShopTypeArmor  = 2;
inline constexpr std::uint8_t kShopTypeItem   = 3;
inline constexpr std::uint8_t kShopTypeRelics = 4;
inline constexpr std::uint8_t kShopTypeVendor = 5;

// Price-adjustment codes: config bits 3-5, dispatched through the 7-entry
// jump table in AdjustShopPrice (shop.asm:900-923; behaviors documented at
// shop.asm:908-915). "Showing character" is the party member fronting the
// shop menu.
inline constexpr std::uint8_t kPriceAdjustNone          = 0;
inline constexpr std::uint8_t kPriceAdjustPlus50        = 1;
inline constexpr std::uint8_t kPriceAdjustPlus100       = 2;
inline constexpr std::uint8_t kPriceAdjustMinus50       = 3;
// -50% when the showing character is female (Terra, Celes, or Relm), +50%
// when male.
inline constexpr std::uint8_t kPriceAdjustFemaleMinus50 = 4;
// The inverse: -50% male showing character, +50% female.
inline constexpr std::uint8_t kPriceAdjustMaleMinus50   = 5;
// -50% when Edgar is the showing character (the Figaro Castle shops).
inline constexpr std::uint8_t kPriceAdjustEdgarMinus50  = 6;

// Record byte +0: the shop's type in the low 3 bits packed with its
// price-adjustment code in bits 3-5. Bits 6-7 are clear across the corpus
// and read by no consumer.
struct ShopConfig {
    std::uint8_t packed = 0;

    constexpr std::uint8_t type() const { return packed & 0x07; }

    constexpr std::uint8_t priceAdjustment() const {
        return (packed & 0x38) >> 3;
    }

    // Builder from the two codes:
    // ShopConfig::of(kShopTypeItem, kPriceAdjustEdgarMinus50).
    static constexpr ShopConfig of(std::uint8_t type,
                                   std::uint8_t priceAdjustment) {
        return ShopConfig{static_cast<std::uint8_t>(
            (type & 0x07) | ((priceAdjustment & 0x07) << 3))};
    }
};

static_assert(sizeof(ShopConfig) == 1,
              "ShopConfig must be byte-identical to the ROM shop config byte");

// One 9-byte shop record. Member order and widths mirror the ROM record
// exactly — pinned by the static_asserts below and the full-corpus
// byte-equivalence test. Unused trailing slots hold ItemId::EMPTY ($FF —
// the buy menu stops drawing at it, shop.asm:822).
struct ShopProperties {
    ShopConfig config;              // +0    shop.asm:1802 / :900
    std::array<ItemId, 8> items;    // +1..8 shop.asm:819
};

static_assert(sizeof(ShopProperties) == 9,
              "ShopProperties must be byte-identical to a 9-byte shop_prop record");
static_assert(offsetof(ShopProperties, config) == 0);
static_assert(offsetof(ShopProperties, items) == 1);

// One table entry: the record's identity as a typed field alongside the
// packed record. Shops have no upstream index enum — the identity is the
// plain table position (decimal 0..127); a compile-time assert verifies
// shopIndex == array position for every entry.
struct ShopEntry {
    std::uint8_t shopIndex;
    ShopProperties record;
};

// The record for a shop. Asserts shopIndex < 128 in debug builds; the ROM
// event scripts only ever pass valid indices.
const ShopProperties& getShopProperties(std::uint8_t shopIndex);

// The full 128-entry table (table order), for iteration and full-corpus
// tests.
std::span<const ShopEntry> shopProperties();

}  // namespace ostinato
