// The cells and bits a battle's end reads and writes: the battle menus, the inventory's records,
// the formation's bytes, the colosseum's wager and prize, the Veldt and the field's music.
//
// The addresses are $7E:xxxx cells in namespace cells: a file that names them says
// `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstdint>

#include "battle/items_obtained.h"

#include "cells/damage/per_target.h"

namespace ostinato::trace::cells {

inline constexpr std::uint8_t kMenusClosedBit = 0x80;
inline constexpr std::uint32_t kInventoryRecords = kWorkRam + 0x2686;
inline constexpr std::uint32_t kRecordOffset = kWorkRam + 0x3010;
// The battle's: the formation (a word); the colosseum (nonzero), the wager and the prize; the battle's number (a word); the
// field's flags (bit 0 Gau may come back after the battle, bit 1 the Veldt, bit 3 the field's music
// playing on, a word) and the background (a word); the formation's four bytes; Doom Gaze's battler; the
// battles Gau can find on the Veldt, a bit each; the message variables.
inline constexpr std::uint32_t kFormationCell = kWorkRam + 0x11E0;
inline constexpr std::uint32_t kWagerCell = kWorkRam + 0x0205;
inline constexpr std::uint32_t kPrizeCell = kWorkRam + 0x0207;
// The final battle's line of fighters, in the colosseum match's cells: up to twelve record numbers,
// $FF after the last and in each place a stage has taken a fighter from (InitParty :7723-7747).
inline constexpr std::uint32_t kFinalBattleLine = kWorkRam + 0x0205;
inline constexpr std::uint32_t kFinalBattleLineSize = 12;
inline constexpr std::uint8_t kEndOfLine = 0xFF;
inline constexpr std::uint32_t kFieldFlags = kWorkRam + 0x11E4;
inline constexpr std::uint8_t kGauMayAppearBit = 0x01;
inline constexpr std::uint8_t kOnTheVeldtBit = 0x02;
inline constexpr std::uint32_t kFormationBytes = kWorkRam + 0x2F48;
inline constexpr std::uint32_t kRecordBytes = battle::kItemRecordBytes;
// The record LoadItemProp makes, from which each record it makes is copied (battle_main.asm:13964-14037);
// its targeting byte is the last record made for an item's (BattleInventory::lastRecordTargeting), which
// a record made for no item leaves in place (:13992-13994).
inline constexpr std::uint32_t kItemHandedOver = kWorkRam + 0x2E72;
inline constexpr std::uint32_t kLastRecordTargeting = kItemHandedOver + 2;
// What the targeting holds before any record for an item is made: the start's fill (InitBattle
// :6063-6067).
inline constexpr std::uint8_t kNoRecordMadeYet = 0xFF;
// The items obtained, five-byte records from $7E:602D. The opening empties the first 64 bytes, every item
// byte of the first thirteen places (btlgfx_main.asm:2472-2480, which stops short of the list's 80 bytes).
inline constexpr std::uint32_t kItemsObtainedRecords = kWorkRam + 0x602D;
inline constexpr std::uint32_t kItemsObtainedEmptiedBytes = 0x40;
inline constexpr std::uint8_t kNoItemObtained = 0xFF;
// The list's two counts: the items added, and the items merged into the inventory. Each indexes the list by
// its low four bits; the graphics program's start zeroes both (btlgfx_main.asm:43456-43460).
inline constexpr std::uint32_t kItemsAdded = kWorkRam + 0x64DA;
inline constexpr std::uint32_t kItemsMerged = kWorkRam + 0x64DB;
inline constexpr std::uint8_t kFieldMusicPlaysOnBit = 0x08;

}  // namespace ostinato::trace::cells
