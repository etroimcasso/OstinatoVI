// Monster steal/drop items: the 384-record MonsterItems table. The row data
// is generated (src/data/generated/monster_items_data.inc); this header owns
// the record type, the entry type, and the accessors.
//
// The table lives at ROM CF/3000 (one monster_steal + monster_drop macro
// pair per monster, monster_items.asm; the MonsterItems label sits at the
// include site, battle_main.asm:16468-16469). The monster loader copies the
// steal pair to the per-monster steal cells (battle_main.asm:7317); the
// victory sequence reads the drop pair (battle_main.asm:15494).
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

namespace ostinato {

// One 4-byte monster-items record: the steal pair then the drop pair, each
// rare-before-common (the monster_steal macro body's byte order,
// monster_items.asm:8-12). ItemId::EMPTY ($FF) marks an empty slot.
struct MonsterItems {
    ItemId rareSteal;
    ItemId commonSteal;
    ItemId rareDrop;
    ItemId commonDrop;
};

static_assert(sizeof(MonsterItems) == 4,
              "MonsterItems must be byte-identical to a 4-byte ROM record");
static_assert(offsetof(MonsterItems, rareSteal) == 0);
static_assert(offsetof(MonsterItems, commonSteal) == 1);
static_assert(offsetof(MonsterItems, rareDrop) == 2);
static_assert(offsetof(MonsterItems, commonDrop) == 3);

// One table entry: the record's identity as a typed field (the MonsterId
// enumerator — identity is a field, never a comment) alongside the packed
// record, which stays sizeof-locked to the ROM bytes. Every generated row
// reads { .id = MonsterId::NAME, .record = { ... } }; a compile-time assert
// verifies id == array position for every entry.
struct MonsterItemsEntry {
    MonsterId id;
    MonsterItems record;
};

// The steal/drop record for a monster. The table is version-invariant (a
// single un-suffixed rip artifact backs all supported ROMs).
const MonsterItems& getMonsterItems(MonsterId id);

// The full 384-entry table (MONSTER index order), for iteration and
// full-corpus tests.
std::span<const MonsterItemsEntry> monsterItems();

}  // namespace ostinato
