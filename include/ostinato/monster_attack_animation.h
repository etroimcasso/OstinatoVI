// The monster attack animations: the 35 rows of the Monster Attack
// Animation Data table (ROM EC/E6E8, 8 bytes each — rom-map.txt:260). A
// monster's special attack selects one row (its per-monster byte in
// monster_special_anim.dat, surfaced by src/data/monster_special_anim.h);
// the battle graphics code loads the row to play the attack
// (InitWeaponAnim, btlgfx_main.asm:23661-23677).
//
// The upstream has no symbolic names for these rows, but the corpus does:
// every monster whose special attack uses a row also carries a display name
// for that attack (the Monster Special Attack Names text table, ROM CFD0D0
// — rom-map.txt:127), and the names cluster by animation. Each enumerator
// is the DOMINANT display name among the row's users (ties broken by the
// earliest monster index; rows no monster uses keep UNUSED_n names). The
// derivation is mechanical and re-verified: parse_monster_special_anim.py
// recomputes the table from the corpus on every run and hard-errors if
// these names drift. Per-enumerator comments carry the derivation counts —
// a 1-of-N name is representative of its row, not authoritative.
#pragma once

#include <cstdint>

namespace ostinato {

enum class MonsterAttackAnimation : std::uint8_t {
    HIT        = 0,   // "Hit", 69 of 91 users
    SICKLE     = 1,   // "Sickle", 1 of 2 users
    DIVE       = 2,   // "Dive", 1 of 2 users
    CRITICAL   = 3,   // "Critical", 2 of 6 users
    WING       = 4,   // "Wing", 1 of 7 users
    SEIZE      = 5,   // "Seize", 4 of 8 users
    SLASH      = 6,   // "Slash", 2 of 12 users
    TAIL       = 7,   // "Tail", 4 of 15 users
    SCRATCH    = 8,   // "Scratch", 3 of 17 users
    RAPIER     = 9,   // "Rapier", 2 of 5 users
    POUNCE     = 10,  // "Pounce", 2 of 16 users
    UMBRAWLER  = 11,  // "Umbrawler", 1 of 3 users
    RUSH       = 12,  // "Rush", 5 of 44 users
    AXE        = 13,  // "Axe", 3 of 7 users
    BITE       = 14,  // "Bite", 6 of 43 users
    IRONNEEDLE = 15,  // "IronNeedle", 1 of 9 users
    INK        = 16,  // "Ink", 5 of 8 users
    PAUSE      = 17,  // "Pause", the sole user
    BONE       = 18,  // "Bone", 2 of 4 users
    WRENCH     = 19,  // "Wrench", 1 of 4 users
    BRAINSTORM = 20,  // "BrainStorm", 1 of 7 users
    NEAR_FATAL = 21,  // "Near Fatal", 1 of 10 users
    METAL_ARM  = 22,  // "Metal Arm", 1 of 3 users
    SLIME      = 23,  // "Slime", 2 of 10 users
    INVIZ      = 24,  // "Inviz", 1 of 9 users
    YAWN       = 25,  // "Yawn", 2 of 9 users
    SMIRK      = 26,  // "Smirk", 1 of 4 users
    WHEEL      = 27,  // "Wheel", all 3 users
    IMPMARE    = 28,  // "Impmare", 1 of 11 users
    UNUSED_29  = 29,  // no monster uses this row
    CLING      = 30,  // "Cling", 2 of 3 users
    DRILL      = 31,  // "Drill", both users
    IRON_BALL  = 32,  // "Iron Ball", both users
    TRADEOFF   = 33,  // "Tradeoff", 1 of 7 users
    UNUSED_34  = 34,  // no monster uses this row
};

static_assert(sizeof(MonsterAttackAnimation) == 1,
              "MonsterAttackAnimation must be byte-identical to a ROM "
              "animation-index byte");

}  // namespace ostinato
