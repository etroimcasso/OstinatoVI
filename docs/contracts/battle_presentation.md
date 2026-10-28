# Battle presentation — behavioral contract

Behavioral specification for the battle's picture: how the screen is split, how a background's
art is unpacked and composed, where each monster and party member stands and faces, which frame
a party member shows, the order the battlers are drawn in, and how the battle's windows are built
from the wallpaper. All citations point into the pinned reference tree `original-src/`
(everything8215/ff6 @ `1ea47b5`); line numbers are into `src/btlgfx/btlgfx_main.asm` unless
another file is named. This document is the test authority for
`tests/test_battle_background_art.cpp`, `tests/test_monster_image.cpp`,
`tests/test_character_battle_sheet.cpp`, `tests/test_small_font.cpp`,
`tests/test_cartridge_routines.cpp`, `tests/test_battle_components.cpp` and
`tests/test_battle_preview.cpp`.

What the battle *does* — its clocks, its lines, its commands — is
[battle_core.md](battle_core.md) and [battle_commands.md](battle_commands.md). This is what it
looks like standing still. Everything that moves — the entrance, the attack animations, the step
forward, the HUD's text, the messages, the damage numerals, a monster's death — is specified as it
is ported.

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| The scene is built before the first frame | `InitBattleGfx :2217-2604` | — |
| The screen split | `_c2d1f2 :43157-43166`, `_c2b6f7 :40402-40439` | `render/battle/layout.h` |
| The background loader | `LoadBattleBG :4090-4144`, `TfrBattleBGGfx :3999-4084`, `TfrBattleBGTiles :3934-3990` | `BattleBackgroundArt::load` |
| The decompressor | `Decompress_ext :48807-48882` | `assets::CartridgeRoutines` |
| Video memory layout | `InitBattleGfx :2352-2398`, `gfx/battle_bg.asm:40-46` | `kBackgroundBlock2FirstTile`, `kBackgroundBlock3FirstTile` |
| A monster's place | `mon_data_get :1990-2034`, `CalcMonsterPos_00..03 :2689-2820`, `InitMonsterPos :2838-2892` | `monsterStanding`, `colosseumMonsterStanding` |
| A monster's picture | `LoadMonsterGfxProp :4543-4617`, `LoadMonsterGfx :4842-4867`, `LoadMonsterGfxTile :4790-4830`, `LoadMonsterPal :4873-4936` | `MonsterImages::image` |
| A monster's facing | `_c13935 :8023-8026` | `MonsterStanding::mirrored` |
| The draw order | `_c10659 :1030-1107`, `UpdateDrawOrder :22447-22506` | `battlerDrawOrder` |
| The party's place | `InitCharGfxMain :8203-8475`, `CalcCharXPos :8171-8197`, `Mult816NoHW :3230-3248`, the tables at `:42801-42820` | `partyMemberStanding`, `partyMemberX` |
| A party member's frames | `_c2c745 :42552-42584`, `LoadCharGfx :8511-8626`, `UpdateCharGfxBuf :7733-7772`, `CharSpriteData :42768-42790`, `DrawCharSprite :7417-7626` | `CharacterBattleSheets::sheet`, `CharacterSprite` |
| A party member's colors | `LoadCharGfx :8606-8623`, `CharPalTbl :42719-42743`, `LoadStatusPal :8632-8645`, `_c2c689 :42499-42503` | `characterColors` |
| The idle pose | `DrawCharSprite :7465-7500`, `_c2c6a9 :42508-42547` | the preview's `kIdlePose` |
| The windows | `DrawWindowMain :12331-12437`, `DrawWindowRow :12457-12469`, `DrawWindowCol :12475-12497`, `WindowTileTbl :12516`, `WindowBorderTileTbl :12523` | `MenuWindow` |
| The small font and its palettes | `LoadMenuGfx :8895-8933`, `btlgfx_ram.inc:5-11` | `text::buildSmallFontSheet`, `text::smallFontPalettes` |

## The screen

The screen is split at scanline 151 (`_c2d1f2 :43157-43166`: 64 + 64 + 23 lines, then 73). Lines
0-150 show the battlefield's background and the battlers. Line 151 shows the HUD's tile row 31,
which is blank, so the backdrop color shows there. Lines 152-223 are the HUD.

The HUD's geometry is derived from the per-line scroll tables (`_c2b6f7 :40402-40439`): the
windows' layer scrolls by `$68` from line 151, which puts the windows' tile row 0 at line 152 and
their eighth row's last line at 215; the text layer scrolls by `$64` from line 159 and by 4 more
every 12 lines, which turns its 16-pixel line pitch (letters on odd tile rows, `MenuTextCmd_01
:15595-15610`) into lines at 164, 176, 188 and 200 that each show seven of their letters' eight
pixel rows.

