// The items the party comes away with from a battle, on their way into its inventory.
//
// An item a party member comes away holding, a drop, the colosseum's prize — each joins a list of
// sixteen places (obtainItem()), and reaches the inventory only when the list is merged
// (mergeItemsObtained()): at the battle's end, after the prize and after each drop, and when a party
// member's battle menu opens. The list is a ring: the next item goes in place `added % 16` and the
// merge starts at place `merged % 16`, and neither count is brought back down within the battle.
//
//     battle::obtainItem(state, ItemId::POTION, 1);   // waits in the list
//     battle::mergeItemsObtained(state);             // now in state.inventory
//
// The merge takes the places in order from where it last stopped and ends at the first empty one.
// An item the party holds has the place's quantity added to its own, held to 99. An item it does
// not hold goes whole into the first empty inventory slot, quantity and all. With no empty slot the
// merge stops there and goes no further, in this merge or any later one: the item waits, and every
// item after it waits behind it.
//
// Each battle starts with both counts at 0 and the first thirteen places empty. The last three
// places are not emptied: they come in on BattleStart::itemsObtainedTail and go back out on
// BattleResult::itemsObtainedTail, and whatever they hold follows a battle's thirteenth item into
// the inventory. A console just switched on holds itemsObtainedTailAtPowerOn() there. A later stage
// of the final battle keeps the whole list.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_rewards.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "battle/battle_inventory.h"
#include "ostinato/item_id.h"

namespace ostinato::battle {

struct BattleState;

// The places in the list.
inline constexpr std::size_t kItemsObtainedPlaces = 16;
// The places a battle's start leaves as they were: the last three.
inline constexpr std::size_t kItemsObtainedTailPlaces = 3;
// The places a battle's start empties: the first thirteen.
inline constexpr std::size_t kItemsObtainedEmptiedPlaces = kItemsObtainedPlaces - kItemsObtainedTailPlaces;

// The most of one item the party holds once a merge has added to it.
inline constexpr std::uint8_t kMostOfOneItem = 99;

struct ItemsObtained {
    // Each place holds one item and how many; a place whose item is ItemId::EMPTY is empty, and
    // nothing else in it is read.
    std::array<BattleItem, kItemsObtainedPlaces> entries{};
    std::uint8_t added = 0;   // items added this battle, counting on past 255 from 0
    std::uint8_t merged = 0;  // places merged this battle, counting on the same way
};

// The last three places, as a battle takes them in and hands them back.
using ItemsObtainedTail = std::array<BattleItem, kItemsObtainedTailPlaces>;

// The list as a battle starts: both counts at 0, the first thirteen places empty, and `tail` in the
// last three.
[[nodiscard]] ItemsObtained itemsObtainedAtBattleStart(const ItemsObtainedTail& tail);

// The last three places of `list`, whatever they hold.
[[nodiscard]] ItemsObtainedTail itemsObtainedTail(const ItemsObtained& list);

// `quantity` of `item` joins the list, as the battle sees it for the party in `state` (battleItem()),
// in place `added % 16`, over whatever was there.
void obtainItem(BattleState& state, ItemId item, std::uint8_t quantity);

// Merge the list into state.inventory, from place `merged % 16` on, until an empty place or a full
// inventory stops it.
void mergeItemsObtained(BattleState& state);

// --- the cartridge's form of a place ----------------------------------------------------------

// A place, an inventory slot and the item a battle works out are each five bytes on the cartridge:
// the item; its flags (0x80 not usable in battle, 0x40 a tool, 0x20 throwable, 0x10 a weapon, 0x08 a
// shield); its targeting; its quantity; and one bit per party slot, set where that party member
// cannot hold it in a hand.
inline constexpr std::size_t kItemRecordBytes = 5;
using ItemRecord = std::array<std::uint8_t, kItemRecordBytes>;

[[nodiscard]] BattleItem battleItemFromRecord(std::span<const std::uint8_t, kItemRecordBytes> record);
// The record for `item`. The fifth byte's high four bits, which nothing here reads, are 0.
[[nodiscard]] ItemRecord recordOf(const BattleItem& item);

// What the last three places hold on a console just switched on: three records out of
// `titleProgram`, the cartridge's title and cutscene program decoded (RomAsset::CUTSCENE through the
// cartridge's decompressor), which is laid over the list's memory at power-on. They read as 108
// Thief Knives, 112 Silk Robes and 136 Excaliburs.
//
// Throws std::out_of_range when `titleProgram` is too short to hold them.
[[nodiscard]] ItemsObtainedTail itemsObtainedTailAtPowerOn(std::span<const std::uint8_t> titleProgram);

}  // namespace ostinato::battle
