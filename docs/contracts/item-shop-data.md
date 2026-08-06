# Item / shop data — behavioral contract

Behavioral specification for the port's item-family data: item properties
(one record per item — item, weapon, armor, and relic stats share a single
table), shop specifications, and the colosseum wager table. All citations
point into the pinned reference tree `original-src/` (everything8215/ff6 @
`1ea47b5`); rip-generated files live under `original-src/src/`. This document
is the test authority for `tests/test_item_properties.cpp`,
`tests/test_shop_properties.cpp`, and `tests/test_colosseum_wagers.cpp`.

## Sources

| Data | Source | Port surface |
|---|---|---|
| Item properties | `src/menu/item_prop_en.dat` (7,680-byte binary; label `ItemProp`) | `src/data/item_properties.{h,cpp}` + `src/data/generated/item_prop_en_data.inc` |
| Shop specifications | `src/menu/shop_prop.dat` (1,152-byte binary; label `ShopProp`) | `src/data/shop_properties.{h,cpp}` + `src/data/generated/shop_prop_data.inc` |
| Colosseum wagers | `src/menu/colosseum.asm:1212` (label `ColosseumProp`) | `src/data/colosseum_wagers.{h,cpp}` + `src/data/generated/colosseum_prop_data.inc` |

## Version posture

- **Item properties are language-variant.** The table is declared via
  `incbin_lang` (`src/menu/item.asm:2592-2599`), which keeps
  `item_prop_en.dat` and `item_prop_jp.dat` as separate files for the same
  ROM range — the split is evidence of content variance between the Japanese
  and US releases. The port carries the EN table (validated against FF3 1.1
  (U)). The JP table is a pinned deferral until a Japanese ROM is available
  for ripping — visible as a skipped test on every platform, and the accessor
  documents that a language dispatch axis arrives with it.
- **Shop specifications are version-invariant.** `shop_prop.dat` is a plain
  `.incbin` (`src/menu/shop.asm:2305-2310`) shared by every supported ROM.
- **Colosseum wagers are version-invariant.** The `ColosseumProp` block
  (`src/menu/colosseum.asm:1212-1469`) contains no version conditionals; the
  `LANG_EN` conditionals elsewhere in `colosseum.asm` sit in drawing code,
  outside the table.

## Item properties (`ItemProp`)

**256 records × 30 bytes** (ROM `D8/5000`; `notes/rom-map.txt:191`), indexed
by the `ITEM` value space. No RAM-map byte table documents the record itself;
the layout authority is the consumer access sites — the ×30 stride in
`GetItemPropPtr` (`src/menu/item.asm:1001-1012`) and the battle loader
(`src/battle/battle_main.asm:7177-7180`), plus the per-field reads cited
below — and the `$11D2-$11DF` cells `CalcEquipEffect` copies bytes +5..+13
into (`battle_main.asm:2480-2533`, `notes/battle-ram.txt:318-381`).

### Record layout — 30 bytes

