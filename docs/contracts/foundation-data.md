# Foundation data — behavioral contract

Behavioral specification for the port's foundation data: the game-domain enum
surface, the character base-stats table, and the RNG table. All citations point
into the pinned reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`);
rip-generated files live under `original-src/src/`. This document is the test
authority for `tests/test_enums.cpp`, `tests/test_character_base.cpp`, and
`tests/test_rng_table.cpp`.

## Sources

| Data | Source | Port surface |
|---|---|---|
| Game-domain enums | `include/const.inc` | 23 headers in `include/ostinato/` |
| Character base stats | `src/field/char_prop.asm` (+ symbol values from `include/const.inc`) | `src/data/character.{h,cpp}` + `src/data/generated/char_prop_data.inc` |
| RNG table | `src/field/rng_tbl.dat` (256-byte binary; label `RNGTbl`, `src/field/rng_tbl.asm:7-11`) | `src/data/rng_table.h` + `src/data/generated/rng_tbl_data.inc` |

## Version posture — all three sources are version-invariant

The upstream tree builds three ROMs (FF6 1.0 J / FF3 1.0 US / FF3 1.1 US) from
one source. For the foundation data, no emitted value differs by version:

- `const.inc`'s only version conditionals are the config axes themselves
  (`LANG_EN_REV1`, `const.inc:30-34`) and a filename-suffix string
  (`LANG_SUFFIX`, `const.inc:37-41`). No enum value is defined under a version
  conditional.
- `char_prop.asm` contains no version conditionals; its only `.if*` directives
  are `.ifnblank` argument-presence checks inside macro definitions.
- `rng_tbl.asm:11` includes `rng_tbl.dat` unconditionally for every version
  target; the table bytes are identical across versions.

## Enum value surface

Every `.enum` in `const.inc` is accounted for: ported, absorbed into a packed
type, or skipped with a stated reason. Enumerator names and values are carried
verbatim; upstream same-value aliases (e.g. `RIGHT_UP = UP_RIGHT`) and
duplicate values (legal in C++) are preserved. Cross-enum value sharing
(e.g. `EVENT_OBJ` members defined as `CHAR::` members) is resolved to the same
values. Underlying width is the smallest of `uint8_t`/`uint16_t` that fits the
enum's maximum value — everything is `uint8_t` except `MonsterId` and
`CharacterFlags` (a 14-bit party bitmask, `const.inc:1416-1431`).

| Upstream | Port type | Notes |
|---|---|---|
| `ITEM` (256) | `ItemId` | `EMPTY = $ff` sentinel |
| `ATTACK` (256) | `AttackId` | unified spell/skill/attack list |
| `MONSTER` (384) | `MonsterId` | `uint16_t` |
| `CHAR` (33 names, values `$00-$0f`) | `CharacterId` | heavily aliased; see index-space section |
| `CHAR_PROP` (64) | `CharacterPropId` | the char_prop record index space, `const.inc:1326-1391` |
| `GENJU` (27) | `EsperId` | |
| `GENJU_BONUS` (18) | `EsperBonus` | `NONE = $ff` |
| `BATTLE_CMD` (31) | `BattleCommandId` | `NONE = $ff` sentinel |
| `DANCE` (8) | `DanceId` | |
| `STATUS_ID` (32) | `StatusId` | sequential `0..31` |
| `ELEMENT` | `Element` | bit values `FIRE = $01 … WATER = $80`, `NONE = 0` |
| `ITEM_TYPE` (7) | `ItemType` | |
| `ITEM_USAGE` | `ItemUsage` | |
| `TARGET` | `TargetFlags` | flags + a 2-bit `INIT_*` sub-field + `MENU = $ff` sentinel; values verbatim incl. duplicates |
| `EVENT_DIR`, `EVENT_OBJ`, `CHAR_GFX` | `EventDir`, `EventObjId`, `CharacterGfxId` | |
| `CHAR_RUN_FACTOR` (5) | `RunFactor` | `const.inc:1394-1400`; incl. `MASK = $03` |
| `CHAR_LEVEL_MOD` (5) | `LevelMod` | `const.inc:1403-1409`; incl. `MASK = $0c` |
| `CHAR_FLAG` (14) | `CharacterFlags` | `uint16_t`; `BIT_0 … BIT_13` |
| `WEAPON_FLAG`, `BATTLE_CMD_FLAG` | `WeaponFlags`, `BattleCommandFlags` | bit values |
| `BATTLE_CHAR_PAL` | `BattleCharacterPalette` | |
| `STATUS1`-`STATUS4` | — | not ported as enums; they define the `StatusSet` byte layout (below) |
| `STATUS12/23/34/14` | — | combined 16-bit views of adjacent status bytes; expressed as `StatusSet` accessors, not types |

Full corpus: 1,311 enumerators across the 23 ported enums, each asserted
against its upstream value by `tests/test_enums.cpp` via the generated fixture
`tests/fixtures/enums_expected.h`.

### Status byte layout (`StatusSet`)

`STATUS_ID` is sequential `0..31`. The four packed status bytes `STATUS1-4`
assign bit `i % 8` of byte `i / 8` to status id `i` — the four 8-bit banks
align exactly with `STATUS_ID` order (4 × 8 = 32). `StatusSet` (four bytes,
`sizeof == 4`) implements `has/set/clear` with that mapping. The emitter
verifies the alignment against `const.inc` on every run and refuses to emit if
it ever breaks.

### Element bits (`ElementSet`)

The eight elements are one bit each in a single byte (`FIRE = $01` through
`WATER = $80`); `ElementSet` (`sizeof == 1`) wraps the byte with `has/set/clear`.

## Character base-stats table

### Index space — `CharacterPropId`, never `CharacterId`

The table has **64 records of 22 bytes**, indexed by the `CHAR_PROP` enum
(`const.inc:1326-1391`): playable roster `$00-$0d`, guests `BANON = $0e` /
`LEO = $0f`, then ghosts, the Narshe-scenario Moogles, story variants
(`TERRA_INTRO`, `SHADOW_COLOSSEUM`, `WEDGE`, `VICKS`), seven Kefka variants
(`$29-$2f`), and named padding/beta slots up to `HO = $3f`.

`CharacterId` (upstream `CHAR`) **cannot index this table**: its 33 names span
only values `$00-$0f` with heavy aliasing (`KUPEK = $02` = CYAN,
`GHOST_1 = $07` = STRAGO, `WEDGE`/`BANON`/`MADUIN` = `$0e`,
`VICKS`/`LEO`/`KEFKA` = `$0f`). The two value spaces agree by name only through
`$0d` and diverge from `$0e` upward.

In the original, the record index arrives from game state — an "actor number"
multiplied by 22 (`$16`) at the access sites:

- `src/battle/battle_main.asm:2311-2317` — `lda $15db,x` (actor number),
  `lda #$16` / `jsr MultAB`, then `lda f:CharProp+10,x` (battle power).
