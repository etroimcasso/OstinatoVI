# Foundational enums

## Public surface

```cpp
#include "ostinato/game_version.h"      // GameVersion, Language, language(), isRevision1()
#include "ostinato/character_id.h"      // CharacterId
#include "ostinato/character_prop_id.h" // CharacterPropId
#include "ostinato/character_flags.h"   // CharacterFlags
#include "ostinato/character_gfx_id.h"  // CharacterGfxId
#include "ostinato/battle_character_palette.h" // BattleCharacterPalette
#include "ostinato/battle_command_id.h" // BattleCommandId
#include "ostinato/battle_command_flags.h" // BattleCommandFlags
#include "ostinato/attack_id.h"         // AttackId
#include "ostinato/monster_id.h"        // MonsterId
#include "ostinato/item_id.h"           // ItemId
#include "ostinato/item_type.h"         // ItemType
#include "ostinato/item_usage.h"        // ItemUsage
#include "ostinato/weapon_flags.h"      // WeaponFlags
#include "ostinato/status_id.h"         // StatusId
#include "ostinato/element.h"           // Element
#include "ostinato/target_flags.h"      // TargetFlags
#include "ostinato/dance_id.h"          // DanceId
#include "ostinato/esper_id.h"          // EsperId
#include "ostinato/esper_bonus.h"       // EsperBonus
#include "ostinato/run_factor.h"        // RunFactor
#include "ostinato/level_mod.h"         // LevelMod
#include "ostinato/event_dir.h"         // EventDir
#include "ostinato/event_obj_id.h"      // EventObjId
```

Every game-domain identity is an `enum class` in namespace `ostinato`, each
enumerator carrying the exact ROM byte value. These are the typed vocabulary every
other data-layer surface composes: a character record's command slots are
`BattleCommandId`, its equipment slots are `ItemId`, an esper's teachable spells are
`AttackId`, and so on. Magic numbers don't appear at any consumer; the enum is
always the call-site surface.

All enums are `: std::uint8_t` except `CharacterFlags` (`std::uint16_t`, a 14-bit
mask) and `MonsterId` (`std::uint16_t`, 384 values). Headers carrying the
`AUTO-GENERATED` banner mirror a `.enum` block in the disassembly's
`include/const.inc`; `game_version.h` is hand-written (its values are port-chosen,
not ROM bytes — see below).

## `GameVersion` / `Language` — the runtime version axis

```cpp
enum class GameVersion : std::uint8_t { JP_1_0, US_1_0, US_1_1 };
enum class Language    : std::uint8_t { JP, EN };

constexpr Language language(GameVersion version);      // JP_1_0 → JP, else EN
constexpr bool     isRevision1(GameVersion version);   // true only for US_1_1
```

One binary serves all three supported ROM revisions — Final Fantasy VI (J) 1.0 and
Final Fantasy III (US) 1.0 / 1.1. Version-conditional data and behavior key off
these predicates rather than build-time configuration. The enumerator values are
ordering-stable but arbitrary and are never persisted as-is; a save or asset pack
records the identified revision explicitly. ROM identification (CRC matching) is
the extraction tool's concern, not this header's.

## Character identity — four distinct index spaces

Four enums name characters, and they are **not interchangeable**:

- **`CharacterId`** — the 16-slot actor space (`0x00..0x0F`) game state uses.
  Heavily aliased: the story cast occupies `TERRA=0x00 .. UMARO=0x0D`, and the
  temporary/guest casts reuse the same bytes (`KUPEK==CYAN==0x02`,
  `BANON==WEDGE==MADUIN==0x0E`, `KEFKA==LEO==VICKS==0x0F`, …). Which name applies
  is scenario context, not a property of the byte.
- **`CharacterPropId`** — the 64-record index space (`0x00..0x3F`) of the character
  base-stats table, alias-free: every guest, ghost, moogle, Kefka variant, and
  placeholder gets its own record slot ([characters.md](characters.md)).
- **`CharacterGfxId`** — sprite-graphics identity (`0x00..0x16`); adds
  non-party sprites (`SOLDIER`, `IMP`, `ESPER_TERRA`, `GESTAHL`, …).
- **`CharacterFlags`** — a 14-bit membership mask (`TERRA=0x0001 ..
  UMARO=0x2000`), one bit per permanent party member, for "who is available"
  state.

`BattleCharacterPalette` maps each cast member to one of the seven battle palette
slots (`0x00..0x06`); it's many-to-one by design (Edgar, Sabin, Celes, and four
others share palette `0x00`).

`EventObjId` extends the `CharacterId` space for the event system: the same aliased
`0x00..0x0F` actor bytes, then `NPC_1..NPC_32` (`0x10..0x2F`), `CAMERA` (`0x30`),
and `SLOT_1..SLOT_4` (`0x31..0x34`). `EventDir` is the 16-direction facing/movement
vocabulary events use (four cardinals, four diagonals with both spelling aliases,
eight two-step combinations).

## `AttackId` — the unified 256-value attack space

```cpp
enum class AttackId : std::uint8_t {
    FIRE = 0x00,           // magic spells: 0x00..0x35
    RAMUH = 0x36,          // esper summons: 0x36..0x50 (matches EsperId)
    FIRE_SKEAN = 0x51,     // then skean throws, SwdTech, Blitz, dance attacks,
    // ...                 // Lores, tools, magitek, enemy attacks, desperations
    LAGOMORPH = 0xFE,
    NONE = 0xFF,           // empty-slot sentinel
};
```

