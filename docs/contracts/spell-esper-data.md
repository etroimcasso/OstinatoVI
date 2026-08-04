# Spell / esper data — behavioral contract

Behavioral specification for the port's spell and esper data: attack/spell
properties, per-battle magic points, the dance attack table, esper properties,
and natural magic. All citations point into the pinned reference tree
`original-src/` (everything8215/ff6 @ `1ea47b5`); rip-generated files live
under `original-src/src/`. This document is the test authority for
`tests/test_attack_properties.cpp`, `tests/test_battle_magic_points.cpp`,
`tests/test_dance_properties.cpp`, `tests/test_esper_properties.cpp`, and
`tests/test_natural_magic.cpp`.

## Sources

| Data | Source | Port surface |
|---|---|---|
| Attack properties | `src/battle/magic_prop_en.dat` (3,584-byte binary; label `MagicProp`) | `src/data/attack_properties.{h,cpp}` + `src/data/generated/magic_prop_en_data.inc` |
| Per-battle magic points | `src/battle/battle_magic_points.dat` (512-byte binary) | `src/data/battle_magic_points.{h,cpp}` + `src/data/generated/battle_magic_points_data.inc` |
| Dance attacks | `src/battle/dance_prop.asm` (label `DanceProp`) | `src/data/dance_properties.{h,cpp}` + `src/data/generated/dance_prop_data.inc` |
| Esper properties | `src/menu/genju_prop.asm` (label `GenjuProp`) | `src/data/esper_properties.{h,cpp}` + `src/data/generated/genju_prop_data.inc` |
| Natural magic | `src/field/event.asm:1242-1283` (label `NaturalMagic`) | `src/data/natural_magic.h` + `src/data/generated/natural_magic_data.inc` |

## Version posture

- **Attack properties are language-variant.** The upstream rip manifests keep
  `magic_prop_en.dat` and `magic_prop_jp.dat` as separate files for the same
  ROM range while version-invariant tables share one path — the split is
  evidence of content variance between the Japanese and US releases. The port
  carries the EN table (validated against FF3 1.1 (U); the US 1.0/1.1
  revisions share the EN rip entry). The JP table is a pinned deferral until a
  Japanese ROM is available for ripping — visible as a skipped test on every
  platform, and the accessor documents that a language dispatch axis arrives
  with it.
- **The other four tables are version-invariant.** `battle_magic_points.dat`
  rips to a single shared path in both manifests; `dance_prop.asm`,
  `genju_prop.asm`, and the `NaturalMagic` block contain no version
  conditionals (only `.ifnblank` argument-presence checks inside macro
  definitions).

## Attack properties (`MagicProp`)

**256 records × 14 bytes** (ROM `C4/6AC0`; `rom-map.txt:58`), indexed by the
full unified `ATTACK` value space — spells, esper attacks, skills, and monster
specials all share the one table, hence the port name `AttackProperties`
rather than "magic". No source-level record grammar exists (the table rips as
a binary); the layout authority is the loader `LoadMagicProp`
(`src/battle/battle_main.asm:6857-6875`: A = attack index, ×14 via `MultAB`,
14 bytes copied to the spell-mode block at `$11A0`) plus the RAM map
`notes/battle-ram.txt:208-249`, which documents every byte and every flag bit.
Direct access sites confirm individual offsets: MP cost read at `MagicProp+5`
(`src/battle/battle_main.asm:8885`), usable-on-field tested on bit 0 of
`MagicProp+3` (`src/menu/skills.asm:1102`).

### Record layout — 14 bytes

| Offset | Field | Value space |
|---|---|---|
| 0 | targeting | `TARGET` flag byte (flags + 2-bit `INIT_*` sub-field + `MENU = $ff` sentinel) |
| 1 | element | `ELEMENT` bits |
| 2 | attack traits | flag byte (`battle-ram.txt:212-220`) |
| 3 | attack flags 1 | flag byte (`battle-ram.txt:221-229`) |
| 4 | attack flags 2 | flag byte (`battle-ram.txt:230-238`) |
| 5 | mp cost | raw magnitude |
| 6 | power | raw magnitude |
| 7 | misc flags | 2-bit flag byte (`battle-ram.txt:241-243`) |
| 8 | hit rate | raw magnitude |
| 9 | special effect | raw byte; `$ff` = no effect |
| 10-13 | statuses | the four packed status bytes (`STATUS1-4` layout) |

`AttackProperties` carries this layout with `sizeof == 14`; each record's
object representation is byte-identical to the ROM record. The flag-byte bit
names in `include/ostinato/attack_flags.h` cite the RAM map per bit; two bits
the upstream map itself marks uncertain (`???`) keep that uncertainty note
verbatim. The `$ff` special-effect sentinel is stored raw
(`kNoSpecialEffect`); the consumer-side transform that turns it into a
disabled effect is battle-logic scope, ported with the battle systems.

Full corpus: all 256 records are compared byte-for-byte against the generated
fixture `tests/fixtures/magic_prop_expected.h` by
`tests/test_attack_properties.cpp`.

## Per-battle magic points

