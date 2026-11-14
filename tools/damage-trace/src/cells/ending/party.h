// The cells a won battle's rewards read and write besides the records: the win's flags, the message
// variables, the party bytes and slots, the Morph supply, the SwdTechs, Blitzes, rages and dances
// known, the gil and the monster slots.
//
// The addresses are $7E:xxxx cells in namespace cells: a file that names them says
// `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cells/damage/per_target.h"

namespace ostinato::trace::cells {

// The win's flags, a bit each.
inline constexpr std::uint8_t kCurseDispelledBit = 0x01;
inline constexpr std::uint8_t kMagicPointsEarnedBit = 0x08;
inline constexpr std::uint8_t kCatHoodCountedBit = 0x10;
inline constexpr std::uint8_t kTechniqueToldBit = 0x40;
inline constexpr std::uint8_t kBlitzToldBit = 0x80;
inline constexpr std::uint32_t kMessageVariableSize = 3;
// The party's cells beyond the records and the spells known: each record's party byte, the Morph
// supply, the SwdTechs and Blitzes known, the rages known (32 bytes from here) and the dances known.
inline constexpr std::uint32_t kRecordPartyBytes = kWorkRam + 0x1850;
inline constexpr std::uint32_t kMorphSupply = kWorkRam + 0x1CF6;
inline constexpr std::uint32_t kSwdtechsKnown = kWorkRam + 0x1CF7;
inline constexpr std::uint32_t kBlitzesKnown = kWorkRam + 0x1D28;
inline constexpr std::uint32_t kRagesKnown = kWorkRam + 0x1D2C;
inline constexpr std::uint32_t kDancesKnown = kWorkRam + 0x1D4C;
inline constexpr std::uint32_t kGilSize = 3;
// A record's party byte: the party in bits 0-2, the slot in bits 3-4, the back row in bit 5, enabled
// in bit 6 and visible in bit 7.
inline constexpr std::uint8_t kPartyNumberMask = 0x07;
inline constexpr int kPartySlotPlace = 3;
inline constexpr std::uint8_t kPartySlotMask = 0x03;
inline constexpr std::uint8_t kPartyBackRowBit = 0x20;
inline constexpr std::uint8_t kPartyEnabledBit = 0x40;
inline constexpr std::uint8_t kPartyVisibleBit = 0x80;
// The battle switches' twenty variables.
inline constexpr std::uint32_t kSwitchVariables = kWorkRam + 0x3EB4;
inline constexpr std::uint16_t kEmptyMonsterSlot = 0xFFFF;

inline constexpr std::size_t kItemsBelowEmpty = 255;
inline constexpr std::size_t kActors = 16;
inline constexpr std::size_t kMonsterIds = 384;

}  // namespace ostinato::trace::cells