Everything a battler can perform lives in one index space: the 54 menu spells
(`0x00..0x35`), the 27 esper summons (`0x36..0x50`, byte-identical to `EsperId`),
and onward through skean throws, SwdTech, Blitz, dance attacks, Rage/Lore/enemy
attacks, tool and magitek attacks, and the desperation attacks, ending at
`LAGOMORPH=0xFE` with `NONE=0xFF` as the empty-slot sentinel. The
attack-properties table ([attack-properties.md](attack-properties.md)) has one
record per value. Spell names follow the US SNES localization (`BOLT`, `PEARL`,
`ANTDOT`, `BSERK`).

## `MonsterId` — 384 monsters, 16-bit

`GUARD=0x0000 .. COLOSSEUM=0x017F`. The only enum whose ROM index space exceeds a
byte. Includes every boss variant and event dummy; two slots without stable names
are `MONSTER_0177`-style placeholders preserving the index layout.

## `ItemId` — 256 items

`DIRK=0x00 .. DRIED_MEAT=0xFE`, `EMPTY=0xFF`. One contiguous byte space covering
weapons, armor, shields, helmets, relics, tools, and consumables; `EMPTY` is the
empty-slot sentinel equipment fields use. `ItemType` classifies an item into one of
seven classes (`TOOL=0x00 .. CONSUMABLE=0x06`); `ItemUsage` (`THROW=0x10`,
`BATTLE=0x20`, `MENU=0x40`) and `WeaponFlags` (`SWDTECH=0x02`, `BACK_ROW=0x20`,
`TWO_HAND=0x40`, `RUNIC=0x80`) are bit-valued and appear inside packed bytes of the
item table when it lands.

## `StatusId` — the 32 status effects

`BLIND=0x00 .. FLOAT=0x1F`, sequential. This is the *ordinal* identity of a status
— bit position `id % 8` of byte `id / 8` in the four packed status bytes. The
packing lives in `StatusSet` ([typed-wrappers.md](typed-wrappers.md)); consumers
never do that bit math by hand.

## `Element` — the 8 elements, bit-valued

`FIRE=0x01, ICE=0x02, LIGHTNING=0x04, POISON=0x08, WIND=0x10, HOLY=0x20,
EARTH=0x40, WATER=0x80, NONE=0x00`. Values are one-hot bits; a set of affinities is
an `ElementSet` byte ([typed-wrappers.md](typed-wrappers.md)).

## `TargetFlags` — the packed targeting byte's vocabulary

Bit values for the attack targeting byte: `MANUAL=0x01`, `ONE_SIDE=0x02` (aliased
by `SELF`), the two-bit initial-cursor sub-field `INIT_SINGLE/ALL/GROUP/HALF`
(`0x00/0x04/0x08/0x0C`, `INIT_MASK=0x0C`), `AUTO_CONFIRM=0x10`,
`MULTI_TARGET=0x20`, `ENEMY=0x40`, `ROULETTE=0x80`, and the whole-byte sentinel
`MENU=0xFF`. Composed via the `Targeting` carrier type
([typed-wrappers.md](typed-wrappers.md)).

## Battle commands, dances, espers

- **`BattleCommandId`** — the 30 battle commands (`FIGHT=0x00 .. MAGITEK=0x1D`)
  plus `NONE=0xFF`, the empty-command-slot sentinel character records use.
  `BattleCommandFlags` (`GOGO=0x01`, `MIMIC=0x02`, `IMP=0x04`, `UNKNOWN=0x08`) are
  the bit-valued command-availability flags.
- **`DanceId`** — the 8 dances, `WIND_SONG=0x00 .. SNOWMAN_JAZZ=0x07`
  ([dances.md](dances.md)).
- **`EsperId`** — the 27 espers, `RAMUH=0x36 .. PHOENIX=0x50`. Deliberately **not**
  zero-based: esper identity lives inside the unified `AttackId` space, so
  `static_cast<AttackId>(esperId)` is the esper's summon attack. Table lookups
  subtract `EsperId::RAMUH` ([espers.md](espers.md)).
- **`EsperBonus`** — the 17 level-up bonuses (`HP_10=0x00 .. MAGPWR_2=0x10`) plus
  `NONE=0xFF` for espers granting no bonus.

## `RunFactor` / `LevelMod` — trait sub-fields, values in place

The two 2-bit sub-fields of the character trait byte carry their values already
shifted into position: `RunFactor` occupies bits 0–1 (`HIGH=0x00 .. VERY_LOW=0x03`)
and `LevelMod` bits 2–3 (`NORMAL=0x00, HIGH=0x04, VERY_HIGH=0x08, LOW=0x0C`), each
with a `MASK` enumerator. Packing them is a plain OR — see `CharacterTraits` in
[typed-wrappers.md](typed-wrappers.md).

## Regenerating

Each `AUTO-GENERATED` header names its source `.enum` block and the script that
emits it (`tools/asm_parser/parse_const_enums.py`). Hand edits to those headers are
lost on regeneration — change the game by changing table data, not identity bytes.

## What's tested

`tests/test_enums.cpp` asserts every emitted enumerator's byte value — all 1311
across the 23 generated headers — against the generated fixture
(`tests/fixtures/enums_expected.h`), plus the `GameVersion` predicates and the
`ElementSet` / `StatusSet` bit mappings. The generator's own tests under
`tools/asm_parser/` verify the extraction end-to-end against the disassembly
checkout.
