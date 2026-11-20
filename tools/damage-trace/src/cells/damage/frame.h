// The work-RAM cells the attack frame reads beyond the damage routines' own: the attack queued, the
// battlers' facing, the lores known and learned, the special attack, the monster named and the
// hands' items.
//
// The addresses are $7E:xxxx cells in namespace cells: a file that names them says
// `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstdint>

#include "cells/damage/per_target.h"

namespace ostinato::trace::cells {

inline constexpr std::uint32_t kQueuedAttack = kWorkRam + 0x3A7D;
inline constexpr std::uint32_t kFacingRight = kWorkRam + 0x2F50;
inline constexpr std::uint32_t kLoresKnown = kWorkRam + 0x1D29;
inline constexpr std::uint32_t kLoresLearned = kWorkRam + 0x3A84;
// Per battler, at twice the battler's index: its special attack and that attack's animation, the
// monster its special attack is named for (a word), its vigor and its magic power.
inline constexpr std::uint32_t kSpecialAttack = kWorkRam + 0x322D;
inline constexpr std::uint32_t kNamedMonster = kWorkRam + 0x33A8;
// The monster named while a battler is no monster and rages as none: the start's fill, both bytes
// (InitBattle :6061-6067). A party member's special attack is then named for it (:8742-8748).
inline constexpr std::uint16_t kNoMonsterNamed = 0xFFFF;
// Per battler, at twice its index: its weapon hand, then its shield hand — the item (a monster's
// attack graphic), the hit rate, the element, the weapon's flags, the spell it casts and its block
// and weapon-effect byte. A Fight loads the hand it swings with from these (_c2299f).
inline constexpr std::uint32_t kHandItems = kWorkRam + 0x3CA8;

}  // namespace ostinato::trace::cells