| Offset | Field | Value space | Layout authority |
|---|---|---|---|
| +0 | type + usage | `ITEM_TYPE` in bits 0-2, `ITEM_USAGE` flags ($10 throw / $20 battle / $40 menu); bits 3 and 7 unused corpus-wide | `item.asm:565-570` |
| +1-2 | equip permissions | 16-bit little-endian mask (see below) | `item.asm:1358`, `equip.asm:2287-2317` |
| +3 | spell learn rate | raw magnitude | `item.asm:1717` |
| +4 | spell learned | `ATTACK` value | `item.asm:1720` |
| +5 | field effects | flag byte → `$11DF` (`battle-ram.txt:377-381`) | `battle_main.asm:2487` |
| +6 | status 1 protection | `STATUS1` byte → `$11D2` | `battle_main.asm:2490` |
| +7 | status 2 protection | `STATUS2` byte → `$11D3` | (16-bit half of +6) |
| +8 | status 3 granted | `STATUS3` byte → `$11D4` | `battle_main.asm:2492` |
| +9 | relic effects 1 | flag byte → `$11D5` (`battle-ram.txt:326-334`) | (16-bit half of +8) |
| +10 | relic effects 2 | flag byte → `$11D6` (`battle-ram.txt:335-343`) | `battle_main.asm:2494` |
| +11 | relic effects 3 | flag byte → `$11D7` (`battle-ram.txt:344-352`) | (16-bit half of +10) |
| +12 | relic effects 4 | flag byte → `$11D8` (`battle-ram.txt:353-360`; bit 7 undocumented) | `battle_main.asm:2496` |
| +13 | relic effects 5 | flag byte → `$11D9` (`battle-ram.txt:361-367`; bits 5-6 undocumented) | (16-bit half of +12) |
| +14 | targeting | `TARGET` flag byte | `battle_main.asm:6510` |
| +15 | element | `ELEMENT` bits (role varies — see the role table) | `item.asm:1830`, `battle_main.asm:2554` |
| +16 | vigor / speed | two signed nibbles (see stat boosts) | `item.asm:1583`, `battle_main.asm:2498-2510` |
| +17 | stamina / magic power | two signed nibbles | `item.asm:1603` |
| +18 | spell cast | spell in bits 0-5 + two mode bits (see below) | `battle_main.asm:2642, 6517` |
| +19 | weapon flags / item-use flags | role varies — see below | `equip.asm:1730`, `battle_main.asm:2644` → `$11DA` (`battle-ram.txt:368-373`) |
| +20 | power | raw magnitude (role varies) | `item.asm:1624/1701/2476` |
| +21 | hit rate / defense | raw magnitude (role varies) | `item.asm:1629`, `battle_main.asm:2640/7030` |
| +22 | elements absorbed | `ELEMENT` bits (role varies) | `item.asm:1948`, `battle_main.asm:2559` |
| +23 | elements nullified | `ELEMENT` bits (role varies) | `item.asm:1953`, `battle_main.asm:7032` |
| +24 | elements weak | `ELEMENT` bits (role varies) | `item.asm:1958`, `battle_main.asm:2556` |
| +25 | status 2 set | `STATUS2` byte (cursed-gear class) | `battle_main.asm:2552` |
| +26 | evade / magic block | two nibbles, each an `EquipEvadeTbl` index | `item.asm:1744`, `battle_main.asm:2513-2530` |
| +27 | special effect | packed byte (role varies — see below) | `battle_main.asm:2584/7024` |
| +28-29 | price | 16-bit little-endian | `shop.asm:1140-1148` (sell = /2 is consumer logic) |

`ItemProperties` carries this layout with `sizeof == 30`; each record's
object representation is byte-identical to the ROM record. Fields carry
equipment-primary names (5 of the 7 item types are equipment); the
role-varying fields are the subject of the role table below.

### Per-type roles of the overloaded fields

Fields +15 and +18..+27 change meaning with the record's `ITEM_TYPE` (bits
0-2 of byte +0: 0 tool, 1 weapon, 2 armor, 3 shield, 4 helmet, 5 relic,
6 consumable). The roles, with the consumer that establishes each:

