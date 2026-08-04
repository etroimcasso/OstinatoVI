# Battle magic points

## Public surface

```cpp
#include "data/battle_magic_points.h"

std::uint8_t mp = ostinato::magicPointsForBattle(battleIndex);  // 0..511
ostinato::kBattleMagicPoints;   // std::array<BattleMagicPointsEntry, 512>
```

## What it is

The magic-point (esper skill point) award for winning each battle formation:

```cpp
struct BattleMagicPointsEntry {
    std::uint16_t battleIndex;   // 0..511, equals the array position
    std::uint8_t  magicPoints;   // the award for that formation
};

std::uint8_t magicPointsForBattle(std::uint16_t battleIndex);
```

512 entries in battle-formation-index order, matching the ROM table byte for byte.

The original's reward routine guards the read: formations with index ≥ 512 award 0
magic points without touching the table. That guard is **consumer reward logic**
and deliberately not part of this surface — the table is strictly the 512 ROM
entries, and `magicPointsForBattle` debug-asserts `battleIndex < 512`. The battle
reward code applies the guard before calling.

## Backing data / where to change

Rows live in `src/data/generated/battle_magic_points_data.inc` as
`{ .battleIndex = N, .magicPoints = M }` pairs (both decimal). A compile-time
assert verifies every row's `.battleIndex` equals its array position. A deliberate
change must also update the matching row in
`tests/fixtures/battle_magic_points_expected.h` (original ROM values).

## What's tested

`tests/test_battle_magic_points.cpp` — all 512 entries compared against the
fixture (identity and value), plus accessor reads at indices 0, 256, and 511.
