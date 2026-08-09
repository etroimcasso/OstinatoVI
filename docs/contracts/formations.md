# Formations — behavioral contract

Behavioral specification for the port's formation-core data: the 576 battle
formations (`battle_monsters`), their parallel aux records (`battle_prop`), and
the 16-entry conditional-battle table (`cond_battle`). All citations point into
the pinned reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`);
rip-generated files live under `original-src/src/`. This document is the test
authority for `tests/test_formation_data.cpp`.

## Sources

| Data | Source | Port surface |
|---|---|---|
| Formations | `src/battle/battle_monsters.dat` (8,640-byte binary; label `BattleMonsters`, `battle_main.asm:16446`) | `src/data/formations.{h,cpp}` + `src/data/generated/formation_data.inc` |
| Formation aux | `src/battle/battle_prop.dat` (2,304-byte binary; label `BattleProp`, `battle_main.asm:16442`) | `src/data/formations.{h,cpp}` + `src/data/generated/formation_aux_data.inc` |
| Conditional battles | `src/battle/cond_battle.dat` (64-byte binary; label `CondBattle`, `battle_main.asm:16456`) | `src/data/formations.{h,cpp}` + `src/data/generated/cond_battle_data.inc` |

## Version posture

Every table here is version-invariant: each rips to a single un-suffixed file
shared by all supported ROMs, with no version conditionals around any
declaration. No language dispatch axis and no version-pinned deferrals apply.

## Formations (`BattleMonsters`)

**576 records × 15 bytes** (ROM `CF/6200`), indexed by formation number. The
battle-graphics loader `mon_data_get` (`btlgfx_main.asm:1990-2028`) and
`InitMonsters` (`battle_main.asm:7672`) name every field.

### Record layout — 15 bytes

| Bytes | Field | Layout authority |
|---|---|---|
| 0 bits 4-7 | VRAM map index (0-12) — selects the sprite-tile layout | `btlgfx_main.asm:1994` |
| 0 bits 0-3 + 1 bits 6-7 | 6-bit "bg1 monsters" mask — zero for every formation ("not set for any monsters" upstream) | `btlgfx_main.asm:2002-2006` |
| 1 bits 0-5 | present mask — slot *i* on-screen at start iff bit *i* | `battle_main.asm:7672` |
| 2-7 | low byte of each slot's monster id | `btlgfx_main.asm:1990` |
| 8-13 | packed position — high nibble X, low nibble Y, each in 8-pixel units | `btlgfx_main.asm:2019-2028` |
| 14 bits 0-5 | bit 8 of each slot's monster id; an empty slot reads `$1FF` | `battle_main.asm:7680` |

A slot's monster id is 9 bits, split across its low byte (2-7) and one bit of
byte 14. The empty-slot sentinel is `$1FF`. A slot may hold a monster (id ≠
`$1FF`) while its present bit is clear: that monster is a reinforcement that
arrives after the battle starts. Byte-14 bits 6-7 are zero corpus-wide.

## Formation aux (`BattleProp`)

**576 records × 4 bytes** (ROM `CF/5900`), one per formation. `LoadBattleProp`
(`battle_main.asm:7940`) reads it; `ChooseBattleType` /
`InitBattleType_*` (`battle_main.asm:7556-7666`) consume the battle-type
nibble.

### Record layout — 4 bytes

| Byte | Field |
|---|---|
| 0 low nibble | monster entrance type (0-15; indexes the 18-entry entry/exit script space) |
| 0 high nibble | battle-type **disable** mask, stored inverted; the loader XORs `$F0` to get the possible types — `$10` front, `$20` back, `$40` pincer, `$80` side |
| 1 | flags: `$02` disable fanfare · `$04` disable Joker Doom · `$08` disable Leap (checked, never set in-game) · `$80` enable character AI. Bits 0/4/5/6 zero corpus-wide |
| 2 | character AI index (zero whenever byte 1 bit 7 is clear) |
| 3 | `$01` disable running · `$02` can't appear on the Veldt · `$04` disable preemptive · `$38` battle song (3-bit index into `BattleSongTbl`, `btlgfx_main.asm:41464`) · `$80` continue current music · `$40` set on exactly two formations (384, 385) with no known consumer — preserved raw |

The port stores the four bytes exactly as ROM (the inverted mask and the
unknown `$40` bit included); the typed accessors apply the `^ $F0` inversion
and decode the packed flag/song fields.

The battle song field indexes `BattleSongTbl` (`$24,$14,$33,$2E,$1A,$3B,$FF,$FF`;
`btlgfx_main.asm:41465`), where `$FF` means "keep the current song" — the two
high values.

## Conditional battles (`CondBattle`)

**16 entries × (trigger formation word, replacement formation word)** (ROM
`CF/3780`). The decode loop (`battle_main.asm:7945-7955`) walks entries 0-7,
testing conditional-battle flag bits 8-15 of `$3EB9` (entry *i* ↔ bit 8+*i*):
when a bit is set, the trigger formation is replaced by the replacement.
Entries 8-15 are never read — dead ROM bytes, ported for byte fidelity and
exposed by the accessor for completeness. Only entry 0 is populated in-game
(formation 452 → 424, the undead-Behemoth substitution).

A formation word is a `FormationRef`: bits 0-14 the formation index, bit 15 the
"add a random 0-3 to the index" flag `LoadBattleProp` reads
(`battle_main.asm:7956-7965`).

## FormationId derivation

The 576 formation names are computed from the compositions: a formation is
named for its monsters in slot order (empties skipped), each the `const.inc`
`MONSTER` symbol. A monster appearing *n* > 1 times is written `NAME_Xn`
(first-seen slot order); a zero-monster formation is `UNUSED_<index>` (14 of
these); and formations that produce the same name take `_2`, `_3`, ... suffixes
in ascending index order. The derivation is recomputed on every parser run and
its monster tokens are cross-checked against the shipped `MonsterId` enum, so a
name can never silently diverge from the corpus.

## Quirks (ported as contract notes; none change data shape)

1. **16-byte copy of the 15-byte record.** The loader copies 8 words
   (`battle_main.asm:7984-7989`), over-reading one byte into the next record;
   the destination byte is never read. The port reads 15 bytes.
2. **Half-dead `cond_battle`.** Entries 8-15 are unreachable by the decode
   loop; ported byte-identically, liveness documented above.
3. **Inverted battle-type nibble.** The ROM stores a disable mask; the XOR
   lives in the loader. The port stores the raw byte and inverts in the typed
   accessor.
4. **Veldt formations** are built from the RAM encounter list at run time
   (`GetVeldtBattle`), not from an extra ROM table; `$FF` in the world battle
   group is the trigger sentinel (field-encounter data, a separate table).
5. **Unknown byte-3 `$40` bit** on formations 384 and 385 has no located
   consumer; preserved raw.

## Negative coverage (absences are contract)

- No table outside `battle_main.asm` / `btlgfx_main.asm` consumes these three
  files; the encounter-selection tables that *choose* a formation are a
  separate family.
- Entrance types 1, 5, 8, 11 appear in no formation (event-command reachable
  only); types 16-17 exceed the formation nibble. The `MonsterEntranceType`
  enum still covers 0-17 (the entry/exit script space, `ARRAY_LENGTH = 18`).
- Aux byte 1 bits 0/4/5/6, and formation byte-14 bits 6-7, are zero corpus-wide
  — asserted by the parser.

## What's tested

`tests/test_formation_data.cpp` verifies every record of all three tables is
byte-identical to its ROM-byte fixture (full corpus, no subsets), with each
row's identity field matching its position; it hand-traces Lobo (formation 0),
the three split-id monsters of formation 471, Kefka's final battle and its
`FINAL_KEFKA_DESCENT` entrance (formation 514), and the undead-Behemoth
substitution (cond entry 0); and it exercises the aux accessor decode, the
`FormationRef` builder round-trip, and the two formations carrying the unknown
`$40` bit.
