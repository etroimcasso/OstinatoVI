#include "battle/items_obtained.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include "battle/battle_state.h"
#include "data/battle_tables.h"
#include "data/item_properties.h"

namespace ostinato::battle {

namespace {

// The counts index the list by their low four bits.
constexpr std::uint8_t kPlaceMask = kItemsObtainedPlaces - 1;

// A sum that reaches this is held to kMostOfOneItem.
constexpr unsigned kOverTheMost = kMostOfOneItem + 1;

// The record's flag bits.
constexpr std::uint8_t kNotUsableInBattle = 0x80;
constexpr std::uint8_t kToolFlag = 0x40;
constexpr std::uint8_t kThrowableFlag = 0x20;
constexpr std::uint8_t kWeaponFlag = 0x10;
constexpr std::uint8_t kShieldFlag = 0x08;
// A hand whose weapon broke has every flag bit set (AttackerEffect_0e battle_main.asm:10210-10218).
constexpr std::uint8_t kBrokenHandFlags = 0xFF;
// The record's fifth byte: the party slots that cannot hold the item; every bit set for an item no hand
// check is made for.
constexpr std::size_t kRecordHandsByte = 4;
constexpr std::uint8_t kPartySlotBits = 0x0F;
constexpr std::uint8_t kNoHandCheck = 0xFF;

// LEGACY VERBATIM (docs/Bugs.md): where the last three places sit in the decoded title program —
// the list's fourteenth place ($7E:602D + 13 x 5) less where the program is laid ($7E:5000). They
// are the second frame of the opening's Magitek armor animation.
constexpr std::size_t kItemsObtainedTailInTitleProgram = 0x106E;

ItemRecord& placeAt(ItemsObtained& list, std::uint8_t count) {
    return list.entries[count & kPlaceMask];
}

}  // namespace

ItemsObtained itemsObtainedAtBattleStart(const ItemsObtainedTail& tail) {
    ItemsObtained list;
    // LEGACY VERBATIM (docs/Bugs.md): a battle's start empties the first thirteen places only, so
    // whatever the last three hold follows the battle's thirteenth item into the inventory.
    std::copy(tail.begin(), tail.end(), list.entries.begin() + kItemsObtainedEmptiedPlaces);
    return list;
}

ItemsObtainedTail itemsObtainedTail(const ItemsObtained& list) {
    ItemsObtainedTail tail;
    std::copy(list.entries.begin() + kItemsObtainedEmptiedPlaces, list.entries.end(), tail.begin());
    return tail;
}

void obtainItem(BattleState& state, ItemId item, std::uint8_t quantity) {
    ItemsObtained& list = state.itemsObtained;
    placeAt(list, list.added) = itemRecordFor(state, item, quantity);
    ++list.added;
}

void mergeItemsObtained(BattleState& state) {
    ItemsObtained& list = state.itemsObtained;
    auto& slots = state.inventory.items;
    for (;;) {
        ItemRecord& place = placeAt(list, list.merged);
        const ItemId item = itemIn(place);
        if (item == ItemId::EMPTY) {
            return;
        }
        const auto held = std::find_if(slots.begin(), slots.end(),
                                       [&](const BattleItem& slot) { return slot.item == item; });
        if (held != slots.end()) {
            // The sum keeps its low byte before it is held to 99.
            const auto sum = static_cast<std::uint8_t>(held->quantity + quantityIn(place));
            held->quantity = sum >= kOverTheMost ? kMostOfOneItem : sum;
        } else {
            const auto empty = std::find_if(slots.begin(), slots.end(),
                                            [](const BattleItem& slot) { return slot.item == ItemId::EMPTY; });
            if (empty == slots.end()) {
                // LEGACY VERBATIM (docs/Bugs.md): a full inventory stops the merge at this place
                // for good; it and every item after it wait.
                return;
            }
            *empty = battleItemFromRecord(place);
        }
        place[kRecordItemByte] = static_cast<std::uint8_t>(ItemId::EMPTY);
        ++list.merged;
    }
}

BattleItem battleItemFromRecord(std::span<const std::uint8_t, kItemRecordBytes> record) {
    const std::uint8_t flags = record[1];
    return BattleItem{
        .item = static_cast<ItemId>(record[0]),
        .usableInBattle = (flags & kNotUsableInBattle) == 0,
        .throwable = (flags & kThrowableFlag) != 0,
        .isTool = (flags & kToolFlag) != 0,
        .isWeapon = (flags & kWeaponFlag) != 0,
        .isShield = (flags & kShieldFlag) != 0,
        .targeting = Targeting{record[2]},
        .quantity = record[3],
        .equippableBy = BattleSlotMask{static_cast<std::uint8_t>(~record[4] & kPartySlotBits)},
    };
}

ItemRecord recordOf(const BattleItem& item) {
    std::uint8_t flags = item.usableInBattle ? 0 : kNotUsableInBattle;
    flags |= item.throwable ? kThrowableFlag : 0;
    flags |= item.isTool ? kToolFlag : 0;
    flags |= item.isWeapon ? kWeaponFlag : 0;
    flags |= item.isShield ? kShieldFlag : 0;
    if (item.brokenFrom) {
        flags = kBrokenHandFlags;
    }
    return ItemRecord{static_cast<std::uint8_t>(item.item), flags, item.targeting.bits, item.quantity,
                      static_cast<std::uint8_t>(~item.equippableBy.bits & kPartySlotBits)};
}

ItemRecord itemRecordFor(const BattleState& state, ItemId item, std::uint8_t quantity) {
    ItemRecord record = recordOf(battleItem(state, item, quantity));
    if (item != ItemId::EMPTY && itemTypeBattleFlags(getItemProperties(item).typeAndUsage.type()).skipsEquippableCheck()) {
        record[kRecordHandsByte] = kNoHandCheck;
    }
    return record;
}

ItemsObtainedTail itemsObtainedTailAtPowerOn(std::span<const std::uint8_t> titleProgram) {
    constexpr std::size_t kTailBytes = kItemsObtainedTailPlaces * kItemRecordBytes;
    if (titleProgram.size() < kItemsObtainedTailInTitleProgram + kTailBytes) {
        throw std::out_of_range("The title program is too short to hold the items-obtained tail.");
    }
    ItemsObtainedTail tail;
    const std::span<const std::uint8_t> bytes = titleProgram.subspan(kItemsObtainedTailInTitleProgram, kTailBytes);
    for (std::size_t i = 0; i < tail.size(); ++i) {
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(i * kItemRecordBytes), kItemRecordBytes, tail[i].begin());
    }
    return tail;
}

}  // namespace ostinato::battle
