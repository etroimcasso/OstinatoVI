// Monster special-attack animations: the 384-record MonsterSpecialAnim
// table. The row data is generated
// (src/data/generated/monster_special_anim_data.inc); this header owns the
// entry type and the accessors.
//
// The table lives at ROM CF/37C0 (one byte per monster; incbin at
// battle_main.asm:16476-16477). Each byte is the row the monster's special
// attack selects in the Monster Attack Animation Data table — the battle
// loader stores it per monster (battle_main.asm:7420; the rage path copies
// it too, :1004-1005), and when a special attack fires the battle graphics
// code multiplies it by 8 to load that table's 8-byte record
// (battle_main.asm:8193-8194; btlgfx_main.asm:23661-23677). The rows are a
// symbol set, so the value surfaces as its MonsterAttackAnimation
// enumerator, never a bare byte.
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "ostinato/monster_attack_animation.h"
#include "ostinato/monster_id.h"

namespace ostinato {

// One table entry: the monster's identity as a typed field (the MonsterId
// enumerator — identity is a field, never a comment) and the animation its
// special attack plays. Every generated row reads { .id = MonsterId::NAME,
// .specialAnim = MonsterAttackAnimation::NAME }; a compile-time assert
// verifies id == array position.
struct MonsterSpecialAnimEntry {
    MonsterId id;
    MonsterAttackAnimation specialAnim;
};

// The animation a monster's special attack plays. The table is
// version-invariant (a single un-suffixed rip artifact backs all supported
// ROMs).
MonsterAttackAnimation monsterSpecialAnim(MonsterId id);

// The full 384-entry table (MONSTER index order), for iteration and
// full-corpus tests.
std::span<const MonsterSpecialAnimEntry> monsterSpecialAnims();

}  // namespace ostinato
