# Monster data — behavioral contract

Behavioral specification for the port's monster-family data: the 32-byte
monster-properties record, the metamorph pack and rate tables, and the six
satellite tables (steal/drop items, rage, sketch, control, special-attack
animation, vertical alignment). All citations point into the pinned reference
tree `original-src/` (everything8215/ff6 @ `1ea47b5`); rip-generated files
live under `original-src/src/`. This document is the test authority for
`tests/test_monster_properties.cpp` and `tests/test_monster_tables.cpp`.

## Sources

| Data | Source | Port surface |
|---|---|---|
| Monster properties | `src/battle/monster_prop.dat` (12,288-byte binary; label `MonsterProp`) | `src/data/monster_properties.{h,cpp}` + `src/data/generated/monster_prop_data.inc` |
| Metamorph packs | `src/battle/metamorph_prop.dat` (128-byte binary; label `MetamorphProp`) | `src/data/metamorph.{h,cpp}` + `src/data/generated/metamorph_prop_data.inc` |
| Metamorph rates | `src/battle/battle_main.asm:10008-10009` (label `MetamorphRateTbl`) | `src/data/metamorph.{h,cpp}` + `src/data/generated/metamorph_rate_data.inc` |
| Steal/drop items | `src/battle/monster_items.asm` (384 macro rows) | `src/data/monster_items.{h,cpp}` |
| Rage attacks | `src/battle/monster_rage.asm` (256 macro rows) | `src/data/monster_attacks.{h,cpp}` |
| Sketch attacks | `src/battle/monster_sketch.asm` (384 macro rows) | `src/data/monster_attacks.{h,cpp}` |
| Control attacks | `src/battle/monster_control.asm` (384 macro rows) | `src/data/monster_attacks.{h,cpp}` |
| Special-attack animation | `src/battle/monster_special_anim.dat` (384-byte binary) | `src/data/monster_special_anim.{h,cpp}` |
| Vertical alignment | `src/btlgfx/monster_align.dat` (256-byte binary) | `src/data/monster_align.{h,cpp}` |

## Version posture

Every table in this family is version-invariant: each rips to a single
un-suffixed file shared by all supported ROMs, with no version conditionals
around any declaration. No language dispatch axis exists here and no
version-pinned deferrals apply.

## Monster properties (`MonsterProp`)

**384 records × 32 bytes** (ROM `CF/0000`; `notes/rom-map.txt`), indexed by
the `MONSTER` value space. The layout authority is the two battle loaders,
whose per-byte load comments name every field: `LoadMonsterProp`
(`battle_main.asm:7307-7436`) reads the stat bytes, and its helper
`LoadRageProp` (`battle_main.asm:7504-7550`) reads the status/element/
metamorph/special bytes. The RAM map (`notes/battle-ram.txt:952-970`)
documents the two flag bytes and the packed metamorph byte.

### Record layout — 32 bytes

