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
| 9 | special effect | dispatch-space index (see the section below); `$ff` = no effect |
| 10-13 | statuses | the four packed status bytes (`STATUS1-4` layout) |

`AttackProperties` carries this layout with `sizeof == 14`; each record's
object representation is byte-identical to the ROM record. The flag-byte bit
names in `include/ostinato/attack_flags.h` cite the RAM map per bit; two bits
the upstream map itself marks uncertain (`???`) keep that uncertainty note
verbatim. The special-effect byte carries the typed surface
`AttackSpecialEffect` (`include/ostinato/attack_effects.h`, `$ff` =
`AttackSpecialEffect::NONE`); the consumer-side transform that disables the
effect at dispatch is battle-logic scope, ported with the battle systems.

Full corpus: all 256 records are verified byte-identical to the generated
fixture `tests/fixtures/magic_prop_expected.h` by
`tests/test_attack_properties.cpp`.

### Special-effect dispatch space (byte +9)

Byte +9 names an entry in the battle engine's special-effect dispatch space.

- **Mechanism.** `LoadMagicProp` copies the 14-byte record to the spell-mode
  block at `$11A0`; byte +9 lands at `$11A9` and is doubled in place as the
  jump-table index (`src/battle/battle_main.asm:6870`). The carry branch
  zeroes the cell for ANY value ≥ `$80` (`:6871-6872`) — the upstream comment
  says `$ff`, but the mechanism masks the whole high half; the corpus carries
  only `$FF` there, so the two are observationally identical.
- **Dispatch.** Two parallel 88-entry jump tables cover `$00-$57`:
  `AttackerEffectTbl` (`:10938`), dispatched by `DoAttackerEffect`
  (`:10118-10125`), and `TargetEffectTbl` (`:10014`), dispatched by
  `DoTargetEffect` (`:9087-9093`). Unfilled slots point at bare-`rts`
  handlers.
- **One space, four feeders.** The attack record's byte indexes the space
  directly. The same space is fed by weapon special effects `$00-$0F` (item
  record special-effect high nibble, `:6954-6957`; `WeaponSpecialEffect` in
  `include/ostinato/item_effects.h`), item-use effects offset `+$48` into
  `$49-$4E` (`:7024-7028`; `ItemUseEffect`), and command-injected pre-doubled
  immediates `$50-$57` (possess `:3852`, gp rain `:4022`, steal
  `:3363`/`:8038`, control `:4110`, leap `:4124`, sketch `:3304`, debilitator
  `:7153`, air anchor `:7160`).
- **Consumer sweep (negatives are contract).** `$11A9` appears nowhere
  outside `battle_main.asm`. Besides the two dispatchers there are exactly
  two reads: the Atma-Weapon damage-formula check (`:1876-1879` — a doubled
  weapon-band value, not attack-band) and the effect-is-zero gate
  (`:5845-5846`, where a stored `$00` and the zeroed `$FF` read identically).
  Menu code reads record bytes +0/+3/+5 only (`src/menu/skills.asm:1060`,
  `:1102`, `src/menu/field_menu.asm:2820`) — never +9.
- **Corpus.** 66 of 256 records carry a non-`$FF` byte, spanning 52 distinct
  values. Four corpus values are dead at dispatch — no handler on either
  side, unread at both extra sites: `$00` (Pummel), `$24` (Crusader), `$3C`
  (Retort — the live Retort mechanism is the `$3E4C.0` state flag its command
  handler sets, `:3910-3911`, not this byte), `$45` (Clear). Two attacker
  handlers exist with no attack-corpus carrier — `$41` (halve damage,
  `:10865`) and `$42` (quarter damage, `:10854`); they stay deliberately
  un-enumerated, and the emitter hard-errors on any byte outside the corpus
  set so a divergence (e.g. a future JP rip) surfaces at emit time.
- **Ruling — `$11` (Golem).** The attacker handler's header (`:10277`) says
  "scan", but the body (`:10279-10283`) loads the attacker's max HP into the
  golem-block pool cell, and the value's sole carrier is Golem (Scan itself
  is `$10`, target-side) — the header is a disassembly annotation mislabel;
  the port enumerator is `GOLEM`.
- **Note — `$1C` (Reflect???).** "Reflect???" is the literal lore name (the
  carrier is `ATTACK::REFLECT_LORE`, `$97`); its target-table slot (`:9760`)
  is a bare `rts` distinct from the shared no-op handler only by address —
  the live behavior is attacker-side (`:10903`).

Value → enumerator map (A = attacker table, T = target table; citations are
`battle_main.asm` handler lines; dead corpus values are carrier-named):

