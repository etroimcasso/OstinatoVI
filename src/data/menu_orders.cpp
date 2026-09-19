#include "data/menu_orders.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// Each esper's place in the list, in esper index order.
constexpr std::array<EsperMenuOrderEntry, kEsperMenuOrderCount> kEsperMenuOrder = {{
#include "data/generated/esper_menu_order_data.inc"
}};

// The six magic-order settings.
constexpr std::array<MagicListOrderEntry, kMagicListOrderSettings> kMagicListOrder = {{
#include "data/generated/magic_list_order_data.inc"
}};

// The imp's sixteen equipment slots.
constexpr std::array<ImpEquipmentEntry, kImpEquipmentSlots> kImpEquipment = {{
#include "data/generated/imp_equipment_data.inc"
}};

constexpr bool espersMatchPositions() {
    constexpr auto base = static_cast<std::size_t>(EsperId::RAMUH);
    for (std::size_t i = 0; i < kEsperMenuOrder.size(); ++i) {
        if (static_cast<std::size_t>(kEsperMenuOrder[i].esper) != base + i) {
            return false;
        }
    }
    return true;
}

constexpr bool settingsMatchPositions() {
    for (std::size_t i = 0; i < kMagicListOrder.size(); ++i) {
        if (kMagicListOrder[i].setting != i) {
            return false;
        }
    }
    return true;
}

constexpr bool slotsMatchPositions() {
    for (std::size_t i = 0; i < kImpEquipment.size(); ++i) {
        if (kImpEquipment[i].slot != i) {
            return false;
        }
    }
    return true;
}

static_assert(espersMatchPositions(),
              "kEsperMenuOrder esper fields must match array positions + EsperId::RAMUH");
static_assert(settingsMatchPositions(),
              "kMagicListOrder setting fields must match array positions");
static_assert(slotsMatchPositions(),
              "kImpEquipment slot fields must match array positions");

}  // namespace

std::uint8_t esperMenuPlace(EsperId esper) {
    const auto base = static_cast<std::size_t>(EsperId::RAMUH);
    const auto index = static_cast<std::size_t>(esper) - base;
    assert(static_cast<std::size_t>(esper) >= base && index < kEsperMenuOrder.size()
           && "esper id has no menu place");
    return kEsperMenuOrder[index].place;
}

std::span<const EsperMenuOrderEntry> esperMenuOrder() { return kEsperMenuOrder; }

std::span<const AttackId> magicListOrder(std::uint8_t setting) {
    assert(setting < kMagicListOrder.size() && "no such magic order setting");
    return kMagicListOrder[setting].firstSpells;
}

std::span<const MagicListOrderEntry> magicListOrders() { return kMagicListOrder; }

bool isImpEquipment(ItemId item) {
    for (std::size_t slot = 0; slot < kImpEquipmentSlotsRead; ++slot) {
        if (kImpEquipment[slot].item == item) {
            return true;
        }
    }
    return false;
}

std::span<const ImpEquipmentEntry> impEquipment() { return kImpEquipment; }

}  // namespace ostinato
