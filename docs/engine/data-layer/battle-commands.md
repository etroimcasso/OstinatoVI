# Battle commands — properties and the command-keyed tables

Every player battle command — FIGHT, MAGIC, STEAL, BLITZ, and the rest — has a
row of properties and appears in a handful of small tables the battle engine
consults: which commands a status still lets you use, which re-pick their target,
which carry an attack number, how long each delays the ATB gauge, and how a relic
swaps one command for another. This file covers all of that data and its
accessors.

There are 30 real commands, `BattleCommandId::FIGHT` (0) through
`BattleCommandId::MAGITEK` (29). Every table here is a simple mapping keyed by
`BattleCommandId` — the command is the identity. The *dispatch* side (the jump
tables that actually run a command, roll its random handler, or initialize its
targeting) is the runtime battle engine and is not part of this data layer.

## Public surface

```cpp
#include "data/battle_commands.h"

using ostinato::BattleCommandId;

// A command's properties: who can use it, and its default targeting mode.
const auto& fight = ostinato::battleCommandProperties(BattleCommandId::FIGHT);
fight.flags.has(ostinato::BattleCommandFlags::GOGO);   // usable by Gogo?
fight.targeting;                                        // a Targeting byte

// The ATB advance-wait a command adds, in ticks.
ostinato::commandAdvanceWait(BattleCommandId::JUMP);    // 224

// The packed targeting-init byte a command starts from.
ostinato::commandTargetingInit(BattleCommandId::SKETCH);  // {0x80}
```

`battleCommandProperties()` (no argument) returns a span of all 30 entries in
command-id order.

## Command properties

```cpp
struct BattleCommandProperties {
    FlagSet<BattleCommandFlags> flags;   // who can use it (Gogo/Mimic/Imp)
    Targeting                   targeting;  // default targeting mode
};                                          // sizeof 2 — the two ROM bytes

struct BattleCommandPropertiesEntry { BattleCommandId command; BattleCommandProperties record; };
```

`flags` is a [`FlagSet`](typed-wrappers.md) over `BattleCommandFlags` — `GOGO`
(the command can be copied by Gogo), `MIMIC` (mimickable), `IMP` (usable while an
imp), and `UNKNOWN`. `targeting` is a [`Targeting`](typed-wrappers.md) byte
holding the default cursor mode (manual/auto, one target vs. group, default side,
the `MENU`/`SELF` sentinels).

## Command-membership masks

```cpp
inline constexpr BattleCommandSet kConfusedAllowedCommands;  // muddled / charmed / colosseum
inline constexpr BattleCommandSet kBerserkAllowedCommands;   // berserk / zombie
inline constexpr BattleCommandSet kRetargetCommands;         // re-pick target after selection
```

A `BattleCommandSet` is a four-byte set — one bit per command — with a single
query:

```cpp
kConfusedAllowedCommands.has(BattleCommandId::FIGHT);   // true
kRetargetCommands.has(BattleCommandId::DANCE);          // true
```

The bit layout is the ROM's `GetBitPtr` order: command *n* lives in byte *n*/8,
bit 1&nbsp;<<&nbsp;(*n*%8). You never need the layout to *use* the set — `has()`
hides it — but it is why the four raw bytes are what they are, and the tests
check membership both ways.

## Command-id lists

Three plain lists name the commands that get special handling; each entry names
its own command, so the list reads directly:

```cpp
inline constexpr std::array<BattleCommandId, 10> kRandomHandlerCommands;  // special random-use handler
inline constexpr std::array<BattleCommandId, 8>  kUpdateStateCommands;    // enabled-state update handler
inline constexpr std::array<BattleCommandId, 6>  kInitFunctionCommands;   // init function on setup
```

`kRandomHandlerCommands` is walked by the random-command consumer in interleaved
even/odd pairs (Gogo and Mimic pick a random command and dispatch its handler);
this layer is the flat list, the pairing is consumer math.

## Per-command mappings

Two tables map every real command to a value:

```cpp
struct CommandAdvanceWaitEntry   { BattleCommandId command; std::uint8_t wait; };
struct CommandTargetingInitEntry { BattleCommandId command; CommandTargetingInit init; };

inline constexpr std::array<CommandAdvanceWaitEntry, 30>   kCommandAdvanceWait;
inline constexpr std::array<CommandTargetingInitEntry, 30> kCommandTargetingInit;
```

- `kCommandAdvanceWait` is the ATB advance-wait (in ticks, a decimal magnitude)
  the command adds before its action runs — JUMP's 224 is the visible outlier.
- `kCommandTargetingInit` is the command's packed targeting-init byte, wrapped in
  `CommandTargetingInit`. That byte splits into three non-overlapping fields
  (`$E1 | $18 | $06 == $FF`): `directBits()` are copied straight into the
  targeting work byte, `initialTarget()` selects the initial cursor target, and
  `dispatchIndex()` selects an init-target handler:

  ```cpp
  auto t = ostinato::commandTargetingInit(BattleCommandId::MORPH);  // {0x18}
  t.initialTarget();  // 0x18
  t.directBits();     // 0x00
  ```

  The concrete meaning of the individual `directBits` flags belongs to the
  runtime targeting consumer.

## Pair tables

Two tables pair each command with a second named value:

```cpp
struct RelicCommandSwap  { BattleCommandId from; BattleCommandId to; };
struct CommandAttackBase { BattleCommandId command; AttackId attackBase; };

inline constexpr std::array<RelicCommandSwap, 5>  kRelicCommandSwaps;
inline constexpr std::array<CommandAttackBase, 5> kCommandAttackBases;
```

- `kRelicCommandSwaps` — a relic replaces `from` with `to` (steal→capture,
  slot→gp rain, sketch→control, magic→x-magic, fight→jump).
- `kCommandAttackBases` — the base `AttackId` an attack-carrying command counts
  up from (summon→Ramuh, lore→Condemned, magitek→Fire Beam, blitz→Pummel,
  swdtech→Dispatch).

## Backing data / where to change

Rows live in `src/data/generated/` — `battle_cmd_prop_data.inc` (the property
records, `#include`d into the array in `src/data/battle_commands.cpp`) and
`battle_cmd_tables_data.inc` (the satellite tables, `#include`d at namespace
scope in `battle_commands.h`). Every value is named:

```cpp
    { BattleCommandId::FIGHT, BattleCommandProperties{
        FlagSet<BattleCommandFlags>::of(BattleCommandFlags::GOGO, BattleCommandFlags::MIMIC,
                                        BattleCommandFlags::IMP, BattleCommandFlags::UNKNOWN),
        Targeting::of(TargetFlags::MANUAL, TargetFlags::INIT_SINGLE, TargetFlags::ENEMY) } },
```

To change who can use a command or how it targets by default, edit its `flags` /
`targeting` builders; to retune an ATB delay, edit its `wait`; to change a relic
swap, edit its `from`/`to`. A deliberate change must also update the matching
entry in the fixtures under `tests/fixtures/` (`battle_cmd_prop_expected.h`,
`battle_cmd_tables_expected.h`), which hold the original ROM bytes.

The ROM's command-properties and command-delay tables have 32 slots; the last
two (`$1E`/`$1F`) are unused `NONE`/`MENU` padding with no command, so they are
not modeled here — the fixtures still verify their raw bytes.

## What's tested

`tests/test_battle_commands.cpp` — every property record and every satellite
table compared in full against its fixture (no subsets), the two padding rows
verified as unused, the `GetBitPtr`-order membership on all three masks, the
`CommandTargetingInit` field decode, and the accessors hand-traced to their ROM
values.