- `src/field/event.asm:1031-1039` — actor number `$ec`, hardware multiply by
  `$16`, then `lda f:CharProp+2,x` (battle commands).

The port therefore exposes `getCharacterBaseStats(CharacterPropId)` and
provides **no** `CharacterId` ↔ `CharacterPropId` conversion — any such mapping
is game-logic scope for the systems that produce actor numbers.

### Record layout — 22 bytes

Field order per the record emitter (`char_prop.asm:34-45`):

| Offset | Field | Value space |
|---|---|---|
| 0-1 | hp, mp | raw magnitudes |
| 2-5 | battle commands ×4 | `BattleCommandId`; `NONE = $ff` |
| 6-14 | strength, agility, stamina, magic power, battle power, defense, magic defense, evade, magic block | raw magnitudes |
| 15-20 | weapon, shield, helmet, armor, relic1, relic2 | `ItemId`; `EMPTY = $ff` |
| 21 | packed traits | below |

The packed trait byte (`char_prop.asm:44`) holds three disjoint fields:
run factor in bits 0-1 (`CHAR_RUN_FACTOR`, mask `$03`), level modifier in bits
2-3 (`CHAR_LEVEL_MOD`, mask `$0c`), and the fixed-equipment flag in bit 4
(`CHAR_PROP_FIXED_EQUIP = $10`, `const.inc:1412`). `CharacterTraits`
(`sizeof == 1`) packs and unpacks exactly these fields.

`CharacterBaseStats` carries this layout with `sizeof == 22`; each record's
object representation is byte-identical to the ROM record.

### Population

40 records are populated; 24 are zero-filled padding emitted by
`empty_char_prop` (`char_prop.asm:3-5`): indices 29 (`$1d`), 34-40
(`$22-$28`), and 48-63 (`$30-$3f`). Zero-filled means all 22 bytes are `0` —
distinct from the `$ff` `EMPTY`/`NONE` sentinels populated records use for
empty slots.

Full corpus: all 64 records are compared byte-for-byte against the generated
fixture `tests/fixtures/char_prop_expected.h` by `tests/test_character_base.cpp`.

## RNG table

`rng_tbl.dat` is 256 raw bytes (label `RNGTbl`, ROM `c0/fd00`); the byte values
are the whole contract — game systems read `RNGTbl + index`. The port stores
one `{ index, value }` entry per byte in `kRngTable` (index equals array
position, checked at compile time) and exposes `rngByte(index)`; `index` is
`uint8_t`, matching the original's one-byte table offset. Consumers (battle
and field RNG routines) are ported in later phases.

Full corpus: all 256 entries are compared against the generated fixture
`tests/fixtures/rng_tbl_expected.h` by `tests/test_rng_table.cpp`.

## Regenerating the generated artifacts

Each generated file names its emitter and exact command line in its header.
The emitters live in `tools/asm_parser/` (`parse_const_enums.py`,
`parse_char_prop.py`, `parse_rng_tbl.py`), read only the pinned
`original-src/` tree, and verify their structural expectations (enum coverage,
status-layout alignment, record sizes, table length) on every run, refusing to
emit on any deviation. Their unit tests (`test_parse_*.py`) run locally via
`python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'` and on
CI in the `parser-tests` job.
