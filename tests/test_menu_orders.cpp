// Full-corpus tests for the menu ordering lists. Every entry of all three is
// compared against the generated fixture (the raw cartridge values), and each
// list's own invariant is pinned: the esper places are a permutation, the six
// magic-order settings are the six distinct band orderings, and the imp list's
// ten read slots are followed by six empty ones.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "data/menu_orders.h"
#include "ostinato/attack_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"

#include "fixtures/menu_orders_expected.h"

namespace {

using namespace ostinato;

// --- full corpus -----------------------------------------------------------------

TEST(EsperMenuOrder, EveryPlaceMatchesRom) {
    const auto table = esperMenuOrder();
    ASSERT_EQ(table.size(), kEsperMenuOrderCount);
    ASSERT_EQ(table.size(), test::kExpectedEsperMenuOrder.size());
    const auto base = static_cast<std::size_t>(EsperId::RAMUH);
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(static_cast<std::size_t>(table[i].esper), base + i) << "row " << i;
        EXPECT_EQ(table[i].place, test::kExpectedEsperMenuOrder[i]) << "row " << i;
        EXPECT_EQ(esperMenuPlace(table[i].esper), table[i].place) << "row " << i;
    }
}

TEST(MagicListOrder, EveryRowMatchesRom) {
    const auto table = magicListOrders();
    ASSERT_EQ(table.size(), kMagicListOrderSettings);
    ASSERT_EQ(table.size(), test::kExpectedMagicListOrder.size());
    for (std::size_t setting = 0; setting < table.size(); ++setting) {
        const auto& expected = test::kExpectedMagicListOrder[setting];
        EXPECT_EQ(table[setting].setting, setting);
        for (std::size_t band = 0; band < kMagicListBands; ++band) {
            EXPECT_EQ(static_cast<std::uint8_t>(table[setting].firstSpells[band]),
                      expected[band])
                << "setting " << setting << " band " << band;
        }
        // The fourth byte of each row is the terminator the table stores; it is
        // not carried as a field.
        EXPECT_EQ(expected[kMagicListBands], 0xFF) << "setting " << setting;
        const auto order = magicListOrder(static_cast<std::uint8_t>(setting));
        EXPECT_TRUE(std::equal(order.begin(), order.end(),
                               table[setting].firstSpells.begin()));
    }
}

TEST(ImpEquipment, EverySlotMatchesRom) {
    const auto table = impEquipment();
    ASSERT_EQ(table.size(), kImpEquipmentSlots);
    ASSERT_EQ(table.size(), test::kExpectedImpEquipment.size());
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        EXPECT_EQ(table[slot].slot, slot);
        EXPECT_EQ(static_cast<std::uint8_t>(table[slot].item),
                  test::kExpectedImpEquipment[slot])
            << "slot " << slot;
    }
}

// --- each list's invariant --------------------------------------------------------

// A menu ordering cannot repeat or skip a place: the 27 places are 1..27.
TEST(EsperMenuOrder, PlacesArePermutationOfOneToTwentySeven) {
    std::vector<int> places;
    for (const EsperMenuOrderEntry& entry : esperMenuOrder()) {
        places.push_back(entry.place);
    }
    std::sort(places.begin(), places.end());
    for (std::size_t i = 0; i < places.size(); ++i) {
        EXPECT_EQ(places[i], static_cast<int>(i) + 1);
    }
}

// The six settings are the six distinct orderings of the three bands — every
// row holds each band exactly once, and no two rows agree.
TEST(MagicListOrder, SettingsAreTheSixDistinctBandOrderings) {
    const std::set<AttackId> bands{AttackId::FIRE, AttackId::SCAN, AttackId::CURE};
    std::set<std::array<AttackId, kMagicListBands>> seen;
    for (const MagicListOrderEntry& entry : magicListOrders()) {
        const std::set<AttackId> row{entry.firstSpells.begin(), entry.firstSpells.end()};
        EXPECT_EQ(row, bands) << "setting " << +entry.setting;
        seen.insert(entry.firstSpells);
    }
    EXPECT_EQ(seen.size(), kMagicListOrderSettings);
}

// equip.asm:1670 reads ten slots. The six beyond them are empty.
TEST(ImpEquipment, TenNamedSlotsThenSixEmpty) {
    const auto table = impEquipment();
    for (std::size_t slot = 0; slot < kImpEquipmentSlotsRead; ++slot) {
        EXPECT_NE(table[slot].item, ItemId::EMPTY) << "slot " << slot;
    }
    for (std::size_t slot = kImpEquipmentSlotsRead; slot < table.size(); ++slot) {
        EXPECT_EQ(table[slot].item, ItemId::EMPTY) << "slot " << slot;
    }
}

// --- traced values ---------------------------------------------------------------

// skills.asm:1696-1722: Ramuh is first in the list, Kirin eighteenth, and Terrato
// — the fifth esper found — sits nineteenth.
TEST(EsperMenuOrder, TracedPlaces) {
    EXPECT_EQ(esperMenuPlace(EsperId::RAMUH), 1);
    EXPECT_EQ(esperMenuPlace(EsperId::KIRIN), 2);
    EXPECT_EQ(esperMenuPlace(EsperId::TERRATO), 19);
    EXPECT_EQ(esperMenuPlace(EsperId::RAIDEN), 27);
}

// skills.asm:767-772: setting 0 opens with white magic, setting 2 with black.
TEST(MagicListOrder, TracedSettings) {
    const auto first = magicListOrder(0);
    EXPECT_EQ(first[0], AttackId::CURE);
    EXPECT_EQ(first[1], AttackId::FIRE);
    EXPECT_EQ(first[2], AttackId::SCAN);

    const auto third = magicListOrder(2);
    EXPECT_EQ(third[0], AttackId::FIRE);
    EXPECT_EQ(third[1], AttackId::SCAN);
    EXPECT_EQ(third[2], AttackId::CURE);
}

// equip.asm:1685-1694.
TEST(ImpEquipment, NamedItemsAreEquippable) {
    EXPECT_TRUE(isImpEquipment(ItemId::CURSED_SHLD));
    EXPECT_TRUE(isImpEquipment(ItemId::IMP_HALBERD));
    EXPECT_TRUE(isImpEquipment(ItemId::ATMA_WEAPON));
    EXPECT_TRUE(isImpEquipment(ItemId::HEAL_ROD));
    EXPECT_FALSE(isImpEquipment(ItemId::EMPTY));
    EXPECT_FALSE(isImpEquipment(ItemId::EXCALIBUR));
}

}  // namespace
