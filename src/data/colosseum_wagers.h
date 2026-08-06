// Colosseum wagers: the 256-record ColosseumProp table — for each wagerable
// item, the monster fought and the prize won. The row data is generated
// (src/data/generated/colosseum_prop_data.inc); this header owns the record
// type, the entry type, and the accessors.
//
// The table is committed source upstream (src/menu/colosseum.asm:1212, ROM
// DF/B600): 256 make_colosseum_prop rows of 4 bytes each (the macro at
// colosseum.asm:1189-1204), indexed by the wagered item's id. The sole
// reader is LoadColosseumProp (colosseum.asm:833-846), which reads the
// monster (+0), prize (+2), and hide flag (+3) — byte +1 is dead data (see
// kColosseumUnusedByte below).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

namespace ostinato {

// Record byte +3: $ff hides the prize name in the wager menu until the
// battle is won; $00 shows it (the macro's hide_prize arg,
// colosseum.asm:1199-1203; read at colosseum.asm:844).
inline constexpr std::uint8_t kHidePrize = 0xFF;
inline constexpr std::uint8_t kShowPrize = 0x00;

// Record byte +1: every row carries $40 and no code in the tree reads the
// byte (LoadColosseumProp skips from +0 to +2). Ports verbatim as dead data.
inline constexpr std::uint8_t kColosseumUnusedByte = 0x40;

// The monster field is ONE byte (.byte MONSTER::name in the macro) while the
// MONSTER index space is 384 entries wide — so every wagered monster's index
// fits a byte (ca65 would error otherwise; the parser hard-asserts it, and
// the full-corpus byte-equivalence test pins the stored bytes). This helper
// narrows a MonsterId to that stored byte for the generated rows.
constexpr std::uint8_t wagerMonster(MonsterId id) {
    return static_cast<std::uint8_t>(id);
}

// One 4-byte colosseum record. Member order and widths mirror the ROM record
// exactly — pinned by the static_asserts below and the full-corpus
// byte-equivalence test.
struct ColosseumWager {
    std::uint8_t monster;         // +0 colosseum.asm:840 (MONSTER index low byte)
    std::uint8_t unused40;        // +1 dead data — no consumer reads it
    ItemId prize;                 // +2 colosseum.asm:842
    std::uint8_t hidePrizeFlag;   // +3 colosseum.asm:844

    constexpr MonsterId monsterId() const {
        return static_cast<MonsterId>(monster);
    }

    constexpr bool prizeHidden() const { return hidePrizeFlag == kHidePrize; }
};

static_assert(sizeof(ColosseumWager) == 4,
              "ColosseumWager must be byte-identical to a 4-byte ColosseumProp record");
static_assert(offsetof(ColosseumWager, monster) == 0);
static_assert(offsetof(ColosseumWager, unused40) == 1);
static_assert(offsetof(ColosseumWager, prize) == 2);
static_assert(offsetof(ColosseumWager, hidePrizeFlag) == 3);

// One table entry: the record's identity as a typed field (the wagered
// item's ItemId enumerator — the table is indexed by wagered item) alongside
// the packed record. A compile-time assert verifies id == array position for
// every entry.
struct ColosseumWagerEntry {
    ItemId id;
    ColosseumWager record;
};

// The wager record for an item. ItemId is uint8_t, so every value indexes
// the 256-entry table by construction. Unwagerable items carry the table's
// default row (Chupon for no prize — see the contract doc).
const ColosseumWager& getColosseumWager(ItemId wagered);

// The full 256-entry table (ITEM index order), for iteration and full-corpus
// tests.
std::span<const ColosseumWagerEntry> colosseumWagers();

}  // namespace ostinato
