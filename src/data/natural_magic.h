// The natural-magic tables. The table definitions are generated
// (src/data/generated/natural_magic_data.inc, consumed at namespace scope
// below); this header owns the record type and the slot-entry type. There is
// deliberately NO accessor and NO character dispatch: which character reads
// which half is consumer logic (the level-up and event learn routines), never
// data-layer scope — hence two named tables, header-only.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ostinato/attack_id.h"

namespace ostinato {

// One 2-byte natural-magic pair (the NaturalMagic block in event.asm, ROM
// EC/E3C0). ROM byte order is spell first, level second (the opposite of the
// esper table's pairs — do not conflate).
struct NaturalMagicEntry {
    AttackId spell;
    std::uint8_t level;
};

static_assert(sizeof(NaturalMagicEntry) == 2,
              "NaturalMagicEntry must be byte-identical to a ROM {spell, level} pair");
static_assert(offsetof(NaturalMagicEntry, spell) == 0);
static_assert(offsetof(NaturalMagicEntry, level) == 1);

// One table row: the pair's identity as a typed field (its decimal slot
// within the character's half — identity is a field, never a comment; no
// index enum exists for the slots) alongside the packed record, which stays
// sizeof-locked to the ROM bytes. A compile-time assert verifies
// slot == array position for every row of both tables.
struct NaturalMagicSlot {
    std::uint8_t slot;
    NaturalMagicEntry record;
};

// kNaturalMagicTerra / kNaturalMagicCeles — the two 16-pair halves of the
// contiguous ROM block (Terra first; the consumers read Celes's half at
// NaturalMagic+$20). Celes's out-of-sorted-order MUDDLE-at-32 entry after
// BSERK-at-40 is the ROM's own ordering, ported verbatim.
#include "data/generated/natural_magic_data.inc"

// Self-consistency of the emitted rows: every row's slot field must equal
// its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kNaturalMagicTerra.size(); ++i) {
        if (kNaturalMagicTerra[i].slot != i) {
            return false;
        }
    }
    for (std::size_t i = 0; i < kNaturalMagicCeles.size(); ++i) {
        if (kNaturalMagicCeles[i].slot != i) {
            return false;
        }
    }
    return true;
}(), "natural-magic slot fields must match array positions in both tables");

}  // namespace ostinato