The backdrop is the first color of the battle font's first palette: `LoadMenuGfx :8925-8931`
copies the font palette's first eight colors to colors 0-7 of color memory.

## The background

### What the record names

A background's six bytes (`BattleBackgroundProperties`, [battle-backgrounds](../engine/data-layer/battle-backgrounds.md))
name up to three graphics blocks, two tilemaps and a palette. The loader (`LoadBattleBG
:4100-4107`) calls the graphics transfer three times, once per slot, unconditionally; a slot whose
byte is `$FF` loads nothing (`:4002-4004`). "A double-width first block means the second is not
loaded" is a convention of the data — every record whose first block is double-width leaves its
second slot `$FF` — not a rule of the loader; and three records (`NARSHE_EXT`, `TOWN_EXT`,
`VILLAGE_EXT`) leave their *first* slot `$FF` and load their second and third. The second tilemap
is loaded (`:4111-4113`) and never shown.

### Where the blocks go

The background is drawn on the second background layer, character base word `$1000`, tilemap
`$6000`, 64 × 32 cells (`InitBattleGfx :2352-2398` sets the per-line register bytes `$0c`, `$61`,
`$10`, `$54`; from line 151 `$69`, `$71`, `$22`, `$79`). The three blocks are transferred to words
`$1000`, `$1800` and `$4800`, so block 1 holds tile numbers 0-127 (0-255 when double-width,
transferred as `$2000` bytes, `:4051`), block 2 holds 128-255, block 3 holds 896-1023, and
256-895 are nobody's. A block is 128 tiles of 32 bytes (4,096 bytes); `gfx/battle_bg.asm:40`'s
"64 tiles" counts 16 × 16 tiles.

### How a block is unpacked

A pointer into the battle backgrounds' own graphics (`BattleBGGfxPtrs`, `gfx/battle_bg.asm:675`)
names a compressed stream, decoded by the cartridge's decompressor; a pointer into the field's
map graphics (`:757-820`, the entries that name `MapGfx`) is copied as it is, always `$2000` bytes
(`:4021-4040`). Every block decodes into one buffer at `$7F:C400` (`_c11e2f :4150-4158`) and the
transfer takes `$1000` or `$2000` bytes from its start regardless of how much the art decoded to,
so a block whose art is shorter than its transfer carries the previous block's bytes after its
own. Many blocks are short (the first field's third block decodes to 127 tiles). The buffer's
content before the first block — the previous battle's tilemap — is not reproduced; the port
zero-fills.

The decompressor (`Decompress_ext :48807-48882`) is 147 bytes at `$C2FF6D`-`$C2FFFF`, entered by
`jsl` and returning with `rtl`. Its source pointer is at direct page `$f3`-`$f5`, its destination
at `$f6`-`$f8` (work RAM, bank `$7E` or `$7F` by `$f8` bit 0, written through the work-RAM port,
`:48816-48821`). Its first instruction is a three-byte `ldx #$0000`, so it must be entered with
16-bit index registers. A stream's first word is its **compressed** length, header included
(`cpy $fc` walks the source, `:48876`); the routine says nothing about how much it wrote. A
control byte governs eight items: a set bit is a literal byte, a clear bit a back-reference of 3
to 34 bytes (five bits plus three, `:48855-48858`) into a 2 KB window at `$7F:F800`.

The port runs the routine rather than rewriting it: its bytes are read out of the installed image,
placed on a machine, and called with the stream in work RAM; the decoded length is measured by
running the stream twice over two fills and taking the bytes the runs agree on. Every tilemap
decodes to `$800` bytes.

### What the cells say

A tilemap cell is a word: bits 0-9 the tile number, 10-12 the palette, 13 priority, 14 and 15 the
horizontal and vertical flips. The battlefield shows rows 0-18 of the 32. Over every shown cell of
every background the palette is 5, 6 or 7 (the background's three, loaded at `:4118-4134`); the
rows below the shown ones name other palettes. The priority bit is not read by the port: every
battler's sprite has priority 3 (`InitCharGfxMain :8403`) and is drawn above the background
whatever the cell says.

### The three that reach outside their art

Pinned by `tests/test_battle_background_art.cpp` over all 56:

- **Background 38's second block** (graphics 72, a map graphics pointer at `$E5E420`) is copied
  `$2000` bytes, 4,288 of them past the end of the map graphics family into whatever follows. The
  port copies the same bytes.
- **Background 49** (`BATTLE_BG_31`, which upstream marks unused and no map or event names) has
  251 shown cells naming tiles past its blocks' own art (its blocks decode to 128, 79 and 76
  tiles), so they show the previous block's leftovers. The port reproduces the shared buffer and
  so the same leftovers. Unreachable in the shipped game.
- **Background 50** (`GHOST_TRAIN`) has two shown cells naming tiles 652 and 653, numbers no
  block loads; on the hardware they show whatever that part of video memory held. The port draws
  them blank ([Bugs.md](../Bugs.md)).

## The monsters

### Where a monster stands

A monster's formation byte gives its place: x = the high nibble × 8, y = the low nibble × 8
(`mon_data_get :2019-2030`). The battle type moves it (`CalcMonsterPos_00..03`): a back attack
negates x and subtracts the width (`:2715-2722`, so the sprite is mirrored about 128); a pincer
mirrors a monster whose right edge reaches `$68` or further about 160 (`:2742-2756`); a side attack
adds `$30` to every x (`:2803-2806`). These are the rules `battle::placeMonster` already carries;
y is never moved. A monster's facing flag starts 0 (`InitMonsterPos :2845-2848`) and is toggled by
the same three rules; the art is drawn mirrored when the flag is set (`_c13935 :8023-8026`), and
unmirrored art faces right.

The colosseum's one monster (`InitMonsterPos :2858-2891`) stands with its top at 128 less its
height, raised or lowered by its alignment's offset (`MonsterAlignOffset :2831-2832`, read at
`[alignment]` since the index is alignment − 1 at `+1`: ground 0, buried −8, floating +8, flying
−40), or at the top of the screen for a ceiling monster; its left edge is at (128 − width) / 2 + 24.

