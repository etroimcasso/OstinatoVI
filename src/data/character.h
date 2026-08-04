// Hand-written port-design (PLAN phase-1.A D7 + Amendment A1). The row data is
// parser-emitted (src/data/generated/char_prop_data.inc); this struct + lookup
// are port design informed by the contract, not transcribed from it.
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/character_traits.h"
#include "ostinato/item_id.h"

namespace ostinato {

// One 22-byte character record. Member order and widths mirror char_prop.asm's
// end_char_prop byte layout exactly, so the object representation is byte-identical
// to a ROM record — pinned by the static_assert below and the full-corpus
// byte-equivalence test.
struct CharacterBaseStats {
    std::uint8_t hp;
    std::uint8_t mp;
    std::array<BattleCommandId, 4> commands;
    std::uint8_t strength;
    std::uint8_t agility;
    std::uint8_t stamina;
    std::uint8_t magicPower;
    std::uint8_t battlePower;
    std::uint8_t defense;
    std::uint8_t magicDefense;
    std::uint8_t evade;
    std::uint8_t magicBlock;
    ItemId weapon;
    ItemId shield;
    ItemId helmet;
    ItemId armor;
    ItemId relic1;
    ItemId relic2;
    CharacterTraits traits;
};

static_assert(sizeof(CharacterBaseStats) == 22,
              "CharacterBaseStats must be byte-identical to a 22-byte char_prop record");

// One table entry: the record's identity as a typed field (the
// CharacterPropId enumerator — identity is a field, never a comment)
// alongside the packed record, which stays sizeof-locked to the ROM bytes.
// Every generated row reads { .id = CharacterPropId::NAME, .record = { ... } };
// a compile-time assert verifies id == array position for every entry.
struct CharacterBaseStatsEntry {
    CharacterPropId id;
    CharacterBaseStats record;
};

// The record index is a CharacterPropId — the 64-value char_prop index space,
// NOT a CharacterId. CharacterId spans only 0x00..0x0f with heavy aliasing and
// cannot address a 64-record table; no CharacterId<->CharacterPropId conversion is
// provided. In the original the index arrives from game state (the "actor number"
// event/battle logic multiplies by 22) — any mapping is consumer-phase game logic,
// never data-layer scope (PLAN Amendment A1).
const CharacterBaseStats& getCharacterBaseStats(CharacterPropId id);

// The full 64-entry table (index order), for iteration and full-corpus tests.
std::span<const CharacterBaseStatsEntry> characterBaseStats();

}  // namespace ostinato
