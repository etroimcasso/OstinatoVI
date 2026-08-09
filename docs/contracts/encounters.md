# Field encounters — behavioral contract

Behavioral specification for the port's field-encounter data: the two battle-
group families (`rand_battle_group`, `event_battle_group`), the two group-index
tables (`world_battle_group`, `sub_battle_group`), the two rate tables
(`world_battle_rate`, `sub_battle_rate`), and the five inline tables in
`field/battle.asm` that drive world-map encounter selection. All citations point
into the pinned reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`);
rip-generated files live under `original-src/src/`. This document is the test
authority for `tests/test_encounter_data.cpp`.

## Sources

| Data | Source | Port surface |
|---|---|---|
| Random battle groups | `src/field/rand_battle_group.dat` (2,048 B; `RandBattleGroup`, `field/battle.asm:510`) | `src/data/encounters.{h,cpp}` + `generated/rand_battle_group_data.inc` |
| Event battle groups | `src/field/event_battle_group.dat` (1,024 B; `EventBattleGroup`, `:514`) | `generated/event_battle_group_data.inc` |
| World battle groups | `src/field/world_battle_group.dat` (512 B; `WorldBattleGroup`, `:518`) | `generated/world_battle_group_data.inc` |
| Sub (map) battle groups | `src/field/sub_battle_group.dat` (512 B; `SubBattleGroup`, `:522`) | `generated/sub_battle_group_data.inc` |
| World battle rates | `src/field/world_battle_rate.dat` (128 B; `WorldBattleRate`, `:526`) | `generated/world_battle_rate_data.inc` |
| Sub (map) battle rates | `src/field/sub_battle_rate.dat` (128 B; `SubBattleRate`, `:530`) | `generated/sub_battle_rate_data.inc` |
| Inline BG / rate tables | `field/battle.asm:219-263` (5 tables) | `generated/encounter_bg_tables_data.inc` |
| Battle backgrounds | `include/gfx/battle_bg.inc` (`.enum BATTLE_BG`) | `include/ostinato/battle_background_id.h` |

## Version posture

Every table here is version-invariant: each rips to a single un-suffixed file
shared by all supported ROMs, with no version conditionals around any
declaration. No language dispatch axis and no version-pinned deferrals apply.

## Battle groups (`RandBattleGroup` / `EventBattleGroup`)

Each group is a fixed list of candidate **formation words**: `RandBattleGroup`
is 256 groups × 4 words; `EventBattleGroup` is 256 groups × 2 words. A formation
word is a `FormationRef` — bits 0-14 the formation index (`< 576`), bit 15 the
"add a random 0-3 to the index at load" flag (`LoadBattleProp`,
`battle_main.asm:7956-7965`).

Selection (consumer behavior, implemented later; recorded here as the
contract for those routines):

- `CheckBattleWorld` / `CheckBattleSub` (`field/battle.asm:97` / `:319`) roll
  `UpdateBattleGrpRng` and pick the rand-group slot at **80/80/80/16-in-256**
  odds (`:187-196` / `:397-406`): `< $50` → slot 0, `< $A0` → slot 1, `< $F0` →
  slot 2, else slot 3.
- Event battles pick between the two event-group words at **3/4-1/4** (`$C0`)
  odds (`field/event.asm:1910`).
- Only rand group 112's four words set the randomize (bit 15) flag; no event
  word does.

## World / sub battle groups (`WorldBattleGroup` / `SubBattleGroup`)

Both are flat byte tables of **rand-group indices**.

- `WorldBattleGroup` (512 B): indexed `world*256 + (Y & $E0) + ((X>>3) & $1C) +
  bgGroup`, i.e. `world*256 + ySector*32 + xSector*4 + bgGroup`
  (`field/battle.asm:120-135`), where `ySector`/`xSector` are the high 3 bits of
  the party's map Y/X (0-7) and `bgGroup` (0-3) comes from `BattleBGGroupTbl`. A
  value of `$FF` marks a **Veldt sector** — no ROM group; the formation is
  chosen from the RAM encounter list by `GetVeldtBattle` (`:269`). 28 sectors
  are Veldt sectors.
- `SubBattleGroup` (512 B): the rand-group index for each map id 0-511
  (`$0082`, `:391`).

## World / sub battle rates (`WorldBattleRate` / `SubBattleRate`)

Both pack **four 2-bit rate classes per byte**.

- `WorldBattleRate` (128 B): the byte at `world*64 + sector` (`sector` = the
  sector bits `>> 2`, 0-63); the class is the `rateSlot`-th 2-bit field, where
  `rateSlot` (0-3) comes from `BattleBGRateTbl` (`:142-153`). Classes: `0`
  normal, `1` low, `2` high, `3` none (`:250`).
- `SubBattleRate` (128 B): the byte at `mapId >> 2`; the class is the
  `(mapId & 3)`-th 2-bit field (`:353-367`). Classes: `0` normal, `1` low, `2`
  high, `3` very high (`:258`).

Class `3` is never selected corpus-wide (the data uses 0-2); the parser
hard-errors if any 2-bit field is `3`. The enums still define the fourth class
for completeness.

## Inline tables (`field/battle.asm`)

| Table | Shape | Meaning |
|---|---|---|
| `WorldBattleBGTbl` (`:219`) | 16 `BATTLE_BG` bytes | battle background per `[world][slot]` (8 slots/world) |
| `BattleBGRateTbl` (`:242`) | 8 bytes (0-3) | which 2-bit field of the world rate byte each background reads |
| `BattleBGGroupTbl` (`:246`) | 8 bytes (0-3) | the bg-group offset each background adds when picking a world group |
| `WorldBattleRateTbl` (`:251`) | 4 charm states × 4 classes, 16-bit | random-battle counter increment per world rate class |
| `SubBattleRateTbl` (`:259`) | 4 charm states × 4 classes, 16-bit | random-battle counter increment per map rate class |

The rate-increment tables hold **magnitudes** (added to the step counter
`$1F6E`, ceiling `$FF00`, `:166-172` / `:378-383`) — the port stores them as
decimal `std::uint16_t`, not raw bytes. Charm state comes from `$11DF`: `0`
none, `1` Charm Bangle (halves the rate), `2` Moogle Charm (zeroes it), `3`
unused.

## Battle backgrounds (`BATTLE_BG`)

`include/gfx/battle_bg.inc`'s `.enum BATTLE_BG` — 56 sequential backgrounds
(`FIELD_WOB = 0` … `TENTACLES = $37`) plus `DEFAULT = $FF`. Ported with full
token preservation as `BattleBackgroundId`; `WorldBattleBGTbl`'s entries name
these enumerators.

## Quirks (ported as contract notes; none change data shape)

1. **World-side event-battle roll uses the game-time frame counter**
   (`world/event.asm:433` `cmp #$2d` against `$021E`; likewise
   `train_script.asm` ×3) where the field side uses `UpdateBattleGrpRng`.
   Consumer behavior (implemented later); contract note.
2. **`world/ctrl.asm:432`** reads `EventBattleGroup` word 0 of group 93 at a
   fixed offset (`#$0174`) — that word is formation 463. Consumer note.
3. **Veldt formation selection** builds `formation = availableSlot*8 + bit` from
   the RAM encounter list (`GetVeldtBattle`), with no extra ROM table; `$FF` in
   `world_battle_group` is the trigger sentinel.

## Negative coverage (absences are contract)

- These files are consumed only by `field/battle.asm`, `field/event.asm`, and
  `world/{event,ctrl,train_script}.asm`; no other table references them.
- Rate class `3` is unused corpus-wide (asserted by the parser); the randomize
  (bit 15) flag appears only on rand group 112's four words and on no event
  word.

## What's tested

`tests/test_encounter_data.cpp` verifies every entry of all six tables and the
five inline tables against its ROM-byte fixture (full corpus, no subsets), with
each entry's identity field matching its position; it hand-traces the World of
Balance sector-0 backgrounds (9/11/12/13), map 32's group (189), the `$55`
world-rate byte (all LOW) and the `$04` map-rate byte (only map 41 LOW), the 28
Veldt sectors, event group 93's first formation (463), and the rand-group-112
randomize flag; and it confirms the rate increments are decimal magnitudes.
