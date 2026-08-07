# Engine — Data Layer

Guide for working with the static game-content data this port ships with. Read this
if you want to look up character stats, rebalance a spell, change what an esper
teaches, or otherwise customize the gameplay data the game queries at runtime. Each
subsystem is documented in its own file under this directory — see the index below.

## What the data layer is

The data layer holds Final Fantasy VI's static game content as compile-time
`constexpr` C++ arrays plus pure lookup functions. No runtime state, no platform
dependencies — it's read-only data the game queries on demand.

The data tables in `src/data/generated/*.inc` and the enum headers under
`include/ostinato/` that carry an `AUTO-GENERATED` banner were generated **once**,
by Python scripts under `tools/asm_parser/` reading the `original-src/` disassembly
checkout. Those scripts exist to seed the C++ surface with byte-fidelity to the
original ROM — they're port-time tooling, not a live abstraction layer. Once the
files are committed, the `.inc` files and enum headers are normal C++ source: you
modify the game by editing the C++.

One caveat when editing generated rows: each table has a test that compares every
entry against a fixture under `tests/fixtures/` (also generated, holding the
original ROM values). A deliberate gameplay change to a row must update the matching
fixture row too, or that table's test reports the divergence.

## Header layout

Two include roots serve the data layer, both on the public include path:

- `include/ostinato/*.h`, included as `#include "ostinato/<name>.h"` — the typed
  vocabulary. Enum headers (most carry the `AUTO-GENERATED` banner) plus the small
  hand-written value types that compose them (`ElementSet`, `StatusSet`, `FlagSet`,
  `Targeting`, `CharacterTraits`, `GameVersion`).
- `src/data/*.h`, included as `#include "data/<name>.h"` — the record types, the
  tables, and the accessors. One header per data unit; the backing rows live in
  `src/data/generated/*.inc` and are `#include`d into each table's initializer.

## Index

| File | Covers |
|---|---|
| [foundational-enums.md](foundational-enums.md) | The typed vocabulary — every game-domain `enum class`: characters, attacks, monsters, items, statuses, elements, commands, espers, dances, and the `GameVersion` axis. |
| [typed-wrappers.md](typed-wrappers.md) | The hand-written value types over packed ROM bytes — `ElementSet`, `StatusSet`, `FlagSet<F>`, `Targeting`, `CharacterTraits`, and the attack flag enums. |
| [characters.md](characters.md) | Character base stats — the 64-record 22-byte table: starting stats, battle commands, initial equipment, traits. |
| [rng.md](rng.md) | The 256-byte random-number table and `rngByte`. |
| [attack-properties.md](attack-properties.md) | The 256-record attack-properties table — targeting, elements, flags, MP cost, power, hit rate, special effect, statuses for every attack. |
| [battle-magic-points.md](battle-magic-points.md) | The per-battle magic-point award table (512 entries). |
| [dances.md](dances.md) | The 8 dances and their four candidate attacks each. |
| [espers.md](espers.md) | The 27 esper records — teachable spells with learn rates and the level-up bonus. |
| [natural-magic.md](natural-magic.md) | Terra's and Celes's natural-magic spell/level tables. |
| [item-properties.md](item-properties.md) | The 256-record item-properties table — item, weapon, armor, and relic stats in one 30-byte record: equip permissions, effect bits, stat boosts, spell casts, prices. |
| [shop-properties.md](shop-properties.md) | The 128 shop records — shop type, price adjustment, and eight item slots each. |
| [colosseum-wagers.md](colosseum-wagers.md) | The colosseum wager table — for each wagerable item, the monster fought and the prize won. |
| [monster-properties.md](monster-properties.md) | The 384-record monster-properties table — stats, flags, immunities, elements, the packed metamorph and special-attack bytes — plus the metamorph item packs and odds thresholds they key. |
