// The party's records as the cartridge keeps them: the sixteen character records, each field's
// offset in a record, the espers, the spells known and the battle's magic points.
//
// The addresses are $7E:xxxx cells in namespace cells: a file that names them says
// `using namespace cells;` inside ostinato::trace.
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/character_record.h"

#include "cells/damage/per_target.h"

namespace ostinato::trace::cells {

// The sixteen character records, 37 bytes each, and the places of a record's fields within it
// (notes/field-ram.txt): the actor, graphics, name, level, HP, maximum HP with the HP boost in its top
// two bits, MP, maximum MP with the MP boost, experience (three bytes), the first and fourth status
// bytes, the four commands, the four stats, the esper and the six pieces of equipment.
inline constexpr std::uint32_t kCharacterRecords = kWorkRam + 0x1600;
inline constexpr std::uint32_t kRecordSize = 37;
inline constexpr std::size_t kRecordGraphics = 1;
inline constexpr std::size_t kRecordName = 2;
inline constexpr std::size_t kRecordLevel = 8;
inline constexpr std::size_t kRecordHp = 9;
inline constexpr std::size_t kRecordMaxHp = 11;
inline constexpr std::size_t kRecordMp = 13;
inline constexpr std::size_t kRecordMaxMp = 15;
inline constexpr std::size_t kRecordExperience = 17;
inline constexpr std::size_t kRecordFirstStatus = 20;
inline constexpr std::size_t kRecordFourthStatus = 21;
inline constexpr std::size_t kRecordCommands = 22;
inline constexpr std::size_t kRecordVigor = 26;
inline constexpr std::size_t kRecordSpeed = 27;
inline constexpr std::size_t kRecordStamina = 28;
inline constexpr std::size_t kRecordMagicPower = 29;
inline constexpr std::size_t kRecordEsper = 30;
inline constexpr std::size_t kRecordEquipment = 31;
// The esper byte names an esper by its place among the 27, or holds this for none.
inline constexpr std::uint8_t kNoEsper = 0xFF;
inline constexpr std::size_t kEspers = 27;
// The maximum HP and MP words keep the boost in their top two bits.
inline constexpr int kBoostShift = 14;
inline constexpr std::uint16_t kMaximumBits = 0x3FFF;

// The spells each of the first twelve characters knows, 54 bytes each.
inline constexpr std::uint32_t kSpellsKnown = kWorkRam + 0x1A6E;
inline constexpr std::uint32_t kSpellsKnownSize = 648;

// The battle's-end cells in direct page: the win's flags, whether the party has magicite (battle
// switch 67's bit as the win copies it, which is magic points earned in the flags and is OR-ed into
// them when a spell is stored), the place of the spells being learned and the battle's magic points.
inline constexpr std::uint32_t kWinFlags = kWorkRam + 0x00F0;
inline constexpr std::uint32_t kPartyHasMagicite = kWorkRam + 0x00F1;
inline constexpr std::uint32_t kSpellListPlace = kWorkRam + 0x00F4;
inline constexpr std::uint32_t kMagicPoints = kWorkRam + 0x00FB;
inline constexpr std::uint8_t kMagiciteBit = 0x08;

}  // namespace ostinato::trace::cells
