// Monster properties: the 384-record monster_prop table. The row data is
// generated (src/data/generated/monster_prop_data.inc); this header owns the
// record type, the entry type, and the accessors.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/blocked_status_set.h"
#include "ostinato/element_set.h"
#include "ostinato/item_id.h"
#include "ostinato/metamorph_info.h"
#include "ostinato/monster_flags.h"
#include "ostinato/monster_id.h"
#include "ostinato/monster_special_attack.h"
#include "ostinato/status_set.h"

namespace ostinato {

// One 32-byte monster-properties record (monster_prop.dat, ROM CF/0000).
// Member order and widths mirror the record layout the battle loaders read —
// LoadMonsterProp (battle_main.asm:7307-7436) and LoadRageProp (:7504-7550)
// name every byte in their load comments — so the object representation is
// byte-identical to a ROM record, pinned by the static_asserts below and the
// full-corpus byte-equivalence test. hp/mp/experience/gold are the ROM's
// 16-bit little-endian values; the little-endian platform assert below keeps
// the u16 object bytes in ROM order.
struct MonsterProperties {
    std::uint8_t speed;
    std::uint8_t attackPower;
    std::uint8_t hitRate;
    std::uint8_t evade;
    std::uint8_t magicBlock;
    std::uint8_t defense;
    std::uint8_t magicDefense;
    std::uint8_t magicPower;
    std::uint16_t hp;
    std::uint16_t mp;
    std::uint16_t experience;
    std::uint16_t gold;
    std::uint8_t level;
    MetamorphInfo metamorph;
    MonsterTraitFlags traitFlags;
    MonsterBattleFlags battleFlags;
    BlockedStatusSet blockedStatuses;
    ElementSet absorbElements;
    ElementSet nullifyElements;
    ElementSet weakElements;
    // "Item number for graphics" (battle_main.asm:7395) — the item whose
    // attack animation the monster's fight command borrows.
    ItemId attackGraphic;
    StatusSet innateStatuses;
    MonsterSpecialAttack specialAttack;
};

static_assert(sizeof(MonsterProperties) == 32,
              "MonsterProperties must be byte-identical to a 32-byte "
              "monster_prop record");
// The u16 fields hold ROM little-endian values; on a little-endian platform
// their object bytes are the ROM bytes, which the full-corpus memcmp test
// depends on. A big-endian port would need explicit byte-order handling here.
static_assert(std::endian::native == std::endian::little,
              "MonsterProperties u16 fields assume a little-endian platform");
// The byte offsets ARE the contract (the loaders' MonsterProp+N reads).
static_assert(offsetof(MonsterProperties, speed) == 0);
static_assert(offsetof(MonsterProperties, attackPower) == 1);
static_assert(offsetof(MonsterProperties, hitRate) == 2);
static_assert(offsetof(MonsterProperties, evade) == 3);
static_assert(offsetof(MonsterProperties, magicBlock) == 4);
static_assert(offsetof(MonsterProperties, defense) == 5);
static_assert(offsetof(MonsterProperties, magicDefense) == 6);
static_assert(offsetof(MonsterProperties, magicPower) == 7);
static_assert(offsetof(MonsterProperties, hp) == 8);
static_assert(offsetof(MonsterProperties, mp) == 10);
static_assert(offsetof(MonsterProperties, experience) == 12);
static_assert(offsetof(MonsterProperties, gold) == 14);
static_assert(offsetof(MonsterProperties, level) == 16);
static_assert(offsetof(MonsterProperties, metamorph) == 17);
static_assert(offsetof(MonsterProperties, traitFlags) == 18);
static_assert(offsetof(MonsterProperties, battleFlags) == 19);
static_assert(offsetof(MonsterProperties, blockedStatuses) == 20);
static_assert(offsetof(MonsterProperties, absorbElements) == 23);
static_assert(offsetof(MonsterProperties, nullifyElements) == 24);
static_assert(offsetof(MonsterProperties, weakElements) == 25);
static_assert(offsetof(MonsterProperties, attackGraphic) == 26);
static_assert(offsetof(MonsterProperties, innateStatuses) == 27);
static_assert(offsetof(MonsterProperties, specialAttack) == 31);

// One table entry: the record's identity as a typed field (the MonsterId
// enumerator — identity is a field, never a comment) alongside the packed
// record, which stays sizeof-locked to the ROM bytes. Every generated row
// reads { .id = MonsterId::NAME, .record = { ... } }; a compile-time assert
// verifies id == array position for every entry.
struct MonsterPropertiesEntry {
    MonsterId id;
    MonsterProperties record;
};

// The record for a monster. The table is version-invariant (a single
// un-suffixed rip artifact backs all supported ROMs).
const MonsterProperties& getMonsterProperties(MonsterId id);

// The full 384-entry table (MONSTER index order), for iteration and
// full-corpus tests.
std::span<const MonsterPropertiesEntry> monsterProperties();

}  // namespace ostinato
