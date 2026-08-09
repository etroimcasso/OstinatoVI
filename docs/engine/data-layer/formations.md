# Formations — battle_monsters, battle_prop, cond_battle

Three tables define what a battle *is*: which monsters stand where
(`battle_monsters`), how the fight begins (`battle_prop`), and which battles
swap themselves for another when a flag is set (`cond_battle`). All three are
keyed by `FormationId` — a 576-entry enum whose names are the monsters in the
formation.

## Public surface

```cpp
#include "data/formations.h"

using ostinato::FormationId;

// Who is in the fight and where.
const auto& f = ostinato::getFormation(FormationId::SHORT_ARM_LONG_ARM_FACE);
f.monsterId(0);     // MonsterId::SHORT_ARM
f.slotEmpty(1);     // true — nothing in slot 1
f.positionX(0);     // pixels (the packed nibble x 8)
f.isPresent(0);     // true — on-screen at the start (vs. a reinforcement)
f.vramMap();        // 0-12, which sprite-layout map the battle uses

// How the fight begins.
const auto& aux = ostinato::getFormationAux(FormationId::FINAL_KEFKA);
aux.entrance();        // MonsterEntranceType::FINAL_KEFKA_DESCENT
aux.frontPossible();   // can this formation be a front attack?
aux.song();            // BattleSong::DANCING_MAD
aux.runningDisabled(); // can the party flee?

// Conditional substitutions (index 0-15).
const auto& c = ostinato::getConditionalBattle(0);
c.trigger.formationId();      // FormationId::SRBEHEMOTH
c.replacement.formationId();  // FormationId::SRBEHEMOTH_UNDEAD
```

Each table also exposes a full span for iteration — `formations()`,
`formationAux()` (entries of `{ id, record }` in `FormationId` order), and
`conditionalBattles()` (16 `ConditionalBattle` entries).

## FormationId

```cpp
enum class FormationId : std::uint16_t {
    LOBO = 0, LOBO_X2 = 1, /* ... */ SHORT_ARM_LONG_ARM_FACE = 471, /* ... */
};
```

The enumerator names are the formation's contents, in slot order. A monster
that appears more than once collapses into `NAME_Xn` (`LOBO_X2`,
`PIRANHA_X5_RIZOPAS`); a formation with no monsters is `UNUSED_<index>` (14 of
these); and formations that would share a name are disambiguated with `_2`,
`_3`, ... in index order (`KEFKA`, `KEFKA_2`). The value is the formation
index, 0-575.

## Formation record (`battle_monsters`)

```cpp
struct Formation {                         // sizeof == 15
    std::uint8_t vramMapAndBg1;            // byte 0
    std::uint8_t presentAndBg1;           // byte 1
    std::array<std::uint8_t, 6> monsterIdLow;   // bytes 2-7
    std::array<std::uint8_t, 6> position;       // bytes 8-13
    std::uint8_t monsterIdHigh;           // byte 14
};
```

One 15-byte record per formation (576 of them). A formation has six monster
slots; read them through the accessors rather than the raw bytes, because a
slot's identity is packed across two bytes:

- `monsterId(slot)` reassembles the 9-bit id from `monsterIdLow[slot]` plus one
  bit of `monsterIdHigh`. Only meaningful when `!slotEmpty(slot)` — an unused
  slot reads the `$1FF` sentinel.
- `isPresent(slot)` is the present mask: a monster can be in a slot yet absent
  here, meaning it arrives mid-battle as a reinforcement.
- `positionX(slot)` / `positionY(slot)` return pixels (the ROM stores each
  coordinate as a nibble in 8-pixel units).
- `vramMap()` (0-12) selects which sprite-tile layout the battle graphics use;
  `bg1Mask()` is a 6-bit mask that is zero for every formation in the game
  (the feature it gates is unused) and is exposed only for completeness.

## Formation aux record (`battle_prop`)