| Offset | Equipment reading | Consumable reading |
|---|---|---|
| +15 | weapon: attack element; armor: halved elements (`item.asm:1830` draws both) | — |
| +18 | spell cast (rods, elemental shields — see the mode bits below) | — |
| +19 | weapon: `WEAPON_FLAG` bits → `$11DA` | item-use flag bits (see below) |
| +20 | weapon: battle power (`item.asm:1701`); armor: defense (`item.asm:1624`) | HP/MP restored (`item.asm:2476`) |
| +21 | weapon: hit rate (`battle_main.asm:2640`); armor: magic defense (`item.asm:1629`) | status bytes 1-2, 16-bit read with +22 (`battle_main.asm:7030`) |
| +22 | armor: elements absorbed (`battle_main.asm:2559`) | (high half of +21's read) |
| +23 | armor: elements nullified (`item.asm:1953`) | status bytes 3-4, 16-bit read with +24 (`battle_main.asm:7032`) |
| +24 | armor: elements weak (`battle_main.asm:2556`) | (high half of +23's read) |
| +27 | block info + weapon special effect (below) | item-use effect (below) |

### Equip permissions (+1-2)

A 16-bit little-endian mask: bits 0-13 are the 14 playable `CHAR` slots in
enum order (Terra bit 0 .. Umaro bit 13; menu filter `equip.asm:2287-2317`),
bit 14 marks imp gear (the battle equip scan isolates it with a double shift,
`battle_main.asm:2535-2541`), and bit 15 marks heavy gear (only equippable
with the merit-award relic effect, `equip.asm:2300-2307`).

### Effect-bit spaces (+5, +9..+13, +19)

The bit meanings for these bytes have no upstream symbol source — they live
in the prose RAM map at the `$11D2-$11DF` cells the battle equip scan copies
the bytes into. The port's `include/ostinato/item_effects.h` names every
documented bit with its `battle-ram.txt` line citation; positions the RAM map
leaves undocumented stay unnamed, and a corpus byte using one refuses to
decompose. The spaces:

- **Field effects (+5 → `$11DF`)**: charm bangle ($01), moogle charm ($02),
  sprint shoes ($20), tintinabar ($80 — upstream's own "doesn't work" note
  preserved verbatim).
- **Relic effects 1-5 (+9..+13 → `$11D5`-`$11D9`)**: the five relic ability
  bytes (`battle-ram.txt:326-367`) — damage raisers, HP/MP boosts, command
  replacements (fight→jump, magic→x-magic, steal→capture, ...), rate
  raisers, MP costs, low-HP auto-casts, double exp/GP, undead.
- **Weapon flags (+19, weapon role → `$11DA`)**: the `WEAPON_FLAG` enum
  (`include/const.inc:881-886`): swdtech ($02), back-row-capable ($20),
  two-handed ($40), runic ($80).
- **Item-use flags (+19, consumable role)**: walked by the item-use
  routine's shift chain (`battle_main.asm:7035-7059`): invert on undead
  ($02), restores HP ($08), restores MP ($10), removes status ($20),
  fractional damage ($80).

**Dead +19 bits (contract).** Two +19 bits are set in the corpus but read by
no code in the tree; both port verbatim through named constants
(`kDeadItemFlagBit0` / `kDeadItemFlagBit6` in `src/data/item_properties.h`):

- **Bit 0** on exactly Paladin Shld, Memento Ring, and Safety Bit. The
  battle side copies hand-slot +19 bytes to the per-character weapon-effects
  cells but only ever tests bits 1/5/6/7, relic bytes are never copied, and
  every menu read masks the two-hand bit.
- **Bit 6** on exactly Magicite and Super Ball. The battle item-use chain
  shifts it through untested (`battle_main.asm:7041-7042`) and the menu
  restore path tests only bits 3/4/7; both items' behavior comes entirely
  from their item-use effect dispatch.

### Stat boosts (+16, +17)

Each byte packs two signed nibbles (low nibble first — the menu's draw loop
shifts them out low-first, `item.asm:1583/1603`): +16 is vigor then speed,
+17 is stamina then magic power. Nibble bit 3 is the sign; the battle scan
decodes $9..$F as −1..−7 (`battle_main.asm:2500-2508`). The $8
"negative zero" nibble never appears in the corpus and refuses to decompose.

### Spell cast (+18)

Bits 0-5 are the spell (`ATTACK` value ≤ $3F), plus two mode bits: bit 6
casts the spell randomly (1-in-4) when attacking (`CheckWeaponMagic`,
`battle_main.asm:8664-8673`); bit 7 casts it when the equipment is used as
an item (`InitTarget_01`, `battle_main.asm:6525-6533` — the rods and
elemental shields). A nonzero byte always carries at least one mode bit in
the corpus.

### Special effect (+27)

Role-packed by item type:

- **Equipment**: bits 0-1 are the block graphic (0 dagger, 1 sword,
  2 shield, 3 zephyr cape) and bits 2-3 the can-block abilities (physical /
  magic) — the `$11BE ----mpbb` cell (`battle-ram.txt:298-302`, decomposed
  inline at `battle_main.asm:2584-2606`). Bits 4-7 are the weapon
  special-effect index, shifted into the effect dispatcher at
  `battle_main.asm:6954-6957`; every index $1-$E is named by its handler
  (Thiefknife, Atma Weapon, instant-kill, Man Eater, Drainer, Soul Sabre,
  MP-critical, Sniper/Hawkeye, Dice, Valiantknife, Tempest, Heal Rod,
  Scimitar/Zantetsuken, Ogre Nix).
- **Consumable**: the whole byte is the item-use effect, offset by $48 into
  the same dispatch space (`battle_main.asm:7024-7028`): 1 magicite,
  2 super ball, 3 smoke bomb, 4 elixir, 5 warp stone, 6 dried meat. $00
  means no effect (`battle-ram.txt:245`); $FF disables item use outright
  (`battle_main.asm:6870-6872`).

### Index space and identity

Indexed by the `ITEM` enum ($00 Dirk .. $FE Dried Meat, $FF the EMPTY
sentinel). The port's entry identity asserts `id == position` at compile
time for all 256 entries.

Full corpus: all 256 records are memcmp-compared against the
generated fixture `tests/fixtures/item_prop_expected.h` by
`tests/test_item_properties.cpp`.

## Shop specifications (`ShopProp`)

**128 records × 9 bytes** (ROM `C4/7AC0`; `notes/rom-map.txt:62`), indexed
by the shop number event scripts pass to the shop menu (×9 stride via
hardware multiply, `shop.asm:1794-1801`). Each record is one config byte
plus eight item slots (`shop.asm:819`).

### Config byte

Read twice by the shop menu:

- **Bits 0-2 — shop type** (`shop.asm:1802-1812` masks $07 and draws the
  shop name through `ShopTypeTextTbl`): 1 Weapon, 2 Armor, 3 Item,
  4 Relics, 5 Vendor (the names are the table's own EN display strings,
  `src/menu/menu_text_en.inc:282-286`). Type 0 has no text-table entry and
  appears only on unused records.
- **Bits 3-5 — price adjustment** (`AdjustShopPrice`, `shop.asm:895-923`,
  dispatching the 7 documented behaviors): 0 none, 1 +50%, 2 +100%,
  3 −50%, 4 −50% for a female showing character (Terra, Celes, or Relm) /
  +50% male, 5 the inverse, 6 −50% when Edgar is the showing character
  (the Figaro Castle shops — the corpus uses only codes 0 and 6).
- **Bits 6-7** are clear on every record and read by no consumer.

### Item slots and unused records

Slots hold `ITEM` values; `$FF` marks an empty slot (the buy menu checks it,
`shop.asm:822`) and only trails real items — the corpus has no real item
after a pad. The game defines 87 shops (indices 0-86); the remaining 41
records (87-127) are unused fill: config $00 with all eight slots empty.

### Index space and identity

Shops have no upstream index enum; the identity is the plain table position
(0..127), asserted `shopIndex == position` at compile time for all 128
entries.

Full corpus: all 128 records are memcmp-compared against the
generated fixture `tests/fixtures/shop_prop_expected.h` by
`tests/test_shop_properties.cpp`.

## Colosseum wagers (`ColosseumProp`)

**256 records × 4 bytes** (ROM `DF/B600`; `notes/rom-map.txt:222`), one per
wagerable item, indexed by the wagered item's `ITEM` value (`LoadColosseumProp`
computes ×4, `colosseum.asm:833-846`). Rows are emitted by
`make_colosseum_prop` (`colosseum.asm:1189-1204`):

| Offset | Field | Semantics |
|---|---|---|
| +0 | monster | the `MONSTER` index fought — stored as ONE byte |
| +1 | (unused) | `$40` on every record; read by no code in the tree |
| +2 | prize | the `ITEM` won |
| +3 | hide flag | `$FF` hides the prize name in the wager menu, `$00` shows it |

- **Blank rows default to Chupon.** A `make_colosseum_prop` with no
  arguments (151 of the 256 rows — every unwagerable item) takes the
  macro's `.ifblank` defaults: `MONSTER::CHUPON_COLOSSEUM`, prize
  `ITEM::ELIXIR`, shown. (Chupon sneezes the party out of the arena; the
  Elixir is unreachable through that row.)
- **The monster field is one byte** while the `MONSTER` space is 384 entries
  wide — so every wagered monster's index fits a byte (ca65's `.byte` would
  error otherwise). The port stores the byte verbatim and types the accessor
  (`monsterId()`); the emitter verifies the one-byte property for all 256
  rows.
