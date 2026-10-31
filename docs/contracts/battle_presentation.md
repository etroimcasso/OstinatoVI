# Battle presentation — behavioral contract

Behavioral specification for the battle's picture: how the screen is split, how a background's
art is unpacked and composed, where each monster and party member stands and faces, which frame
a party member shows, the order the battlers are drawn in, how the battle's windows are built
from the wallpaper and shaded, and what the presentation shows over time — the names and messages
at the top of the screen, the HUD's text, the damage numerals, a monster's death. All citations point into the pinned reference tree `original-src/`
(everything8215/ff6 @ `1ea47b5`); line numbers are into `src/btlgfx/btlgfx_main.asm` unless
another file is named. This document is the test authority for
`tests/test_battle_background_art.cpp`, `tests/test_monster_image.cpp`,
`tests/test_character_battle_sheet.cpp`, `tests/test_small_font.cpp`,
`tests/test_cartridge_routines.cpp`, `tests/test_battle_components.cpp`,
`tests/test_battle_presentation.cpp`, `tests/test_battle_hud_components.cpp` and
`tests/test_battle_preview.cpp`.

What the battle *does* — its clocks, its lines, its commands — is
[battle_core.md](battle_core.md) and [battle_commands.md](battle_commands.md). This is what the
player sees of it. The entrance, the attack animations and the step forward are specified as they
are ported.

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| The scene is built before the first frame | `InitBattleGfx :2217-2604` | — |
| The screen split | `_c2d1f2 :43157-43166`, `_c2b6f7 :40402-40439`, `line_copy :1244-1346` | `render/battle/layout.h` |
| The border | `system_init :43444-43447`, `:1702-1703`, `:2541-2543`, `_c2d0fe :43005-43016`, `InitBattleGfx :2386-2392` | `ScreenBorder`, `kBattleTopBlankRows`, `kBattleBottomBlankRows` |
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
| A party member's pose | `DrawCharSprite :7465-7538`, `_c2c6a9 :42508-42547`, `_c13071 :6816-6857`, `InitCharGfxMain :8418-8426` | `partyMemberPose`, `poseStep`, `poseCounters` |
| The windows | `DrawWindowMain :12331-12437`, `DrawWindowRow :12457-12469`, `DrawWindowCol :12475-12497`, `WindowTileTbl :12516`, `WindowBorderTileTbl :12523` | `MenuWindow` |
| The small font and its palettes | `LoadMenuGfx :8895-8933`, `btlgfx_ram.inc:5-11` | `text::buildSmallFontSheet`, `text::smallFontPalettes` |
| Playing a script | `BtlGfx_01 :392-409`, `WaitFrame :425-439`, the `GfxCmd_xx` handlers | `tickBattlePresentation` |
| The windows at the top | `GfxCmd_01 :22784-22829`, `_c197a1 :22773-22774`, `_c2bb11 :40904-40970`, `GfxCmd_02 :22882-22894`, `GfxCmd_10 :22863-22876`, `GfxCmd_11 :22712-22769`, `InitMsgWindow :22968-22989`, `_c19961 :23058-23072`, `_c19980 :23078-23099`, `chg_center :22926-22942`, `mess_wait :22948-22962`, `MsgSpeedTbl :22899-22900` | `MessagePlayback`, `MessageWindowBox`, `MessageText` |
| The text drawer | `DrawDlgText :13905-13924`, `DlgTextCmdTbl :14003-14035`, `DrawDlgLetter :14508-14664`, `DlgTextCmd_01 :14400-14454` | `battleMessageSteps`, `monsterDialogSteps` |
| The message speed a new game starts with | `menu/menu_sram.asm:101-104`, `menu/config.asm:933-960` | `kNewGameMessageSpeed`, `kSlowestMessageSpeed` |
| The shadings | `InitBattleGfx :2435-2462`, `InitMsgWindowHDMA :22531-22556`, the HDMA table `_c2d138 :43057-43062` | `HudShading`, `MessageShading` |
| The HUD's text | `LoadMenuText :11406-11435`, `DrawMenuText :15445-15471`, `DrawMenuLetter :15494-15504`, `DrawCharInfo_00..02 :15810-15902`, `TrimZeroes4 :3646-3657`, `GaugeTextTbl :15922-15954`, `MenuText::_0..3 :45063-45087` | `HudText`, `PartyStatus`, `MonsterNameList` |
| The list of monster names | `battle_main.asm:15194-15238`, `UpdateMonsterNames :322-338` | `monsterNameList` |
| The damage numerals | `GfxCmd_0b :24664-24784`, `GfxCmd_03 :22512-22525`, `_c1a5fa :24826-24958`, `DrawDmgNumSprites :6085-6182`, `_c12bfc :6188-6242`, `_c12c67 :6250-6293`, `_c12cc2 :6299-6380`, `_c10659 :1030-1107`, `InitBattleGfx :2325-2333` | `DamageNumbersShown`, `ShownNumeral`, `DamageNumbers`, `damageNumeralArt` |
| A monster's death | `MonsterDeathAnim :22222-22317`, `UpdateMonsterDeathPal :22323-22339`, `SetColorMathHDMA :28695-28713` | `DeathFade`, `MonsterDeathFade` |

