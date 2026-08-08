// Monster vertical alignments: the 256-record MonsterAlign table. The row
// data is generated (src/data/generated/monster_align_data.inc); this header
// owns the entry type and the accessors.
//
// The table lives at ROM EC/E800 (one byte per monster, values strictly
// 0-4; incbin at btlgfx_main.asm:48910-48916). Both consumers are
// colosseum-specific — battle setup for the colosseum battle
// (btlgfx_main.asm:2872-2883, with CEILING special-cased to the top of the
// screen) and the colosseum wager menu (menu/colosseum.asm:555-564) — and
// both index the table with an 8-bit monster id: monsters 256-383 have no
// alignment row, and that absence is contract.
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "ostinato/monster_id.h"
#include "ostinato/monster_vertical_alignment.h"

namespace ostinato {

// One table entry: the monster's identity as a typed field (the MonsterId
// enumerator — identity is a field, never a comment) and its vertical
// alignment. Every generated row reads { .id = MonsterId::NAME,
// .alignment = MonsterVerticalAlignment::NAME }; a compile-time assert
// verifies id == array position.
struct MonsterAlignEntry {
    MonsterId id;
    MonsterVerticalAlignment alignment;
};

// The vertical alignment for a monster. PRECONDITION (asserted): id < 256 —
// the table covers the consumers' 8-bit index space only. The table is
// version-invariant (a single un-suffixed rip artifact backs all supported
// ROMs).
MonsterVerticalAlignment getMonsterAlignment(MonsterId id);

// The full 256-entry table (MONSTER index order), for iteration and
// full-corpus tests.
std::span<const MonsterAlignEntry> monsterAlignments();

}  // namespace ostinato