- **The dead `$40` byte** ports verbatim (`unused40`, always
  `kColosseumUnusedByte`): `LoadColosseumProp` reads +0, +2, and +3,
  skipping +1, and no other code touches the table.
- **Hide-prize rows**: exactly four — Ragnarok (Didalos, hiding the
  Illumina), Striker (Chupon, hiding another Striker), Cat Hood (Hoover,
  hiding the Merit Award), and Merit Award (Covert, hiding the Rename
  Card).

### Index space and identity

Indexed by the wagered item's `ITEM` value; the port's entry identity
asserts `id == position` at compile time for all 256 entries.

Full corpus: all 256 records are memcmp-compared against the
generated fixture `tests/fixtures/colosseum_prop_expected.h` by
`tests/test_colosseum_wagers.cpp`, which also asserts the `$40` byte on
every record.

## Regenerating the generated artifacts

Each generated file names its emitter and exact command line in its header.
The emitters live in `tools/asm_parser/` (`parse_item_prop.py`,
`parse_shop_prop.py`, `parse_colosseum.py`), read only the pinned
`original-src/` tree, and verify their structural expectations (file
lengths, record sizes and counts, value-space bounds, macro grammar, row
labels) on every run, refusing to emit on any deviation. Their unit tests
(`test_parse_*.py`) run locally via
`python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'` and
on CI in the `parser-tests` job.