## The screen

| What | Rows / columns |
|---|---|
| The border | columns 0-7 and 248-255, rows 0-3 and 219-223: black |
| The battlefield | rows 0-150: the background's tilemap from its pixel row 1, and the battlers |
| The HUD's windows | rows 151-214: top border on 151, bottom border on 212-213 |
| The HUD's text | glyph rows 163-170, 175-182, 187-194, 199-206: every letter's eight rows |
| The message windows | rows 8-39, from tile row 1 |
| The message text | the 16-pixel line on rows 15-30; a letter's ink on rows 19-26 |

The rows follow from three facts. The console outputs no scanline 0: the first line shown is
scanline 1, so a background read without a vertical offset begins on the second row of its top
tile (`engine/third_party/snaggletooth/docs/ppu-behavior.md` § The picture's edges). An HDMA
entry delivered on line *n* is what line *n* + 1 shows, so HDMA entry *n* is shown row *n*. Hence a
background's map row *r* shows on row *r* − 1 − its vertical scroll, and a sprite's y is its row.

- **The split** is at entry 151 (`_c2d1f2 :43157-43166`: 64 + 64 + 23 lines, then 73).
- **The battlefield's layer** — the background and every window — scrolls 0 on the battlefield's
  lines (`line_copy :1326-1343`, both terms 0 at rest; `line_init_long :40395-40401`), so its map
  row *r* shows on row *r* − 1: the background's pixel rows 1-151 on rows 0-150.
- **The text layer** scrolls 0 there too (cleared `:40396-40401`), so the message text's tile rows
  2-3 (map rows 16-31) show on rows 15-30.
- **The animation layer**, which carries the message window's box (`InitMsgWindowHDMA
  :22531-22556` sets its color math on it), scrolls −1 (`:1266`), so the box at tile row 1 shows
  from row 8.
- **The HUD's windows**: the windows' layer scrolls by `$68` from entry 151 (`_c2b6f7
  :40434-40439`), which puts the windows' tile row 0 on row 151 and their eighth row's last line on
  214.
- **The HUD's text**: the text layer scrolls by `$64` from entry 159 and by 4 more every 12 entries
  (`:40416-40433`), which turns its 16-pixel line pitch (letters on odd tile rows, `MenuTextCmd_01
  :15595-15610`) into lines at 163, 175, 187 and 199, each showing all eight rows of its letters.
- **The blank rows**: the main-screen designation is set per line by HDMA (`_c2d0fe :43005-43016`,
  to `$212A-$212D`): 4 lines from `w7e8987`, 147 from `w7e898b`, 68 from `w7e898f`, 5 from
  `w7e8987`. `InitBattleGfx :2386-2392` sets `w7e898d` and `w7e8991` to `$17` (the three
  backgrounds and the sprites) and `w7e8989` to 0, so rows 0-3 and 219-223 show no layer.
- **The side columns**: window 1 is inverted on every background and on the sprites (`$33` to
  `W12SEL`, `W34SEL` and `WOBJSEL`, `system_init :43444-43447`) and masks the main screen (`TMW` =
  `$17`, `:2541-2543`), its edges 8 and 247 (`:1702-1703`; per line `_c2d13f :43065-43072`, 8/247
  on the battlefield's lines, `:2409-2418`, and 0/247 from 151, `:2419-2426`, where columns 0-7
  are blank map anyway). The battle is mode 1 at 256 pixels wide (`BGMODE` `$09`, `:2352-2355`;
  `SETINI` 0, `:43427`).

The masked and blank area shows color 0, the backdrop color, which is black in battle: the first
color of the battle font's first palette (`LoadMenuGfx :8925-8931` copies the font palette's first
eight colors to colors 0-7 of color memory).

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
horizontal and vertical flips. The battlefield shows rows 0-18 of the 32, from the first row's
second pixel row (§ The screen). Over every shown cell of
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

### Its pose

`DrawCharSprite :7465-7500`: the action in progress, else the status pose, else the waiting pose,
else the idle action 6. The status pose (`_c13071 :6816-6857`, first that applies): Dead → 1 (the
fallen frame); Sleep → `$0a`; Confuse → `$25`; Chant → 9; Poison → `$0a`; Near-fatal → `$0a` — so
a poisoned member stands wounded. The waiting pose is the one its command's ready stance sets
(`commandWaitingPose`), cleared by the step back and by a pose reset.

Each party member has its own pose counter, started at 0, 2, 4 and 6 for slots 0 to 3
(`InitCharGfxMain :8418-8426`) and counted up once a frame as the member is drawn (`:7538`). The
pose's step is the counter shifted right by three, masked to 0-3 (`:7472-7487`): a pose takes its
next step every eight frames, the slots staggered by two frames. (An animation can make a member
step every four frames, `w7e61d0`; nothing this presentation plays sets it.) Step 0 of the idle
action is frame 17, not mirrored.

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
`battleWindowColors`); the HUD's windows' layer scrolls to line 151.

## The small font

`SmallFontGfx` is 256 two-plane glyphs (`:8895-8898`, 4,096 bytes). `LoadMenuGfx :8925-8933`
loads the font palette's first sixteen colors as palettes 0-3, replacing palette 0's third color
with the player's font color (`$1d55`), and the last twelve as palettes 5-7; palette 4 is left
black. The HUD shows all eight rows of a glyph (§ The screen).

## Playing a script

A battle script plays one command at a time (`BtlGfx_02`'s command loop; each `GfxCmd_xx` returns
when its part is done). A command that waits for nothing — a ready stance, a step back, a pose
reset, a formation change, monsters leaving or entering, and here the animations, battle events and
monster animations, whose pictures belong to the animation programs — takes no frame, and the next
command follows in the same frame. Battle time runs through every command (`WaitFrame :434-436`
updates it unless a battle event stops it), so the battle is told time runs every frame; the battle
loop takes no step while a command is playing, a script is waiting, or a monster is fading out.

Port rulings on the frames a command takes:

- **The transfers around a window are not counted.** Opening a window at the top waits three
  frames for its tiles and palette to reach video memory (`InitMsgWindow :22976-22983`) and closing
  it one more (`_c19917 :23027`); a frame-taking command here shows its picture from its first
  frame and is gone the frame after its last. Those frames are the hardware's transfer budget, not
  part of the picture, as the HUD's one-cell-a-frame refresh is (`UpdateCharText :9518-9536`): the
  HUD shows the battle as it stands every frame.

## The windows at the top of the screen

### Which window and what it says

The names go in the narrow window, 18 × 4 tiles at column 7, row 1 (`MenuWindowTbl`, window 12);
the messages in the wide one, 30 × 4 at column 1, row 1 (window 11). Both are drawn from the
wallpaper through the battle's window colors (`DrawMsgWindow :12281-12287`: palette 2), on the
battlefield's own layer, above the battlers.

`GfxCmd_01` takes a name kind and a number. The kind's row of `_c197a1` says how the name is read:
kind 0 splices an attack name (`$0f`), kind 1 an item name (`$0e`), kind 5 a command name (`$0c`) —
text commands, read as a battle text reads them, below; kind 2 copies the esper attack name at the
number (`_c2bb46`); kind 4 the Bushido name at the number (`_c2bb1c`); kind 3 nothing. The battle
writes kind 0 for an attack (and for Joker Doom and a Blitz), 1 for an item, 2 for a summon with the
esper's index, 4 for a Bushido technique with its index from Dispatch, 5 for a command. A monster's
special attack is its own command (`GfxCmd_11`): the monster's special name copied up to its length
or `$FF`.

A battle message (`GfxCmd_02`) is the battle message text at its index; monster dialog
(`GfxCmd_10`) the monster dialog at its index.

### How the text appears

The text drawer (`DrawDlgText`) draws every byte from `$20` up as a letter of the dialog font, the
pen advancing by the font's width table (`FontWidth`, indexed by the byte less `$60`); in English
the letter is the byte less `$60` as a dialog glyph. A byte below `$20` is a command:

| Command | Does |
|---|---|
| `$01` | a line break: the line scrolls up one pixel a frame for sixteen frames and is gone, and the pen goes back to the left (`:14432-14454`) |
| `$02 c` | the name of the character whose record's first byte is `c`, up to six letters or `$FF` |
| `$05` | a pause of 60 frames |
| `$06 n` | a pause of `n` frames |
| `$07` | waits for a key; the text goes on once the player presses it, and no hold follows (`w7ee9f5`) |
| `$0c n` | the command name at `n`, up to its length or `$FF`; nothing for `$FF` |
| `$0e n` | the item name at `n` past its symbol, up to its length or `$FF`; for `$FF` a row of blanks the item name's length |
| `$0f n` | below `$36` the spell name at `n` past its symbol; below `$51` the esper name at `n` − `$36`; else (in English) the attack name at `n` − `$51` |
| `$10`, `$11` | the first and second variable as a number, leading zeros dropped, the last digit always shown |
| `$12 t` | a string from a variable: the name of the party member in the slot variable 1 holds (`t` = 0), or the item, attack or command variable 0 holds (1, 2, 3) |

A name, an attack message and a monster's special attack name are drawn whole: every letter at once
(`w7e62ac` = 1 skips the per-letter transfer, `:14659-14661`). Monster dialog is typed: one letter a
frame (`w7e62ac` = 0). Pauses, line breaks and key waits take their frames either way.

Once the text is all down, the window stays up for the message speed's time — `MsgSpeedTbl
[($1d4d >> 4) & 7]`: 32, 48, 64, 80, 96, 112 frames for settings 0 to 5 — unless the text waited for
a key (`mess_wait :22950-22951`). The config menu offers settings 0 to 5 (`config.asm:942-944`); a
new game starts at 2 (`menu_sram.asm:101-104`: `$1d4d = %00101010`). A monster's special attack name
stays 32 frames whatever the setting (`GfxCmd_11 :22759-22760`).

### Where the text sits

The text is on its own layer at tile rows 2 and 3 — the 16-pixel line from line 15 — the wide
window's from tile column 2 across 28 columns (`_c19980 :23083-23099`, English), the narrow window's
from column 9 across 22 (`_c19961 :23058-23072`). A letter's artwork goes four rows into the line
(`DrawDlgLetter :14555`, the first row written at `+$08`) and four pixels left of the pen
(`:14540-14547`, `_c16109 :14459`: whichever half of a tile the pen is in, the artwork lands four
left of it), as the dialog's does. Only the line's tiles show, so text rising out of the line, or
reaching past the window's columns, is not seen.

The narrow window's text is moved right to sit centered: once it is down, by (104 − the pen) / 2
+ 2, the division rounding down (`chg_center :22926-22942`, through the text layer's scroll).

The ink is the player's font color, the shadow black, one pixel right and down (`_c199ac
:23120-23128`).

### The shading

Both windows are darkened by color math (the per-line table `_c2d138`, one entry a line): from line
8, the fixed color is subtracted from the window's layer at intensity 0 for two lines, 1 for the
next two, up to 15 on lines 38 and 39 (`InitMsgWindowHDMA :22531-22556`). The text and the battlers
are not darkened.

## The HUD

### Its windows

The monster names' window is 12 × 8 at column 1 and the party's 18 × 8 at column 13 (`MenuWindowTbl`,
windows 0 and 1), from line 151. From line 151 they are darkened by color math: the fixed color
subtracted from their layer at intensity 0 for three lines, 1 for the next three, and so on
(`InitBattleGfx :2441-2462`); the windows' last line, 214, loses 21 31sts. The text is not
darkened.

### Its text

The text is the small font's glyphs, one a cell, each line on the second of its two tile rows
(`DrawMenuLetter :15494-15504`), placed at the columns of the battle's text placements: monster
names at column 2, character names at 14, HP at 21, the gauge at 25 (`_c2df9e`). Each draws through
the palette its attribute byte names: `$21` (palette 0) unless the text says otherwise
(`LoadMenuText :11431`).

- **The monster names**: each line of the list (below), the monster's name, its record's length,
  then a blank (`MenuText::_0`, `MenuTextCmd_0b :16048-16084`).
- **A party member's name**: six letters of its record's name (`DrawCharInfo_00 :15810-15819`).
- **HP**: four digits from `ZERO_CHAR` (`$B4`), the leading zeros made blank (`$FF`) up to the third,
  so the last digit always shows (`HexToDec4 :15967-15980`, `TrimZeroes4 :3646-3657`).
- **The gauge** (`DrawCharInfo_02 :15869-15902`): with the gauge shown, the left cap, four cells from
  `GaugeTextTbl` at the gauge's fill shifted right by three, the right cap (`DrawGaugeText
  :15850-15864`), in palette 2 when the gauge's byte is `$FF` and palette 5 otherwise (`$29`, `$35`);
  a morphed member shows its Morph gauge the same way in palette 6 (`MenuText::_22..25`, `$39`). The
  gauge's byte is the battler's ATB meter less one (`battle_main.asm:15149-15163`). With the gauge
  turned off: "~" (`$CE`), the max HP in four digits, a blank.

The text layer's scroll ramp puts the lines twelve rows apart from row 163, each showing all eight
pixel rows of its letters (§ The screen).

### The list of monster names

The battle keeps four name slots (`battle_main.asm:15194-15238`, as it works out the run difficulty):
for each monster slot in order whose monster is present, has not died or escaped, is counted in the
battle, and is not petrified, dead or a zombie, a monster not hidden from the list puts its name
group into the first empty slot or counts on the slot already holding it. The HUD shows the slots
when the battle says they changed and no menu is open (`UpdateMonsterNames :322-338`).

## The damage numerals

### Their art

The digits and "Miss" are not the small font: they are four-plane tiles of the status art, which the
battle unpacks with its decompressor to `$7F:A400` when it starts (`InitBattleGfx :2325-2333`) —
"Miss" the two tiles at `$7F:BC00`, digit *d* the tile at `$7F:BC40` + 32·*d* (`GfxCmd_0b :24711`,
`_c1a735 :24989-24991`). Their pixels use colors 12 and 13 only. A numeral draws through palette 4
(`$38`) — party slot 0's, whose colors 12 to 15 are its status colors — or, green for healing, palette
5 (`$3a`), slot 1's (`_c12cc2 :6346-6353`).

### One at a time and all at once

- **One numeral** (`GfxCmd_0b`): one of four threads, taken in turn and cleared as it is taken
  (`:24665-24670`); a frame passes; if another thread's numeral is over the same battler, eight more
  frames pass (`:24674-24681`); the numeral starts and one more frame passes before the script goes on
  (`:24782`). The value's bit 15 makes it green, bit 14 makes it "Miss", the low 14 bits are the amount.
- **Every battler's numeral** (`GfxCmd_03`, `_c1a5fa`): a numeral for each battler with one, started
  together, only over a battler that is shown; then the script waits frame by frame until no numeral
  of either kind is left (`:22514-22524`).

A numeral bounces for 64 frames, lifted by `DmgNumBounceTbl` at its frame, then is gone
(`:6098-6100`, `:6301-6306`). One over a battler that is not shown is dropped (`_c12bfc`).

### Where they sit

A battler's numerals start from its anchor: a monster's center at the top of its bottom tile row
(`_c10659 :1036-1056`: x + width·4, y + 8·height − 8), a party member's center at its feet (`:1088-
1100`: x + 8, y + 16). The digits run left to right from the anchor less the numeral's offset: 4 a
digit, leading zeros dropped (`:24733-24749`, `:24894-24911`), so a number sits centered. "Miss" is
copied one tile into the numeral (`+$20`, `:24712`, `:24864`) with an offset of 8 shown on its own
(`:24717`) and 0 shown with the others (`:24841`), so it starts at the anchor or one tile right of it
([../Bugs.md](../Bugs.md)).

## A monster's death

Between scripts, every frame (`BtlGfx_01`), the monsters shown that the battle no longer counts as
standing (`$2f2f`) die together (`MonsterDeathAnim`): one frame as they were; then their sprites take
palette 3, loaded with `MonsterDeathPal` (`:22243-22248`), at priority 0, and color math adds the
sprites to the background (`:22256-22259`: the sprites on the subscreen, added to the background's
layer), so a dying monster shows added over what is behind it; then 32 frames, the first showing the
death palette as loaded and each next with every channel of its sixteen colors one 31st lower, floored
at 0 (`UpdateMonsterDeathPal`); then the monsters are no longer shown, the color math goes off and
the palettes come back. A monster that comes back without dying is shown again at once
(`:22299-22304`).

## Negatives — not here

The entrance and exit scripts and the step forward; the attack animations, and an animation's
faster pose stepping (`w7e61d0`); the battle events and the battle dialog they show; the menu, its
windows and text, the MP window and its three-digit numbers (`TrimZeroes3`), and the highlight of
the acting member's HUD row (`_c14ec2`); the condemned numerals and gauge; the pose `DrawCharSprite` forces at `:7436-7464`; the
character shadows, status sprites, outlines and skin colors; the imp and Magitek graphics swaps;
the controlled-target flip; the final battle's scrolls; the wavy background; the three background
load variants; the scripted battle positions; the alternate text color (`$04`), which no battle
message, monster dialog or name uses; the Japanese monster-list counts; the BG1 monster path (no
shipped formation sets a BG1 monster, `:2002-2003`).