### Its picture

The monster's graphics properties (`LoadMonsterGfxProp :4543-4617`): the graphics offset is the
first word's low fifteen bits × 8 into the monster graphics; bit 15 of that word marks
three-plane graphics; the palette index is bits 0-9 of the next word; the stencil number follows.
The tiles stream through the stencil's filled cells row by row, left to right (`LoadMonsterGfx
:4842-4867`, the bit walk `:4758-4774`), each filled cell taking the next tile — 24 bytes for a
three-plane monster, 32 for a four-plane one (`:4790-4830`) — and an empty cell shows nothing. The
palette is sixteen colors at the index × 16 into the monster palettes (`:4873-4936`); a
three-plane monster's index addresses an eight-color block and the next eight come with it.

The Ghost Train's stencil is one cell, and its tile is blank: its picture is its background.

At most three distinct monster palettes load in one battle (`:4894-4905`); a fourth would read as
the first. No shipped formation needs a fourth (`tests/test_monster_image.cpp`), so nothing is
preserved.

### Its feet

For the draw order a monster's bottom edge is y + 8 × height − 8 + its overlap shift
(`_c10659 :1043-1059`, `w7e8057` from the monster overlap table).

## The party

### Where a party member stands

The party stands along lines from a point `$68` (104) above the top of the screen at x = 120
(`:42794-42799`). Each slot's y comes from `CharYOffsetTbl :42808-42813` by battle type (68, 86,
104, 122 in a front attack; the Magitek train's background uses its own row, `:8205-8209`), and
its line's angle from `CharXAngleTbl :42816-42820`, plus `CharRowOffsetTbl :42801-42805` for a
back-row member (`:8279-8289`): 256 points straight down, each 128 is 45 degrees, larger values
lean right.

`CalcCharXPos :8171-8197` is exact-port arithmetic: the angle's low nine bits less 256 is the
lean (`:8178-8182`); the distance is the slot's y plus 104 in a byte (`:8173-8176`); for a lean of
0 or more, x = 120 + ((lean × distance) >> 8); for a lean below 0 the lean is *complemented* (not
negated, `not_a :8185`), the product is taken the same way, and the result is complemented again
(`:8189`) before 120 is added. `Mult816NoHW :3230-3248` multiplies a 16-bit value by an 8-bit one
and the high word is read (`:8188`). The two forms differ by one wherever the product has a
fraction (`tests/test_battle_components.cpp` pins one such point).

In the colosseum slot 0 stands at y 104 (`:8370-8376`). Scripted battle positions
(`w7e6246`, `:8377-8394`) are written by a battle event command and are that unit's.

### Its facing

`CharFlipTbl :45268-45272` by battle type — a back attack mirrors every slot, a pincer slots 0 and
2, a side attack slots 2 and 3 — and a party member fighting against the party is mirrored again
(`:8457-8474`). Unmirrored art faces left.

### Its frames

A party member's 32 frames are cut from its field sprite sheet, one of the map sprite family's
items, at the place `CharGfxPtrs :42748-42752` gives its graphics slot (the 23 named sheets in
order, then the soldier's again for the green soldier). `_c2c745 :42552-42584` gives each frame's
eight tiles as byte offsets into the sheet; `LoadCharGfx :8540-8566` copies 256 tiles and mirrors
in place the two leg tiles of frames 3 and 16 (`:8569-8587`, at `$03C0` and `$10C0`). A frame's
tiles are two columns by four rows, row-major (`UpdateCharGfxBuf :7733-7772` copies four 64-byte
runs to +0, +$200, +$40 and +$240 of the sprite tile space). The frames reach 5,280 bytes into a
sheet; Gestahl's sheet is 1,760 bytes, so his frames read on into the next sheet, as the loader
does.

A standing frame (1-31) is drawn as two 16 × 16 sprites (`CharSpriteData :42768-42772`,
`DrawCharSprite :7563-7624`): the top half 8 above the member's point and the bottom half 8
below it, which together are the frame's 16 × 32 placed 8 above the point; its top row is blank,
so 16 × 24 shows. The fallen frame (0) is two 16 × 16 halves side by side: at (x − 4, y + 8)
and (x + 12, y + 8) unmirrored, or (x + 4, y + 8) and (x − 12, y + 8) mirrored (`:42775-42778`),
which is the 32 × 16 picture mirrored and placed at x − 12. A mirrored frame (a pose step at or
above `$30`, `:7504-7510`) combines with the member's facing by exclusive or (`:7516-7518`).

### Its colors

Twelve colors from the battle palette `CharPalTbl :42719-42743` names for the member's graphics
slot (`LoadCharGfx :8606-8623`; the soldier draws palette 0 when the green-soldier story bit is
set, `:8594-8605`), then the four status colors of the member's party slot (`_c2c689
:42499-42503`, `LoadStatusPal :8632-8645`): sixteen in all, in palettes 8, 10, 12 and 14 by slot
(`:8395-8402`).

### Its pose when nothing is happening

`DrawCharSprite :7465-7500`: the action in progress, else the status pose, else the idle action 6,
whose four steps (`_c2c6a9` row 6) are stepped by the member's counter every four frames. Step 0 of
the idle action is frame 17, not mirrored. The status poses, the waiting poses and the stepping
counter are the presentation logic's.

### Its feet

For the draw order a party member's bottom edge is its y + 16 (`_c10659 :1092-1100`).

## The draw order

`UpdateDrawOrder :22447-22506` lists the four party members' bottom edges then the six monsters',
and bubble-sorts the list descending (`:22482-22485` swaps when an entry is less than the next),
so the battler with the lowest feet on the screen comes first and is drawn in front; two with
level feet keep their order, the earlier slot in front. The port draws back to front, which is the
same list reversed: ascending by feet, and among equals the later slot first.

## The windows

`DrawWindowMain :12331-12437` draws a window `w` columns by `h` rows, border included:

- the top row from `WindowBorderTileTbl`'s first four (`$32,$31,$32,$31`) cycling from column 0,
  and the bottom row from its next four (`$3a,$39,$3a,$39`) the same way (`DrawWindowRow
  :12457-12469` cycles its index `and #$03` from 0 at the window's left edge);