```cpp
struct FormationAux {                      // sizeof == 4
    std::uint8_t entranceAndTypes;        // byte 0
    std::uint8_t flags;                   // byte 1
    std::uint8_t characterAi;             // byte 2
    std::uint8_t audioFlags;              // byte 3
};
```

One 4-byte record per formation controlling how its battle opens. The bytes
pack several fields, all read through accessors:

- `entrance()` → `MonsterEntranceType` — the animation the monsters make when
  they appear (slide in, drop from the ceiling, fade in, ...).
- `frontPossible()` / `backPossible()` / `pincerPossible()` / `sidePossible()`
  — which attack orientations this formation can be encountered in. (The ROM
  stores this as a *disable* mask that the loader inverts; the accessors give
  you the possible types directly.)
- `fanfareDisabled()`, `jokerDoomDisabled()`, `leapDisabled()`,
  `characterAiEnabled()` + `characterAiIndex()` — battle behavior flags.
- `runningDisabled()`, `veldtDisabled()`, `preemptiveDisabled()`,
  `song()` → `BattleSong`, `continueCurrentMusic()` — flee/veldt/preemptive
  rules and music selection.

## Conditional battles (`cond_battle`)

```cpp
struct ConditionalBattle {                 // sizeof == 4
    FormationRef trigger;
    FormationRef replacement;
};
```

16 entries. When a conditional-battle flag is set, the game replaces the
`trigger` formation with the `replacement` — the canonical case is the undead
Behemoth (entry 0): kill `SRBEHEMOTH` and it comes back as
`SRBEHEMOTH_UNDEAD`. Only entries 0-7 are reachable in game (each maps to one
flag bit); entries 8-15 are dead ROM data, carried for byte fidelity.

A `FormationRef` is a formation word — `formationId()` plus a `randomizePlus3()`
flag (bit 15) that, when set, tells the loader to add a random 0-3 to the
formation index. Build one with `FormationRef::of(FormationId::NAME)`.

## Supporting enums

```cpp
enum class MonsterEntranceType : std::uint8_t { PRE_DRAWN, SMOKE, /* ...18... */ };
enum class BattleSong : std::uint8_t { BATTLE_THEME, THE_DECISIVE_BATTLE, /* ...8... */ };
```

`MonsterEntranceType` (`ostinato/monster_entrance_type.h`) is the 18-entry
entry/exit animation space; a formation selects one of the low 16.
`BattleSong` (`ostinato/battle_song.h`) is the 3-bit music selector, where the
two high values mean "keep the current song." Both headers carry per-enumerator
notes on where each name comes from.

## Backing data / where to change

Rows live in `src/data/generated/` — `formation_data.inc`,
`formation_aux_data.inc`, `cond_battle_data.inc` — each `#include`d into its
array in `src/data/formations.cpp`. Every value is named, so edits read
naturally:

```cpp
    FormationEntry{
        .id = FormationId::LOBO,
        .record = Formation::of({
            .vramMap = 0,
            .slots = {{
            { .monster = MonsterId::LOBO, .x = 6, .y = 9, .present = true },
            {}, {}, {}, {}, {},
            }},
        }),
    },
```

The `Formation::of` / `FormationAux::of` builders pack the named fields back
into the exact ROM bytes, so you never touch a split id or an inverted mask by
hand. A deliberate change must also update the matching row in the fixture
under `tests/fixtures/` (`formation_expected.h`, `formation_aux_expected.h`,
`cond_battle_expected.h`), which hold the original ROM bytes. Compile-time
asserts verify every row's identity field matches its array position.

For the tables' consumer semantics — which routines read each field and the
encounter math that selects a formation — see `docs/contracts/formations.md`.

## What's tested

`tests/test_formation_data.cpp` — every record of all three tables compared in
full against its fixture (no subsets); hand-traced spot checks (Lobo's single
slot, formation 471's three split-id monsters, Kefka's final battle and its
descent entrance, the undead-Behemoth substitution); the aux accessor decode;
the `FormationRef` builder round-trip; and the two formations that carry the
otherwise-unused byte-3 bit.