**512 entries × 1 byte** (ROM `DF/B400`; `rom-map.txt:221`), indexed by the
battle (formation) index. **Consumer guard (contract):** the reward routine
guards the read with `cpx #$0200 / bcs`
(`src/battle/battle_main.asm:15313-15319`) — formations ≥ 512 award 0 magic
points without touching the table. That guard is reward logic, ported with the
battle reward systems, NOT data-layer scope: the port table is strictly the
512 ROM entries, and `magicPointsForBattle` asserts the strict bound rather
than replicating the guard.

Full corpus: all 512 entries are compared against the generated fixture
`tests/fixtures/battle_magic_points_expected.h` by
`tests/test_battle_magic_points.cpp`.

## Dance attacks (`DanceProp`)

**8 dances × 4 `ATTACK` bytes**, emitted by `make_dance_prop`
(`src/battle/dance_prop.asm:3-6`), indexed by the `DANCE` enum. The consumer
(`RandDance`, `src/battle/battle_main.asm:920-943`) computes `dance×4 + slot`,
where the slot is chosen by comparing a random byte against `DanceRateTbl`
(`src/battle/battle_main.asm:945-947`) — probability tiers 7/16, 3/8, 1/8,
1/16 in slot order. The slot-selection probabilities and the rate table are
battle-logic scope; this table is strictly the 32 attack bytes. Slot order
within each record is preserved exactly (it IS the probability assignment).

Full corpus: all 8 records are compared byte-for-byte against the generated
fixture `tests/fixtures/dance_prop_expected.h` by
`tests/test_dance_properties.cpp`.

## Esper properties (`GenjuProp`)

**27 espers × 11 bytes** (ROM `D8/6E00`): five learn-spell pairs then one
bonus byte. **Pair byte order is rate first, spell second** — `make_genju_spell`
emits `.byte spell_rate, ATTACK::spell_id` (`src/menu/genju_prop.asm:7-9`).
This is the opposite of the natural-magic pairs; the two must never be
conflated.

- **Index space:** the `GENJU` enum, values `$36-$50` (the esper block of the
  unified actor space). Record position = `GENJU` value − `$36`; the port's
  entry identity asserts exactly that at compile time, and
  `getEsperProperties(EsperId)` performs the same offset.
- **Record stride ×11**, confirmed at both consumers: `GetGenjuPropPtr`
  (`src/battle/battle_main.asm:16116-16122`, `lda #$0b / jsr MultAB`) and the
  skills menu (`src/menu/skills.asm:2571-2576`, learn rate at +0, spell id at
  +1). The bonus byte is read at `GenjuProp+10`
  (`src/battle/battle_main.asm:15782`).
- **Empty slots** are `{rate 0, ATTACK::NONE}` (`ATTACK::NONE = $ff`) and a
  **missing bonus** is `GENJU_BONUS::NONE = $ff` — both produced by the
  upstream macro's `.ifnblank` blank-argument fallbacks
  (`src/menu/genju_prop.asm:11-42`), so the emitter derives them from the
  source rather than assuming them. Bonus-less records are independent of
  spell count (Ragnarok holds one spell, Shiva holds five; both lack a bonus).

Full corpus: all 27 records are compared byte-for-byte against the generated
fixture `tests/fixtures/genju_prop_expected.h` by
`tests/test_esper_properties.cpp`.

## Natural magic (`NaturalMagic`)

**32 `{spell, level}` pairs** (ROM `EC/E3C0`, segment `natural_magic`,
`src/field/event.asm:1242-1283`): Terra's 16 pairs then Celes's 16,
contiguous. **Pair byte order is spell first, level second** — the opposite of
the esper pairs.

- **Consumers:** the battle level-up learn check
  (`src/battle/battle_main.asm:15998-16003`) and the field event learn
  (`src/field/event.asm:1147-1170`, which reads Celes's half at
  `NaturalMagic+$20`). Character→half selection lives entirely in those
  consumers; the data layer therefore exposes two named tables
  (`kNaturalMagicTerra`, `kNaturalMagicCeles`) and deliberately **no**
  character-dispatch accessor.
- **Preserved quirk (contract):** Celes's list holds `MUDDLE` at level 32
  *after* `BSERK` at level 40 (`src/field/event.asm:1275`) — out of sorted
  order in the ROM. The port carries the slot order byte-verbatim; a
  dedicated test pins the order so a well-meaning re-sort fails loudly.

Full corpus: all 2×16 pairs are compared byte-for-byte against the generated
fixture `tests/fixtures/natural_magic_expected.h` by
`tests/test_natural_magic.cpp`.

## Regenerating the generated artifacts

Each generated file names its emitter and exact command line in its header.
The emitters live in `tools/asm_parser/` (`parse_magic_prop.py`,
`parse_battle_magic_points.py`, `parse_dance_prop.py`, `parse_genju_prop.py`,
`parse_natural_magic.py`), read only the pinned `original-src/` tree, and
verify their structural expectations (labels, file lengths, record sizes and
counts, macro grammar, index-space contiguity) on every run, refusing to emit
on any deviation. Their unit tests (`test_parse_*.py`) run locally via
`python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'` and on
CI in the `parser-tests` job.
