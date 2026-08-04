# Natural magic

## Public surface

```cpp
#include "data/natural_magic.h"

ostinato::kNaturalMagicTerra;   // std::array<NaturalMagicSlot, 16>
ostinato::kNaturalMagicCeles;   // std::array<NaturalMagicSlot, 16>
```

## What it is

The spells Terra and Celes learn automatically by level:

```cpp
struct NaturalMagicEntry {
    AttackId     spell;   // the learned spell
    std::uint8_t level;   // the level it's learned at
};
static_assert(sizeof(NaturalMagicEntry) == 2);

struct NaturalMagicSlot {
    std::uint8_t     slot;     // 0..15, equals the array position
    NaturalMagicEntry record;
};
```

Two 16-pair tables — the two halves of one contiguous ROM block (Terra's half
first; the original reads Celes's at a fixed offset past Terra's). Within each
pair the ROM byte order is **spell first, level second** — the opposite of the
esper table's pairs ([espers.md](espers.md)); don't conflate the two.

There is deliberately **no accessor and no character dispatch**: which character
reads which half is consumer logic (the level-up and event learn routines), so the
surface is two named tables, header-only.

## The Celes ordering quirk

Celes's list holds `MUDDLE` at level 32 **after** `BSERK` at level 40 — slot 9
after slot 8, out of sorted-level order. That is the ROM's own ordering, preserved
verbatim: consumers walk the list by slot, not by level, so re-sorting it would
change observable behavior. A dedicated test pins the order; don't "fix" it.

## Backing data / where to change

Both tables live in `src/data/generated/natural_magic_data.inc` (consumed at
namespace scope by `src/data/natural_magic.h`):

```cpp
NaturalMagicSlot{ .slot = 0,
                  .record = NaturalMagicEntry{ .spell = AttackId::CURE, .level = 1 } },
```

A compile-time assert verifies every row's `.slot` equals its array position in
both tables. A deliberate change must also update the matching row in
`tests/fixtures/natural_magic_expected.h` (original ROM values).

## What's tested

`tests/test_natural_magic.cpp` — all 32 pairs (both halves) `memcmp`-compared
byte-for-byte against the fixture; boundary rows of each half (Terra `CURE@1` /
`ULTIMA@99`, Celes `ICE@1` / `METEOR@98`); and the dedicated Celes
`BSERK@40 → MUDDLE@32` order pin.
