// The two flag bytes of a 32-byte monster-properties record. No upstream
// symbol source exists for these bits — the meanings live in the prose RAM map
// (original-src/notes/battle-ram.txt:952-970), corroborated by the battle
// consumers cited per bit below. Byte offsets refer to the record layout in
// src/data/monster_properties.h.
#pragma once

#include <cstdint>

#include "ostinato/flag_set.h"

namespace ostinato {

// Record byte +18 -> $3C95 (battle-ram.txt:965-970, "ui-h-n-m"). Bits 1, 3,
// and 5 are unused in the layout; they keep named UNUSED_n enumerators so any
// corpus byte that sets one still renders through a named surface.
enum class MonsterTraitFlag : std::uint8_t {
    DIES_AT_ZERO_MP   = 0x01,  // bit 0: dies at 0 MP (battle_main.asm:
                               //        3027-3029)
    UNUSED_1          = 0x02,  // bit 1: unused
    DONT_DISPLAY_NAME = 0x04,  // bit 2: don't display name
    UNUSED_3          = 0x08,  // bit 3: unused
    HUMAN             = 0x10,  // bit 4: human (battle_main.asm:9161-9163)
    UNUSED_5          = 0x20,  // bit 5: unused
    IMP_CRITICAL      = 0x40,  // bit 6: imp critical — paired with the IMP
                               //        status at both consumers
                               //        (battle_main.asm:6976-6981, 8304-8308)
    UNDEAD            = 0x80,  // bit 7: undead (battle_main.asm:5837-5839)
};

// Record byte +19 -> $3C80 (battle-ram.txt:952-960, "c?ksruph").
enum class MonsterBattleFlag : std::uint8_t {
    HARDER_TO_RUN = 0x01,  // bit 0: harder to run
    FIRST_STRIKE  = 0x02,  // bit 1: first strike — an action at the very start
                           //        of battle (battle_main.asm:7483-7487)
    CANT_SUPLEX   = 0x04,  // bit 2: can't suplex (battle_main.asm:9728-9730)
    CANT_RUN      = 0x08,  // bit 3: can't run
    CANT_SCAN     = 0x10,  // bit 4: can't scan (battle_main.asm:9713-9715)
    CANT_SKETCH   = 0x20,  // bit 5: can't sketch (battle_main.asm:9524-9530)
    SPECIAL_EVENT = 0x40,  // bit 6: special event ??? (upstream's own
                           //        uncertainty note, preserved verbatim)
    CANT_CONTROL  = 0x80,  // bit 7: can't control (battle_main.asm:9471-9476)
};

using MonsterTraitFlags  = FlagSet<MonsterTraitFlag>;
using MonsterBattleFlags = FlagSet<MonsterBattleFlag>;

static_assert(sizeof(MonsterTraitFlags) == 1 && sizeof(MonsterBattleFlags) == 1,
              "monster flag sets must stay byte-identical to their ROM bytes");

}  // namespace ostinato
