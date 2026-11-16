// The work-RAM cells and bits the status routines read and write: the statuses each battler can be
// given, its triggers, the equipment to work out again, the monsters standing and fallen, and the
// bits of each battler's flags.
//
// The addresses are $7E:xxxx cells in namespace cells: a file that names them says
// `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstdint>

#include "cells/damage/per_target.h"

namespace ostinato::trace::cells {

// Per battler, at twice the battler's index: the statuses it can be given, its flags, its triggers,
// its four status timers and the statuses it is to have for a counterattack. A party member's
// equipment flag is one byte per party slot.
inline constexpr std::uint32_t kInflictable12 = kWorkRam + 0x331C;
inline constexpr std::uint32_t kInflictable34 = kWorkRam + 0x3330;
inline constexpr std::uint32_t kTriggers = kWorkRam + 0x3E4C;      // the first byte: bit 0 Retort
inline constexpr std::uint32_t kMoreTriggers = kWorkRam + 0x3E4D;  // bit 1 Overcast, bit 6 Phantasm
inline constexpr std::uint32_t kEquipmentChanged = kWorkRam + 0x2F30;
// The battle's own: the monsters standing and fallen (a byte of monster bits each), the battlers that
// died this action, the party members whose statuses changed, and the effect's misses.
inline constexpr std::uint32_t kMonstersStanding = kWorkRam + 0x2F2F;
inline constexpr std::uint32_t kMonstersFallen = kWorkRam + 0x3A3A;
inline constexpr std::uint32_t kMissedByEffect = kWorkRam + 0x3A48;

inline constexpr std::uint8_t kPresentBit = 0x01;
inline constexpr std::uint8_t kMenuMayOpenBit = 0x80;
inline constexpr std::uint8_t kEmptyGaugeBit = 0x40;
// The first triggers byte ($3E4C,x): 0 Retort, 1 a monster's own Runic and 7 its piranha mark
// (LoadMonsterProp :7412-7415), 2 a party member's Runic (:4078-4080), 3 the regen, Seize or Phantasm
// timer fired and 4 the poison timer fired (:14970-14983).
inline constexpr std::uint8_t kRetortBit = 0x01;
inline constexpr std::uint8_t kMonsterRunicBit = 0x02;
inline constexpr std::uint8_t kRunicBit = 0x04;
inline constexpr std::uint8_t kRegenDueBit = 0x08;
inline constexpr std::uint8_t kPoisonDueBit = 0x10;
inline constexpr std::uint8_t kPiranhaBit = 0x80;
inline constexpr std::uint8_t kOvercastBit = 0x02;
inline constexpr std::uint8_t kPhantasmBit = 0x40;

}  // namespace ostinato::trace::cells