- every row between from `WindowTileTbl :12516-12520`, the row's pattern index `(r − 1) & 3`
  starting at 0 for the first inside row (`:12357-12376`, `$14` from 0 by 4), the column's `c & 3`
  from column 0 — so the window's second column shows the wallpaper's first;
- the left column from `$36,$34` and the right from `$37,$35`, cycling `x & 1` from row 0
  (`DrawWindowCol :12475-12497`);
- then the four corners `$30`, `$33`, `$38`, `$3b` over the edges' ends.

Tile number − `$20` is the `WindowTile` the wallpaper sheet is carved into. The window palette is
the field's seven colors brightened by 8 of 31 per channel (`_c1417e :9053-9102`,
`battleWindowColors`); the windows' layer scrolls to line 152.

## The small font

`SmallFontGfx` is 256 two-plane glyphs (`:8895-8898`, 4,096 bytes). `LoadMenuGfx :8925-8933`
loads the font palette's first sixteen colors as palettes 0-3, replacing palette 0's third color
with the player's font color (`$1d55`), and the last twelve as palettes 5-7; palette 4 is left
black. The HUD shows seven of a glyph's eight rows (§ The screen); 101 glyphs have ink in the
eighth, the digit zero among them ([Bugs.md](../Bugs.md)).

## Negatives — not here

The entrance and exit scripts and the step forward; the attack animations; the battle events; the
menu; the HUD's text and gauges; the messages; the damage numerals; a monster's death fade; the
status poses, character shadows, status sprites, outlines and skin colors; the imp and Magitek
graphics swaps; the controlled-target flip; the final battle's scrolls; the wavy background; the
three background load variants; the scripted battle positions; the BG1 monster path (no shipped
formation sets a BG1 monster, `:2002-2003`).