| Offset | Field | Value space | Layout authority |
|---|---|---|---|
| +0 | speed | raw magnitude | `battle_main.asm:7407` |
| +1 | attack power | raw magnitude | `battle_main.asm:7393` |
| +2 | hit % | raw magnitude | `battle_main.asm:7403` |
| +3 | evade % | raw magnitude (loader applies `InvertEvade`) | `battle_main.asm:7397` |
| +4 | magic block % | raw magnitude (loader applies `InvertEvade`) | `battle_main.asm:7400` |
| +5 | defense | raw magnitude | `battle_main.asm:7364` (16-bit read pairs +5/+6) |
| +6 | magic defense | raw magnitude | (high half of +5's read) |
| +7 | magic power | raw magnitude (loader applies `AddHalf`) | `battle_main.asm:7409` |
| +8-9 | HP | 16-bit little-endian | `battle_main.asm:7375` |
| +10-11 | MP | 16-bit little-endian | `battle_main.asm:7372` |
| +12-13 | experience | 16-bit little-endian | `battle_main.asm:7366` |
| +14-15 | gold | 16-bit little-endian | `battle_main.asm:7368` |
| +16 | level | raw magnitude | `battle_main.asm:7405` |
| +17 | metamorph info | packed `pppiiiii` (see below) | `battle_main.asm:7546` → `$3C94` |
| +18 | trait flags | flag byte → `$3C95` (see bit map) | `battle-ram.txt:965-970` |
| +19 | battle-interaction flags | flag byte → `$3C80` (see bit map) | `battle_main.asm:7416`; `battle-ram.txt:952-960` |
| +20-22 | blocked statuses 1-3 | `STATUS1`-`STATUS3` bytes | `battle_main.asm:7539` (16-bit read pairs +20/+21), `:7511` (+22) |
| +23 | absorbed elements | `ELEMENT` bits | `battle_main.asm:7543` (16-bit read pairs +23/+24 → `$3BCC`/`$3BCD`) |
| +24 | nullified elements | `ELEMENT` bits | (high half of +23's read — no 8-bit reader exists) |
| +25 | weak elements | `ELEMENT` bits | `battle_main.asm:7508` |
| +26 | attack graphic | `ITEM` value ("item number for graphics") | `battle_main.asm:7395`, `:1010` |
| +27-30 | innate statuses 1-4 | `STATUS1`-`STATUS4` bytes | `battle_main.asm:7519` (+27/+28), `:7536` (+28/+29), `:7521-7528` (+29/+30) |
| +31 | special attack | packed byte (see below) | `battle_main.asm:7506` → `$322D` |

`MonsterProperties` carries this layout with `sizeof == 32`; each record's
object representation is byte-identical to the ROM record (the u16 fields
hold the ROM's little-endian values, with the platform's little-endianness
statically asserted).

### Trait flags (+18 → `$3C95`, `battle-ram.txt:965-970` "ui-h-n-m")

| Bit | Meaning | Consumer trace |
|---|---|---|
| $01 | dies at 0 MP | `battle_main.asm:3027-3029` |
| $04 | don't display name | (RAM map) |
| $10 | human | `battle_main.asm:9161-9163`; the character-side loader sets it unconditionally (`:6779-6782`) |
| $40 | imp critical | paired with the IMP status at both consumers (`battle_main.asm:6976-6981`, `:8304-8308`) |
| $80 | undead | `battle_main.asm:5837-5839`, `:6782` |

Bits $02, $08, and $20 are unused in the layout; the port names them
`UNUSED_n` so a corpus byte setting one still renders through a named
surface (`include/ostinato/monster_flags.h`).

### Battle-interaction flags (+19 → `$3C80`, `battle-ram.txt:952-960` "c?ksruph")

| Bit | Meaning | Consumer trace |
|---|---|---|
| $01 | harder to run | (RAM map) |
| $02 | first strike — an action at the very start of battle | `battle_main.asm:7483-7487` |
| $04 | can't suplex | `battle_main.asm:9728-9730` |
| $08 | can't run | (RAM map) |
| $10 | can't scan | `battle_main.asm:9713-9715` |
| $20 | can't sketch | `battle_main.asm:9524-9530` |
| $40 | special event ??? (upstream's own uncertainty note, preserved verbatim) | (RAM map) |
| $80 | can't control | `battle_main.asm:9471-9476` |

### Metamorph info (+17 → `$3C94`, packed `pppiiiii`)

The metamorph effect (`TargetEffect_12`, `battle_main.asm:9385-9409`) decodes
the byte in place: the low 5 bits select a pack in the metamorph pack table
(the effect then draws two random bits, indexing `MetamorphProp[pack*4 +
rand]`), and the high 3 bits index the rate table (the effect lands when a
random byte compares below the threshold). The port's `MetamorphInfo`
(`include/ostinato/metamorph_info.h`) round-trips exactly this decode, and
the `MetamorphRate` enum names the documented odds ladder
(`battle-ram.txt:963`): 255/256, 3/4, 1/2, 1/4, 1/8, 1/16, 1/32, never.

### Special attack (+31 → `$322D`)

The monster special-attack setup (`battle_main.asm:8195-8235`) decodes the
byte: bit 7 makes the attack undodgeable, bit 6 makes it deal no damage
(status-only), and the low 6 bits select the effect by band —

- **$00-$1F — inflict status.** The value IS the status id: the dispatch
  converts it to a bit pointer and ORs it into the attack's status bytes
  (`:8233-8235`).
- **$20-$2F — damage boost.** The dispatch adds (value − $20) to the damage
  multiplier (`:8225-8229`).
- **$30/$31 — drain HP / drain MP** (`:8212-8219`).
- **$32 and up — remove reflect** (`:8221-8224`); bits past $32 are dead at
  dispatch. Two corpus bytes live in this band: $32 (SrBehemoth, Red Dragon)
  and $3F (Skull Dragon, whose full byte is $FF — undodgeable, no damage).
  The dead residual ports verbatim through a labeled builder argument so the
  byte round-trips.

The port's `MonsterSpecialAttack`
(`include/ostinato/monster_special_attack.h`) constructs through per-band
builders (`inflictStatus` / `damageBoost` / `drainHp` / `drainMp` /
`removeReflect`, with the modifier bits chained as `withCantDodge()` /
`withNoDamage()`), so every configured byte reads as its meaning. The band
*dispatch* is battle behavior and lives outside the data layer.

### Blocked statuses cover bytes 1-3 only

The record has no blocked-status-4 byte: `LoadRageProp` reads +20/+21 as a
16-bit pair and +22 alone, then applies a constant `#$FF` for the fourth
status byte (`battle_main.asm:7515-7517`). The port's `BlockedStatusSet`
(`include/ostinato/blocked_status_set.h`) stores exactly three bytes and
rejects statuses homed in status byte 4 — the type says structurally what
the record can express.

### Absorbed/nullified elements load as a pair

Byte +24 is live only through the 16-bit read at +23 (`battle_main.asm:
7543-7545`): absorbed elements land in `$3BCC` and nullified elements in
`$3BCD` in one load. They are two semantically distinct `ElementSet` fields
in the port; the pairing is a loader detail.

### Byte +30 is dual-role

Innate status byte 4 *and* separately masked with `#$82` as the
piranha/enemy-runic markers (`battle_main.asm:7412-7415`, ORed into
`$3E4C`). The port stores one `StatusSet` over +27..+30; the `$82` mask is
consumer-side interpretation of the same bytes, applied where the battle
code ports.

### Innate-status load quirks

The loader reads +27..+30 as overlapping 16-bit pairs (`battle_main.asm:
7519`, `:7536`) and gives the flying bit special handling — bit 0 of +29
rotates into bit 15 of the `$3DE8` pair, and the result is masked `#$84FE`
to ignore "character-only" statuses (`:7521-7528`). These are load-time
transforms of the same record bytes; the record stores the four plain
status bytes.

### Index space and identity

Indexed by the `MONSTER` enum (`include/const.inc:891` — 384 names,
placeholder slots included). The port's entry identity asserts
`id == position` at compile time for all 384 entries.

Full corpus: all 384 records are memcmp-compared against the generated
fixture `tests/fixtures/monster_prop_expected.h` by
`tests/test_monster_properties.cpp`.

## Metamorph tables (`MetamorphProp`, `MetamorphRateTbl`)

**32 packs × 4 bytes** (ROM `C4/7F40`; incbin at `battle_main.asm:9424`) and
**8 rate bytes** (ROM `c2/3dc5`; `battle_main.asm:10008-10009`). Each pack
holds four `ITEM` values; the metamorph effect picks one of the four at
random (two `RandCarry` bits, `battle_main.asm:9391-9396`). The rate row is
`$FF,$C0,$80,$40,$20,$10,$08,$00` — matching the RAM map's documented
probability ladder exactly.

Packs 28-31 are all item byte `$00`; the bytes resolve like any other
(item $00 is the Dirk) and no monster's metamorph byte selects a pack above
25 in the corpus.

### Index space and identity

Packs have no upstream index enum; identity is the plain table position
(0..31), asserted `index == position` at compile time. Rate rows carry the
`MetamorphRate` enumerator as identity, asserted `id == position`.

Full corpus: all 32 packs and all 8 rates are compared against the generated
fixture `tests/fixtures/metamorph_expected.h` by
`tests/test_monster_properties.cpp`.

## Steal/drop items (`MonsterItems`)

**384 records × 4 bytes** (ROM `CF/3000`), one `monster_steal` +
`monster_drop` macro pair per monster (`monster_items.asm:8-12`; byte order
per the macro definitions at `:3-6`): rare steal, common steal, rare drop,
common drop — each an `ITEM` value, `$FF` for an empty slot. The monster
loader copies each row to the per-monster steal cells
(`battle_main.asm:7317`); the victory sequence reads the drop pair
(`battle_main.asm:15494`).

## Rage attacks (`MonsterRage`)

**256 records × 2 bytes** (ROM `CF/4600`), one `make_monster_rage` row per
monster (`monster_rage.asm:3-5`). **The macro takes only the second attack**
— slot 0 is structurally always `ATTACK::BATTLE`; the rage consumer
coin-flips between the two slots (`battle_main.asm:985-990`), so slot 0
always resolves to the monster's normal fight command. The emitter asserts
slot 0 == `ATTACK::BATTLE` on all 256 rows.

**The table ends at monster 255** (the next table starts at `CF/4800`):
monsters 256-383 have no rage row, and the rage system cannot reach one —
the known-rage list at `$257E` is byte-indexed (`battle_main.asm:976-982`)
and the pick doubles an 8-bit monster index into the table (`:985-990`), as
does `SetRage` (`:999-1010`). The port's accessor documents and asserts the
`id < 256` precondition; absence of rows above 255 is contract, not an
omission.

## Sketch attacks (`MonsterSketch`)

**384 records × 2 bytes** (ROM `CF/4300`), one `make_monster_sketch` row per
monster (`monster_sketch.asm:3-5`). The sketch effect picks slot 1 at 3/4
probability and slot 0 at 1/4 (`battle_main.asm:9543-9549` — a `cmp #$40`
carry rotated into bit 0 selects the slot). The port's slot comments carry
the probabilities.

## Control attacks (`MonsterControl`)

**384 records × 4 bytes** (ROM `CF/3D00`), one `make_monster_control` row
per monster (`monster_control.asm:3-20`). **Slot 0 is structurally always
`ATTACK::BATTLE`** (the macro supplies it), and blank macro arguments emit
`ATTACK::NONE` (`$FF`) — the consumers treat `$FF` as the empty sentinel
(muddled/colosseum attack pick `battle_main.asm:1036-1038`, control menu
`:8878-8879`). The emitter asserts slot 0 == `ATTACK::BATTLE` on all 384
rows.

## Special-attack animation (`MonsterSpecialAnim`)

**384 records × 1 byte** (ROM `CF/37C0`), the monster's special-attack
animation index (`battle-ram.txt:961`). Loaded per monster to `$3C81`
(`battle_main.asm:7420`, also read at `:1004`), then handed to the battle
graphics code as the animation index (`battle_main.asm:8193-8194` → `$B7`).
The byte value is the observable — the port stores it raw (hex), typed
access only.

## Vertical alignment (`monster_align`)

**256 records × 1 byte** (ROM `EC/E800`), values strictly 0-4, named by the
upstream comment block (`btlgfx_main.asm:2824-2829`): 0 ceiling, 1 ground,
2 buried, 3 floating, 4 flying. The port's `MonsterVerticalAlignment` enum
carries these names; the emitter hard-errors on any byte above 4.

**Both consumers are colosseum-specific** and index with an 8-bit monster id
(`btlgfx_main.asm:2872-2883`, `colosseum.asm:555-564`) — monsters 256-383
have no alignment row, and the port's accessor documents and asserts the
`id < 256` precondition. Ceiling (0) is special-cased before the offset
table (`btlgfx_main.asm:2874-2877`). The `MonsterAlignOffset` y-offset table
next to it (`:2831-2832`) is battle-graphics presentation data, outside this
data family.

## What is NOT monster data (absences are contract)

- `src/btlgfx/monster_overlap.asm` (ROM `CF/3600`, per-monster sprite
  priority y-shift; consumer `btlgfx_main.asm:4663`) is battle-graphics
  data, not part of this family.
- Monster names, special-attack names, and dialogue are text-corpus data.
- `monster_attack_anim_prop.dat` (ROM `EC/E6E8`) is battle-graphics
  animation data.
- Formations / battle groups (`CF/4800` onward) are their own family.
- Monster AI scripts (`CF/8400` onward) are bytecode, not tables.

## Regenerating the generated artifacts

Each generated file names its emitter and exact command line in its header.
The emitters live in `tools/asm_parser/` (`parse_monster_prop.py`,
`parse_metamorph_prop.py`), read only the pinned `original-src/` tree, and
verify their structural expectations (file lengths, record sizes and counts,
value-space bounds, the `MetamorphRateTbl` label and row shape) on every
run, refusing to emit on any deviation. Their unit tests (`test_parse_*.py`)
run locally via
`python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'` and
on CI in the `parser-tests` job.
