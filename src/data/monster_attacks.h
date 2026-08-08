// Monster attack-slot tables: rage (256 records), sketch (384), and control
// (384). The row data is generated (src/data/generated/monster_rage_data.inc,
// monster_sketch_data.inc, monster_control_data.inc); this header owns the
// record types, the entry types, and the accessors.
//
// The three tables sit consecutively in ROM — control at CF/3D00, sketch at
// CF/4300, rage at CF/4600 — each one macro row per monster
// (monster_control.asm, monster_sketch.asm, monster_rage.asm). Consumers:
// the rage pick coin-flips between a row's two slots
// (battle_main.asm:985-990); the sketch effect picks slot 1 at 3/4 and
// slot 0 at 1/4 (battle_main.asm:9543-9549); control rows list in the
// control menu with AttackId::NONE ($FF) as the empty sentinel
// (battle_main.asm:8876-8894, muddled/colosseum pick :1021-1044).
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "ostinato/attack_id.h"
#include "ostinato/monster_id.h"

namespace ostinato {

// One 2-byte rage record. The upstream macro takes only the second attack —
// slot 0 is structurally always AttackId::BATTLE, the monster's normal fight
// command (monster_rage.asm:3-5).
struct MonsterRage {
    std::array<AttackId, 2> attacks;
};

static_assert(sizeof(MonsterRage) == 2,
              "MonsterRage must be byte-identical to a 2-byte ROM record");

// One 2-byte sketch record: the two candidate attacks in slot order
// (monster_sketch.asm:3-5). Slot 1 is the likely pick (3/4); slot 0 the
// rare one (1/4).
struct MonsterSketch {
    std::array<AttackId, 2> attacks;
};

static_assert(sizeof(MonsterSketch) == 2,
              "MonsterSketch must be byte-identical to a 2-byte ROM record");

// One 4-byte control record: slot 0 is structurally always
// AttackId::BATTLE; unused slots hold AttackId::NONE (the macro's
// blank-argument padding, monster_control.asm:3-20).
struct MonsterControl {
    std::array<AttackId, 4> attacks;
};

static_assert(sizeof(MonsterControl) == 4,
              "MonsterControl must be byte-identical to a 4-byte ROM record");

// Table entries: each record's identity as a typed field (the MonsterId
// enumerator — identity is a field, never a comment) alongside the packed
// record. Compile-time asserts verify id == array position for every entry
// of every table.
struct MonsterRageEntry {
    MonsterId id;
    MonsterRage record;
};

struct MonsterSketchEntry {
    MonsterId id;
    MonsterSketch record;
};

struct MonsterControlEntry {
    MonsterId id;
    MonsterControl record;
};

// The rage record for a monster. PRECONDITION (asserted): id < 256 — the
// rage table ends at CF/4800 and both the known-rage list and the rage pick
// index it 8-bit (battle_main.asm:976-990, :999-1010); monsters 256-383
// have no rage row, and that absence is contract.
const MonsterRage& getMonsterRage(MonsterId id);

// The sketch record for a monster.
const MonsterSketch& getMonsterSketch(MonsterId id);

// The control record for a monster.
const MonsterControl& getMonsterControl(MonsterId id);

// The full tables (MONSTER index order), for iteration and full-corpus
// tests. All three are version-invariant (single un-suffixed rip artifacts
// back all supported ROMs).
std::span<const MonsterRageEntry> monsterRages();
std::span<const MonsterSketchEntry> monsterSketches();
std::span<const MonsterControlEntry> monsterControls();

}  // namespace ostinato
