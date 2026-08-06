# Colosseum wagers

## Public surface

```cpp
#include "data/colosseum_wagers.h"

const ostinato::ColosseumWager& bet =
    ostinato::getColosseumWager(ostinato::ItemId::RAGNAROK);
bet.monsterId();     // MonsterId::DIDALOS — the monster fought
bet.prize;           // ItemId::ILLUMINA   — the item won
bet.prizeHidden();   // true — prize name hidden in the wager menu

for (const auto& entry : ostinato::colosseumWagers()) {
    // entry.id     — the wagered ItemId
    // entry.record — ColosseumWager
}
```

## The record

```cpp
struct ColosseumWager {
    std::uint8_t monster;         // +0  MONSTER index (one byte)
    std::uint8_t unused40;        // +1  dead data, always kColosseumUnusedByte
    ItemId prize;                 // +2
    std::uint8_t hidePrizeFlag;   // +3  kHidePrize ($FF) or kShowPrize ($00)

    constexpr MonsterId monsterId() const;
    constexpr bool prizeHidden() const;
};
static_assert(sizeof(ColosseumWager) == 4);
```

One 4-byte record per wagerable item, byte-identical to the ROM's
`ColosseumProp` record. Two quirks of the ROM layout surface here:

- **`monster` is one byte** while `MonsterId` is 16-bit — every monster the
  colosseum fields has an index that fits a byte, so the record stores the
  byte and `monsterId()` types it. The generated rows write it through
  `wagerMonster(MonsterId)`, which performs the narrowing.
- **`unused40` is dead data**: every record carries `$40` there and no game
  code reads the byte. It stays for byte-identity with the ROM table —
  always write `kColosseumUnusedByte`.

## The table and the index space

```cpp
struct ColosseumWagerEntry { ItemId id; ColosseumWager record; };

const ColosseumWager& getColosseumWager(ItemId wagered);   // total: every byte value is a row
std::span<const ColosseumWagerEntry> colosseumWagers();    // all 256, index order
```

The table is indexed by the **wagered** item: betting a Ragnarok looks up row
`ItemId::RAGNAROK`. `ItemId` is `uint8_t`, so the lookup is total. Items the
game treats as unwagerable carry the default row — Chupon as the monster
(who sneezes the party out of the arena) with a prize of `ItemId::ELIXIR`
that the player can never reach through that row.

## Backing data / where to change

Rows live in `src/data/generated/colosseum_prop_data.inc` (included into the
array in `src/data/colosseum_wagers.cpp`). To change what a wager fights or
wins, edit its row's `.monster` (through `wagerMonster(MonsterId::...)`) and
`.prize`; set `.hidePrizeFlag = kHidePrize` to mask the prize name in the
menu (the shipped data hides exactly four: Ragnarok, Striker, Cat Hood, and
Merit Award). A compile-time assert verifies every row's `.id` matches its
array position. A deliberate change must also update the matching row in
`tests/fixtures/colosseum_prop_expected.h` (original ROM values).

## What's tested

`tests/test_colosseum_wagers.cpp` — every one of the 256 records
`memcmp`-compared in full against the fixture (including the `$40` byte on
every record); semantic spot-checks hand-traced from the ROM table (the
default row, a plain wager, and two hidden-prize wagers); and the
`wagerMonster` round-trip.
