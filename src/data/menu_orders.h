// The orderings the menus list things in: where each esper sits in the esper
// list, how the magic list groups spells under each of the six magic-order
// settings, and which items an imp can equip. The rows are generated
// (src/data/generated/esper_menu_order_data.inc, magic_list_order_data.inc,
// imp_equipment_data.inc); this header owns the entry types and the accessors.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/attack_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"

namespace ostinato {

// How many espers the list orders, how many magic-order settings there are, how
// many bands the magic list is built from, and how many equipment slots the imp
// list holds.
inline constexpr std::size_t kEsperMenuOrderCount = 27;
inline constexpr std::size_t kMagicListOrderSettings = 6;
inline constexpr std::size_t kMagicListBands = 3;
inline constexpr std::size_t kImpEquipmentSlots = 16;

// How many of those slots the game reads (equip.asm:1670). The rest are empty.
inline constexpr std::size_t kImpEquipmentSlotsRead = 10;

// Where one esper sits in the menu's esper list, counting from 1.
struct EsperMenuOrderEntry {
    EsperId esper;
    std::uint8_t place;
};

// One magic-order setting: the first spell of each band, in the order that
// setting lists them. The bands are black magic, effect magic and white magic;
// which is which follows from the spell each entry names.
struct MagicListOrderEntry {
    std::uint8_t setting;
    std::array<AttackId, kMagicListBands> firstSpells;
};

// One slot of the imp equipment list.
struct ImpEquipmentEntry {
    std::uint8_t slot;
    ItemId item;
};

// Where `esper` sits in the esper list, counting from 1. PRECONDITION
// (asserted): `esper` is one of the 27 espers.
std::uint8_t esperMenuPlace(EsperId esper);

// All 27 espers in esper index order.
std::span<const EsperMenuOrderEntry> esperMenuOrder();

// The band-opening spells for one setting, in list order. PRECONDITION
// (asserted): `setting` is below 6 — the config menu clamps the player's choice
// to 0..5 (config.asm:1108).
std::span<const AttackId> magicListOrder(std::uint8_t setting);

// All six settings in order.
std::span<const MagicListOrderEntry> magicListOrders();

// Whether an imp can equip this item.
bool isImpEquipment(ItemId item);

// All sixteen slots, the last six empty.
std::span<const ImpEquipmentEntry> impEquipment();

}  // namespace ostinato
