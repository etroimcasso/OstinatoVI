// The monster entrance types: the 18-entry entry/exit script space
// (ARRAY_LENGTH = 18, btlgfx_main.asm:45331-45441) that DoMonsterEntryExit
// (btlgfx_main.asm:45529) dispatches on when a battle begins. A formation's
// aux record (src/data/formations.h) selects one in the low nibble of its
// first byte, so a formation can only name values 0-15; values 16-17 exist in
// the script space but are event-command reachable only.
//
// The upstream disassembly names only two of these (13 "flash in/out" and 17
// "final kefka death", both from btlgfx_main.asm comments); 0, 12, and 16 are
// read from the corpus structure (empty vs boss-only scripts). The remaining
// names are the community-documented FF3usME 6.8 labels (its formation
// "Appearance" dropdown and entry/exit sentence-builder strings, which align
// 1:1 with this 18-entry space), cross-checked against the monsters that carry
// each value in the formation table. A later animation-bytecode decode will
// re-verify these descriptions; any correction then is a rename (zero bytes
// change, since the value is what the loader reads).
#pragma once

#include <cstdint>

namespace ostinato {

enum class MonsterEntranceType : std::uint8_t {
    // empty script; usme "Pre-drawn"/"suddenly". Users incl. Tritoch, Doom's
    // arms/face, Guardian + its tools — monsters already on-screen at start.
    PRE_DRAWN = 0,
    // usme "Smoke"/"in smoke". Event-only (no formation uses it).
    SMOKE = 1,
    // usme "Ceiling"/"jumps in". Users: Trapper, Drop, Flan (cave-ceiling).
    DROP_FROM_CEILING = 2,
    // usme "Sides, indiv."/"from side, indiv." — the default (539 of 576).
    SLIDE_FROM_SIDES_INDIVIDUAL = 3,
    // usme "Out of water"/"in water". Users: Piranha, Ultros.
    OUT_OF_WATER = 4,
    // usme "Ceiling + swirl"/"from top in swirl". Event-only.
    TOP_SWIRL = 5,
    // usme labels this terrain-conflictingly ("Out of sands" vs "from water");
    // named terrain-neutral. Users: Ultros (river), Skull Dragon.
    RISE_FROM_BELOW = 6,
    // usme "Sides, synch."/"from side synch.". Users: Whelk, Ice Dragon,
    // Number 128.
    SLIDE_FROM_SIDES_SYNCHRONIZED = 7,
    // usme "Fade-in type 1"/"from top". Event-only.
    FADE_IN_FROM_TOP = 8,
    // usme "Fade-in type 2"/"from bottom". Users: Chadarnook, Doom, Goddess,
    // Poltrgeist.
    FADE_IN_FROM_BOTTOM = 9,
    // usme "Fade-in type 3"/"in checkers". Users: Ifrit, FlameEater,
    // White Drgn.
    FADE_IN_CHECKERED = 10,
    // usme "Fade-in type 4"/"diagonal". Event-only.
    FADE_IN_DIAGONAL = 11,
    // entry script empty, exit script real; usme "dies like a boss" — the
    // value's meaning is its exit animation.
    BOSS_DEATH = 12,
    // dispatcher special case, upstream comment "flash in/out"
    // (btlgfx_main.asm:45534); usme "Flash-in"/"with blinking".
    FLASH = 13,
    // usme sentence list "in light & flashes" (its dropdown says "Invisible" —
    // the sentence list wins, aligning 1:1 with the script space).
    LIGHT_AND_FLASHES = 14,
    // usme "Final Kefka"/"slowly from the skies, Dancing Mad kicks in"; the
    // sole user is formation 514, Kefka's final battle.
    FINAL_KEFKA_DESCENT = 15,
    // empty script; usme "<not tested>"; beyond the formation nibble.
    UNUSED_16 = 16,
    // upstream comment "final kefka death" (btlgfx_main.asm:45538); event-only.
    FINAL_KEFKA_DEATH = 17,
};

static_assert(sizeof(MonsterEntranceType) == 1,
              "MonsterEntranceType must be byte-identical to the entrance "
              "nibble of a formation aux record");

}  // namespace ostinato
