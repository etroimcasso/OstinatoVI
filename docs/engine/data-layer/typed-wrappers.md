# Typed wrappers

## Public surface

```cpp
#include "ostinato/flag_set.h"          // FlagSet<F>
#include "ostinato/element_set.h"       // ElementSet
#include "ostinato/status_set.h"        // StatusSet
#include "ostinato/targeting.h"         // Targeting
#include "ostinato/attack_flags.h"      // AttackTrait/AttackFlag1/AttackFlag2/AttackMiscFlag
                                        //   + AttackTraitSet/AttackFlags1/AttackFlags2/AttackMiscFlags
#include "ostinato/character_traits.h"  // CharacterTraits
```

These are the hand-written value types over packed ROM bytes. Each one exists
because a single ROM byte (or four, for statuses) encodes multiple meanings — bit
flags, sub-fields, or bit-mapped set membership — that a bare integer surfaces
poorly at the call site. Every wrapper is `sizeof`-locked to the exact ROM byte
count it represents (`static_assert` at the definition site), so a record composed
of wrappers stays byte-identical to the ROM record it mirrors.

## `FlagSet<F>` — generic one-byte flag set

```cpp
template <typename FlagT>
struct FlagSet {
    std::uint8_t bits = 0;

    constexpr bool has(FlagT flag) const;
    constexpr void set(FlagT flag);
    constexpr void clear(FlagT flag);
    static constexpr FlagSet of(auto... flags);  // FlagSet<F>::of(F::A, F::B)
};
```

A one-byte set over any bit-valued enum. The enum carries the ROM bit values;
`FlagSet` encapsulates the mask math so no call site open-codes `&`/`|`. `of(...)`
is the builder table rows use — zero arguments yields the empty set (an all-zero
ROM byte). Instantiate it for any new bit-flag byte rather than writing another
bespoke wrapper. (`ElementSet` and `StatusSet` predate the template and stay as
they are.)

## `ElementSet` — the elemental-affinity byte

```cpp
struct ElementSet {
    std::uint8_t bits = 0;
    constexpr bool has(Element element) const;
    constexpr void set(Element element);
    constexpr void clear(Element element);
    static constexpr ElementSet of(auto... elements);
};
static_assert(sizeof(ElementSet) == 1);
```

One byte, one bit per element (`Element` carries the bit values, `FIRE=0x01 ..
WATER=0x80`). `Element::NONE == 0`, so `has(NONE)` is always false — the "no
elements" reading of an all-zero affinity byte matches the original's meaning.

## `StatusSet` — the four status bytes

```cpp
struct StatusSet {
    std::array<std::uint8_t, 4> bytes{};
    constexpr bool has(StatusId id) const;
    constexpr void set(StatusId id);
    constexpr void clear(StatusId id);
    static constexpr StatusSet of(auto... ids);
};
static_assert(sizeof(StatusSet) == 4);
```

The 32 status effects packed into four bytes. `StatusId` is the sequential `0..31`
ordinal; the wrapper maps id → (byte `id / 8`, bit `id % 8`). This type is the sole
owner of that packing rule — the original's combined 16-bit status views are
expressible as accessors here rather than separate constants, and no consumer does
the byte/bit split by hand. Example: `StatusId::SLEEP` (15) lives at byte 1,
bit 7.

## `Targeting` — the attack targeting byte

```cpp
struct Targeting {
    std::uint8_t bits = 0;
    constexpr Targeting() = default;
    explicit constexpr Targeting(std::uint8_t raw);
    static constexpr Targeting of(auto... flags);   // over TargetFlags
};
static_assert(sizeof(Targeting) == 1);
```

A one-byte carrier over the `TargetFlags` bit values — deliberately **without**
semantic read accessors. The byte embeds a two-bit initial-cursor sub-field
(`INIT_SINGLE/ALL/GROUP/HALF`) plus the whole-byte `MENU = 0xFF` sentinel, and a
correct read-side decomposition needs the battle targeting consumer's context; the
read-side wrapper lands with that consumer. Until then the byte is stored verbatim
and composed via `of(...)`, where `INIT_HALF == INIT_ALL | INIT_GROUP` and `MENU`
are plain values.

## Attack flag enums — the four flag bytes of an attack record