| Byte | `AttackSpecialEffect` | Handlers | Carrier(s) |
|---|---|---|---|
| `$00` | `PUMMEL` | none (dead at dispatch) | Pummel |
| `$10` | `SCAN` | T:9710 | Scan |
| `$11` | `GOLEM` | A:10279 (header mislabel — see ruling) | Golem |
| `$12` | `METAMORPH` | T:9383 | Ragnarok (esper) |
| `$13` | `PALIDOR` | T:9210, A:10761 ("sonic dive") | Palidor |
| `$15` | `MANTRA` | A:10835 | Mantra |
| `$16` | `SPIRALER` | A:10802 | Spiraler |
| `$17` | `TAPIR` | T:9780 | Tapir |
| `$18` | `WARP` | A:10461 (handler shared with `$4D`) | Warp |
| `$19` | `EXPLODER` | A:10397, T:9695 | Exploder |
| `$1A` | `BLOW_FISH` | A:10600 | Blow Fish |
| `$1B` | `PEARL_WIND` | A:10264 | Pearl Wind |
| `$1C` | `REFLECT_LORE` | A:10903, T:9760 (rts-only) | Reflect??? |
| `$1D` | `PEARL_LORE` | A:10522 ("l? pearl") | Pearl Lore |
| `$1E` | `STEP_MINE` | A:10150 | Step Mine |
| `$1F` | `DISCHORD` | T:9268 | Dischord |
| `$20` | `PEP_UP` | T:9795 | Pep Up |
| `$21` | `RIPPLER` | T:9661 | Rippler |
| `$22` | `STONE` | T:9195 | Stone |
| `$23` | `DISABLE_COUNTERATTACK` | T:9750 | X-Zone, Odin, Raiden, Cleave, Snare, Xfer |
| `$24` | `CRUSADER` | none (dead at dispatch) | Crusader |
| `$25` | `MISSES_FLOATING_TARGETS` | T:9559 | Quake, Terrato, Wombat, Whump, ChocoboP, Magnitude8, Slide, Takedown, Wild Fang |
| `$26` | `WALLCHANGE` | T:9289 | WallChange |
| `$27` | `ESCAPE` | A:10542, T:9255 (shared `$27/$38/$4B`) | Escape |
| `$28` | `MIND_BLAST` | A:10645, T:9608 | Mind Blast |
| `$29` | `N_CROSS` | A:10659 | N. Cross |
| `$2A` | `FLARE_STAR` | A:10613 | Flare Star |
| `$2B` | `R_POLARITY` | T:9278 | R.Polarity |
| `$2C` | `LAUNCHER` | A:10437 | Launcher |
| `$2D` | `LOVE_TOKEN` | T:9886 | Love Token |
| `$2E` | `SEIZE` | T:9810 | Seize |
| `$2F` | `TARGETTING` | T:9971 | Targetting |
| `$30` | `SUPLEX` | A:10876, T:9725 | Suplex |
| `$31` | `FORCEFIELD` | A:10571 | ForceField |
| `$32` | `QUADRA_SLAM_SLICE` | A:10588 | Quadra Slam, Quadra Slice |
| `$33` | `BABABREATH` | A:10477, T:9246 | Bababreath |
| `$34` | `CHARM` | T:9765 | Charm |
| `$35` | `DOOM` | T:9911 | Doom |
| `$36` | `EMPOWERER` | A:10784 | Empowerer |
| `$37` | `OVERCAST` | T:9863 | Overcast |
| `$38` | `SNEEZE` | T:9255 (shared) | Sneeze |
| `$39` | `ENGULF` | T:9236 | Engulf |
| `$3A` | `ZINGER` | T:9873 | Zinger |
| `$3B` | `EVIL_TOOT` | T:9628 | Evil Toot |
| `$3C` | `RETORT` | none (dead at dispatch; see corpus note) | Retort |
| `$3D` | `REVENGE` | A:10748 | Revenge |
| `$3E` | `PHANTASM` | T:9941 | Phantasm |
| `$3F` | `STUNNER` | T:9952 | Stunner |
| `$40` | `FALLEN_ONE` | T:9983 | Fallen One |
| `$43` | `QUICK` | A:10920 | Quick |
| `$44` | `DISCARD` | A:10818, T:9837 | Discard |
| `$45` | `CLEAR` | none (dead at dispatch) | Clear |
| `$FF` | `NONE` | sentinel; zeroed pre-dispatch (`:6870-6872`) | 190 records |

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

Full corpus: all 8 records are verified byte-identical to the generated
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

Full corpus: all 27 records are verified byte-identical to the generated
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

Full corpus: all 2×16 pairs are verified byte-identical to the generated
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
