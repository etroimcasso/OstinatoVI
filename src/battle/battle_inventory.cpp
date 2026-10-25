#include "battle/battle_inventory.h"

#include "battle/battle_state.h"
#include "data/battle_tables.h"
#include "data/item_properties.h"

namespace ostinato::battle {

namespace {

// The bits an item's type adds to its battle flags (ItemTypeBattleFlags::mergedBits()), named for
// the kind of item that sets each.
constexpr std::uint8_t kToolBit = 0x40;
constexpr std::uint8_t kWeaponBit = 0x10;
constexpr std::uint8_t kShieldBit = 0x08;

// The last rage the battle's Rage menu can list: the menu takes rages 0-254.
constexpr std::size_t kListedRages = 255;

}  // namespace

BattleItem battleItem(const BattleState& state, ItemId item, std::uint8_t quantity) {
    BattleItem entry{.item = item, .quantity = quantity};
    if (item == ItemId::EMPTY) {
        return entry;
    }
    const ItemProperties& properties = getItemProperties(item);
    const ItemType type = properties.typeAndUsage.type();
    const ItemTypeBattleFlags typeFlags = itemTypeBattleFlags(type);
    const std::uint8_t merged = typeFlags.mergedBits();

    entry.targeting = properties.targeting;
    entry.usableInBattle = properties.typeAndUsage.usage().has(ItemUsage::BATTLE);
    entry.throwable = properties.typeAndUsage.usage().has(ItemUsage::THROW);
    entry.isTool = (merged & kToolBit) != 0;
    entry.isWeapon = (merged & kWeaponBit) != 0;
    entry.isShield = (merged & kShieldBit) != 0;

    if (!typeFlags.skipsEquippableCheck()) {
        for (int position = 0; position < kPartySlots; ++position) {
            const EquipPermissions& wearer = state.at(BattlerSlot::character(position)).equipsAs;
            if ((properties.equippableBy.bits() & wearer.bits()) != 0) {
                entry.equippableBy.bits = static_cast<std::uint8_t>(entry.equippableBy.bits | (1u << position));
            }
        }
    }
    return entry;
}

void loadBattleInventory(BattleState& state) {
    BattleInventory& inventory = state.inventory;
    for (int position = 0; position < kPartySlots; ++position) {
        const Battler& battler = state.at(BattlerSlot::character(position));
        if (!battler.record) {
            inventory.weaponHands[position] = BattleItem{};
            inventory.shieldHands[position] = BattleItem{};
            continue;
        }
        const game::CharacterRecord& record = state.party.characters[*battler.record];
        inventory.weaponHands[position] = battleItem(state, record.weapon, 1);
        inventory.shieldHands[position] = battleItem(state, record.shield, 1);
    }
    inventory.tools = ToolSet{};
    for (std::size_t slot = 0; slot < inventory.items.size(); ++slot) {
        const game::InventorySlot& held = state.party.inventory[slot];
        inventory.items[slot] = battleItem(state, held.item, held.quantity);
        inventory.tools.add(held.item);
    }
}

void loadKnownSkills(BattleState& state) {
    const game::Party& party = state.party;
    state.swdtechsKnown = party.swdtechsKnown.count();
    state.blitzesKnown = party.blitzesKnown.count();
    for (std::size_t dance = 0; dance < state.dancesKnown.size(); ++dance) {
        state.dancesKnown[dance] =
            party.dancesKnown.test(dance) ? std::optional<DanceId>(static_cast<DanceId>(dance)) : std::nullopt;
    }
    state.ragesKnown.clear();
    for (std::size_t rage = 0; rage < kListedRages; ++rage) {
        if (party.ragesKnown.test(rage)) {
            state.ragesKnown.push_back(static_cast<MonsterId>(rage));
        }
    }
}

}  // namespace ostinato::battle