```cpp
enum class AttackTrait    : std::uint8_t { PHYSICAL = 0x01, /* ... */ NO_CHARACTER_TARGET = 0x80 };
enum class AttackFlag1    : std::uint8_t { USABLE_ON_FIELD = 0x01, /* ... */ AFFECT_MP = 0x80 };
enum class AttackFlag2    : std::uint8_t { RESTORE_HP_MP = 0x01, /* ... */ FRACTIONAL_DAMAGE = 0x80 };
enum class AttackMiscFlag : std::uint8_t { MISS_IF_STATUS_IMMUNE = 0x01, SHOW_ATTACK_MESSAGE = 0x02 };

using AttackTraitSet  = FlagSet<AttackTrait>;
using AttackFlags1    = FlagSet<AttackFlag1>;
using AttackFlags2    = FlagSet<AttackFlag2>;
using AttackMiscFlags = FlagSet<AttackMiscFlag>;
```

The four flag bytes of a 14-byte attack-properties record
([attack-properties.md](attack-properties.md)). The disassembly has no named
symbols for these bits — their meanings come from the annotated RAM map
(`original-src/notes/battle-ram.txt`), cited per enum in the header. Two bytes
(`AttackTrait`, `AttackMiscFlag`) are unnamed upstream and carry descriptive port
names; the other two use the RAM map's own "Attack flags 1/2" naming. Two bits the
RAM map itself marks uncertain (`AttackFlag1::QUICK_WARP`,
`AttackFlag2::STAMINA_DEFENSE`) keep that uncertainty note in the header comment.

## `CharacterTraits` — the packed character trait byte

```cpp
struct CharacterTraits {
    std::uint8_t packed = 0;

    constexpr CharacterTraits() = default;
    constexpr explicit CharacterTraits(std::uint8_t raw);           // exact ROM byte
    constexpr CharacterTraits(RunFactor, LevelMod, bool fixedEquip); // component form

    constexpr RunFactor runFactor() const;   // bits 0-1
    constexpr LevelMod  levelMod() const;    // bits 2-3
    constexpr bool      fixedEquip() const;  // bit 4
};
static_assert(sizeof(CharacterTraits) == 1);
```

The final byte of a 22-byte character record ([characters.md](characters.md)):
run-away speed (bits 0–1), level-averaging modifier (bits 2–3), and the
fixed-equipment flag (bit 4) in one byte. `RunFactor` and `LevelMod` enumerators
carry their values pre-shifted into position, so the component constructor is a
plain OR — it packs to exactly the byte the original assembler emits, and each
accessor masks its field back out. Table rows use the component form
(`{ RunFactor::NORMAL, LevelMod::NORMAL, false }`); the byte-in constructor exists
for code that round-trips a raw ROM byte.

## Battle-table wrappers — one byte each

Four small wrappers carry single bytes the [battle tables](battle-tables.md) and
[level progression](level-up.md) key on. Each is `sizeof == 1`, constructed
explicitly from its byte, and exposes only reads that are pinned to what the
engine does with it.

```cpp
#include "ostinato/random_threshold.h"        // RandomThreshold
#include "ostinato/battle_slot_mask.h"        // BattleSlotMask
#include "ostinato/item_type_battle_flags.h"  // ItemTypeBattleFlags
#include "ostinato/ability_learned_set.h"     // AbilityLearnedSet

struct RandomThreshold { std::uint8_t bits; constexpr std::uint8_t value() const; };
struct BattleSlotMask  { std::uint8_t bits; constexpr bool has(std::uint8_t slot) const; };
struct ItemTypeBattleFlags {
    std::uint8_t bits;
    constexpr std::uint8_t mergedBits() const;          // bits << 1
    constexpr bool         skipsEquippableCheck() const; // bit 7
};
struct AbilityLearnedSet {
    std::uint8_t bits;
    constexpr bool has(std::uint8_t slot) const;
    constexpr int  count() const;
};
```

- `RandomThreshold` — one rung of a probability ladder. A roll passes the rung
  when `roll >= value()`, so an outcome's share is the gap to the next rung.
- `BattleSlotMask` — a set of battle slots, one bit per slot: `$0F` is every
  character, `$3F` every monster.
- `ItemTypeBattleFlags` — the battle-usability bits an item's type contributes.
- `AbilityLearnedSet` — which swdtechs or blitzes a character has learned, in
  teaching order. Abilities are learned in order, so every reachable value is a
  run of low bits.

## What's tested

`tests/test_enums.cpp` exercises `ElementSet` and `StatusSet` bit mapping
(including cross-byte status boundaries) against exact byte expectations;
`tests/test_attack_properties.cpp` round-trips every builder family
(`Targeting::of`, `ElementSet::of`, `FlagSet::of`, `StatusSet::of`) back to raw ROM
bytes; `tests/test_character_base.cpp` exercises the `CharacterTraits` accessors on
records with distinct packed values.
