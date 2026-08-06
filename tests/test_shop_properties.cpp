// Full-corpus test of the shop-properties table. The byte-equivalence test
// asserts EVERY one of the 128 packed records is byte-identical to the ROM's
// 9-byte record (no subset) and that every entry's identity field matches its
// position; the semantic tests exercise the lookup and the config builder the
// emitted rows depend on.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/shop_properties.h"

#include "ostinato/item_id.h"

#include "fixtures/shop_prop_expected.h"

namespace {

// Full corpus: identity fields on both sides match the position, and one
// memcmp per packed record catches field-order, padding, decomposition, and
// builder drift in one whole-record comparison against the ROM.
TEST(ShopProperties, AllRecordsAreByteIdenticalToRom) {
    const auto table = ostinato::shopProperties();
    ASSERT_EQ(table.size(), ostinato::test::kExpectedShopEntries.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = ostinato::test::kExpectedShopEntries[i];
        EXPECT_EQ(expected.shopIndex, i) << "fixture entry " << i;
        EXPECT_EQ(table[i].shopIndex, i) << "table entry " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected.record, 9), 0)
            << "shop index " << i;
    }
}

// The lookup indexes by shop index. Spot-check semantic surfaces hand-traced
// from the ROM records: shop 0 (the Narshe weapon shop, config $01), shop 4
// (the Figaro Castle item shop, config $33 — Edgar's -50%), shop 39 (the
// $05 Vendor), and shop 127 (an unused record: config $00, all slots empty).
TEST(ShopProperties, LookupSemanticSurface) {
    using ostinato::ItemId;

    const auto& weaponShop = ostinato::getShopProperties(0);
    EXPECT_EQ(weaponShop.config.type(), ostinato::kShopTypeWeapon);
    EXPECT_EQ(weaponShop.config.priceAdjustment(), ostinato::kPriceAdjustNone);
    EXPECT_EQ(weaponShop.items[0], ItemId::REGAL_CUTLASS);
    EXPECT_EQ(weaponShop.items[6], ItemId::FULL_MOON);
    EXPECT_EQ(weaponShop.items[7], ItemId::EMPTY);

    const auto& figaroItemShop = ostinato::getShopProperties(4);
    EXPECT_EQ(figaroItemShop.config.type(), ostinato::kShopTypeItem);
    EXPECT_EQ(figaroItemShop.config.priceAdjustment(),
              ostinato::kPriceAdjustEdgarMinus50);
    EXPECT_EQ(figaroItemShop.config.packed, 0x33u);
    EXPECT_EQ(figaroItemShop.items[0], ItemId::TONIC);
    EXPECT_EQ(figaroItemShop.items[7], ItemId::TENT);

    const auto& vendor = ostinato::getShopProperties(39);
    EXPECT_EQ(vendor.config.type(), ostinato::kShopTypeVendor);
    EXPECT_EQ(vendor.items[4], ItemId::SHURIKEN);

    const auto& unused = ostinato::getShopProperties(127);
    EXPECT_EQ(unused.config.type(), ostinato::kShopTypeUnused);
    EXPECT_EQ(unused.config.packed, 0x00u);
    for (const auto item : unused.items) {
        EXPECT_EQ(item, ItemId::EMPTY);
    }
}

// Builder round-trip: ShopConfig::of re-packs to the exact ROM config byte,
// and the two accessors recover its codes.
TEST(ShopProperties, ConfigBuilderRoundTrip) {
    using ostinato::ShopConfig;

    EXPECT_EQ(ShopConfig::of(ostinato::kShopTypeItem,
                             ostinato::kPriceAdjustEdgarMinus50).packed,
              0x33u);
    EXPECT_EQ(ShopConfig::of(ostinato::kShopTypeVendor,
                             ostinato::kPriceAdjustNone).packed,
              0x05u);
    EXPECT_EQ(ShopConfig{}.packed, 0x00u);

    constexpr ShopConfig roundTrip{0x33};
    EXPECT_EQ(roundTrip.type(), ostinato::kShopTypeItem);
    EXPECT_EQ(roundTrip.priceAdjustment(), ostinato::kPriceAdjustEdgarMinus50);
}

}  // namespace
