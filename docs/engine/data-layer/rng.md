# RNG table

## Public surface

```cpp
#include "data/rng_table.h"

std::uint8_t b = ostinato::rngByte(index);   // index: uint8_t — always in range
ostinato::kRngTable;                         // std::array<RngTableEntry, 256>
```

## What it is

Final Fantasy VI's random numbers come from a fixed 256-byte table in the ROM, not
from a hardware or algorithmic generator — consumers keep a cursor into the table
and read successive bytes. This surface is that table, verbatim:

```cpp
struct RngTableEntry {
    std::uint8_t index;   // 0..255, equals the array position
    std::uint8_t value;   // the ROM byte at that position
};

inline constexpr std::array<RngTableEntry, 256> kRngTable = { /* generated rows */ };

std::uint8_t rngByte(std::uint8_t index);   // the byte at table position `index`
```

`rngByte` is a plain positional read — cursor state, reseeding, and every other
consumption pattern belong to the battle/field consumers that use the table.
Because the parameter is `uint8_t`, every argument value is in range by
construction; there is no bounds check to fail.

## Backing data / where to change

Rows live in `src/data/generated/rng_tbl_data.inc` as
`{ .index = N, .value = 0xNN }` pairs (decimal position, hex ROM byte). A
compile-time assert verifies every row's `.index` equals its array position.
Changing a value changes every downstream random outcome that reads that position —
the fixture test (`tests/fixtures/rng_tbl_expected.h`) will flag any edit until the
fixture row is updated to match.

## What's tested

`tests/test_rng_table.cpp` — all 256 entries compared against the fixture (index
and value), plus accessor reads at positions `0x00`, `0x80`, and `0xFF`.
