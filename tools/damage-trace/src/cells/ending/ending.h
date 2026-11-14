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
// field's flags (bit 1 the Veldt, bit 3 the field's music playing on, a word) and the background (a
// word); the formation's four bytes; Doom Gaze's battler; the battles Gau can find on the Veldt, a
// bit each; the message variables.
inline constexpr std::uint32_t kFormationCell = kWorkRam + 0x11E0;
inline constexpr std::uint32_t kWagerCell = kWorkRam + 0x0205;
inline constexpr std::uint32_t kPrizeCell = kWorkRam + 0x0207;
inline constexpr std::uint8_t kOnTheVeldtBit = 0x02;
inline constexpr std::uint32_t kFormationBytes = kWorkRam + 0x2F48;
inline constexpr std::uint32_t kRecordBytes = battle::kItemRecordBytes;
inline constexpr std::uint8_t kFieldMusicPlaysOnBit = 0x08;

}  // namespace ostinato::trace::cells
