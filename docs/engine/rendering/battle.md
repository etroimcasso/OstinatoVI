# The battle's picture

Drawing a battle: the background on the battlefield, each monster and party member where the
battle stands it and in the pose it stands in, in the order the battle draws them; the windows at
the top of the screen with a name or a message; the HUD with the list of monster names and the
party's names, HP and gauges; the damage numerals; and a monster fading out. Read this to put a
battle on screen or to change how one looks.

The components here draw what [`docs/engine/assets/battle-art.md`](../assets/battle-art.md)
decodes and what the battle's presentation ([`../battle/presentation.md`](../battle/presentation.md))
says is shown, placed by the rules in
[`docs/contracts/battle_presentation.md`](../../contracts/battle_presentation.md). Every one is a
function named for the thing it draws, taking the state it shows and the uploaded sheets, and
returning engine primitives ([components.md](components.md)). `tools/previews/battle/` is a whole
screen built from them.

## Files

| File | Holds |
|---|---|
| `src/render/battle/layout.h` | Where the picture sits: the lines the battle leaves black, the battlefield's 151 lines and the background's one-row scroll, the HUD from line 151 and its text lines at 163 + 12·n, the message text's place, and both shadings' bands |
| `src/render/screen_border.h` | `ScreenBorder`, the black border around the picture |
| `src/render/battle/battle_placement.h` | Where each battler stands and the draw order — `monsterStanding`, `colosseumMonsterStanding`, `partyMemberStanding`, `partyMemberX`, `battlerDrawOrder` |
| `src/render/battle/battle_background.h` | `BattleBackground`, the battlefield's cells; `BackgroundSheet` |
| `src/render/battle/monster_sprite.h` | `MonsterSprite` |
| `src/render/battle/monster_death_fade.h` | `MonsterDeathFade`, a dying monster's sprites |
| `src/render/battle/character_sprite.h` | `CharacterSprite`; `CharacterSheet` |
| `src/render/battle/menu_window.h` | `MenuWindow`, a battle window's cells |
| `src/render/battle/battle_message.h` | The windows at the top of the screen: `MessageWindowBox`, `MessageShading`, `MessageText`, `MessageTextClip` and the windows' places |
| `src/render/battle/hud_windows.h` | `HudWindows`, the HUD's two windows; `HudShading` |
| `src/render/battle/hud_text.h` | `HudText`, a run of small-font letters; `hudLineY` |
| `src/render/battle/monster_name_list.h` | `MonsterNameList` |
| `src/render/battle/party_status.h` | `PartyStatus`, the party's names, HP and gauges, and the HUD's palette numbers |
| `src/render/battle/damage_numbers.h` | `DamageNumbers`; `NumeralSheet`; `numeralAnchor` |
| `src/render/layers.h` | The battle's depths (`kBattleBackdropZ` … `kDamageNumbersZ`) and keys |

## The screen

```
          x 0  8                          248 256
line 0   ┌──┬──────────────────────────────┬──┐   lines 0-3: black
line 4   │  │                              │  │
line 8   │  │ ┌──────────────────────────┐ │  │   a name or a message while one is up
line 40  │  │ └──────────────────────────┘ │  │
         │  │  the battlefield: the        │  │   the background from its second pixel row,
         │  │  background and the battlers │  │   its 19 tile rows ending on line 150
line 151 │  ├──────────────────────────────┤  │
         │  │  the HUD: two windows of     │  │   text lines at 163, 175, 187, 199
line 214 │  │  eight tile rows             │  │
line 219 │  └──────────────────────────────┘  │   lines 219-223: black
line 223 └────────────────────────────────────┘
```

`ScreenBorder` cuts the whole frame to columns 8-247 and lines 4-218, so the engine's black
backdrop shows around it — on every layer and every sprite at once, since it goes on the frame:

```cpp
#include "render/battle/layout.h"
#include "render/screen_border.h"

retropp::FrameDrawState frame{.regions = ScreenBorder(kBattleTopBlankRows, kBattleBottomBlankRows)};
```

The side columns are always `kScreenBorderPx`; the blank lines at the top and bottom are the
screen's own, four and five for the battle.

The battle's backdrop — a layer of one color at `kBattleBackdropZ` — shows wherever nothing else
draws inside the border: the background's see-through pixels and the lines below the HUD's
windows. Its color is the first color of the first small-font palette, as the battle's is; it is
black, the same as the border.

Depths and keys, from `layers.h`:

| Depth | What | Keys |
|---|---|---|
| the frame | the screen border | `border` |
| `kBattleBackdropZ` 0 | the backdrop | `bfback` |
| `kBattlefieldZ` 1 | the background | `bfbg` |
| `kBattlerBaseZ` 10 + rank | one layer per battler, rank 0 at the back | `mon:<slot>`, `chr:<slot>`; a dying monster's darkening `death` |
| `kMessageWindowZ` 20 | the window at the top of the screen | `msgwin`; its shading `msgsh:<row>` |
| `kMessageTextZ` 21 | that window's text | `msgtext`; glyph `msg:<n>`; its clip `msgclip` |
| `kHudWindowZ` 22 | the HUD's windows | `hudwin`; their shading `hudsh:<row>` |
| `kHudTextZ` 23 | the HUD's text | `hudtext`; glyphs `hud:mon:<line>:<n>`, `hud:name:<slot>:<n>`, `hud:hp:<slot>:<n>`, `hud:gauge:<slot>:<n>` |
| `kDamageNumbersZ` 24 | the damage numerals | `dmglayer`; tiles `dmg:<battler>:<n>`, `dmg1:<thread>:<n>` |

The dialog box and text keep their depths above these.

## The background

```cpp
#include "render/battle/battle_background.h"

const BattleBackgroundPicture picture = content.battleBackgrounds.load(id, *content.routines);
const BackgroundSheet sheet = /* uploaded from the picture, below */;
frame.layers.push_back(TileLayer(kBattlefieldKey, kBattlefieldZ, BattleBackground(picture, sheet),
                                 kBackgroundColumns, kBackgroundRowsShown,
                                 {.scroll = {.y = kBattlefieldScrollPx}}));
```

`BattleBackground` returns the tilemap's first 19 rows as cells, 32 across: each cell names the
sheet, the tile its number maps to (`sheetTile`), the palette its two bits name, and its flips. A
cell whose number no block holds draws the sheet's blank tile. The battle shows the tilemap from
its second pixel row, so the layer scrolls down by `kBattlefieldScrollPx` and its 19 rows end on
line 150, the battlefield's last.

`BackgroundSheet` is the picture's tiles uploaded as one tileset with a blank tile appended, and
the three palettes:

```cpp
// The tiles, sixteen to a row, with one blank tile after them; pixel 0 is a hole.
text::IndexedImage image = assets::blankImage(16 * 8, rows * 8);
for (int tile = 0; tile < tiles; ++tile) {
    assets::drawTile(image, {picture.tiles.data() + tile * 32, 32}, assets::TileDepth::FourPlanes,
                     tile % 16 * 8, tile / 16 * 8);
}
BackgroundSheet sheet{
    .tiles = renderer.uploadAtlas(image.pixels.data(), image.width, image.height, {8, 8},
                                  retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown,
                                  tiles + 1, retropp::TransparentIndices::of({0})),
    .blankTile = static_cast<std::uint16_t>(tiles),
};
for (std::size_t p = 0; p < kBackgroundPalettes; ++p) {
    sheet.palettes[p] = renderer.uploadPalette(/* picture.colors[16p .. 16p+15] as Rgba8 */);
}
```

Pixel 0 is a hole in the sheet, so the backdrop shows through it; the tilemap's priority bit is
not read, since every battler is drawn above the background.

## Where the battlers stand

`battle_placement.h` turns the battle's state into pixels. Nothing in it draws.

```cpp
#include "render/battle/battle_placement.h"

const MonsterStanding lobo = monsterStanding(state.battleType, formation.positionX(0),
                                             formation.positionY(0), size, monsterOverlap(MonsterId::LOBO));
// lobo.x, lobo.y: the sprite's top-left; lobo.mirrored; lobo.bottomEdge for the draw order

const PartyStanding terra = partyMemberStanding(state.battleType, /*slot=*/0, battler.backRow,
                                                battler.actsAsEnemy, onTheMagitekTrain, state.colosseum);
// terra.x, terra.y: the point the frame is placed by; terra.mirrored; terra.bottomEdge
```

- **A monster** stands where its formation puts it, moved and mirrored by the battle type exactly
  as the battle's own layout decides (`battle::placeMonster`,
  [../battle/layout.md](../battle/layout.md)); unmirrored art faces right. Its feet, for the draw
  order, are its bottom edge less one tile plus its own overlap shift. The colosseum's one monster
  stands centered with its feet on the ground line, raised or lowered by its alignment;
  `colosseumMonsterStanding` does that one.
- **A party member** stands at the y its slot and the battle type give it
  (`partyPlacement`), on a line fanning out from a point above the screen; `partyMemberX` is the
  original's arithmetic to the pixel. A back-row member leans its line further out, a member
  fighting against the party faces the other way, the Magitek train's background has its own y per
  slot, and the colosseum's fighter stands at y 104. Unmirrored art faces left; its feet are 16
  below its y.
- **The draw order** is `battlerDrawOrder(bottomEdges)`: back to front by feet, the battler whose
  feet are lowest on the screen in front; level feet keep slot order with the earlier slot in
  front. Give each battler its own layer at `kBattlerBaseZ` plus its place in that order.

Which monsters stand on the field is the presentation's `monstersShown`, not the battle's
`monstersStanding`: a monster that has fallen stays shown until it has faded out.

## The battlers

```cpp
#include "render/battle/monster_sprite.h"
#include "render/battle/character_sprite.h"

// A monster: one sprite, its whole image, mirrored as one.
SpriteLayer("mon:0", kBattlerBaseZ + rank,
            {MonsterSprite("mon:0", lobo.x, lobo.y, lobo.mirrored, loboImage, loboPalette, 0)});

// A party member in the pose it stands in, at its pose counter's step.
const std::uint8_t pose = battle::partyMemberPose(battler, shown.waitingPoses[0]);
const PoseFrame step = characterPoseFrames(pose)[battle::poseStep(shown.poseCounters[0])];
SpriteLayer("chr:0", kBattlerBaseZ + rank,
            {CharacterSprite("chr:0", terra.x, terra.y, step.frameIndex(),
                             terra.mirrored != step.isMirrored(), terraSheet, terraPalette, 0)});
```

`MonsterSprite` takes the monster's image uploaded as one cell (`ContentKind::Single`, pixel 0 a
hole) and its sixteen colors as a palette. `CharacterSprite` takes a `CharacterSheet` — the
standing frames uploaded as 32 cells of 16 × 32 (`ContentKind::SpriteSeries`) and the fallen frame
as one cell of 32 × 16 — and a frame: a standing frame is placed `kStandingFrameRaisePx` above the
point so its blank top row sits above it; the fallen frame lies `kFallenFrameDropPx` below and
`kFallenFrameLeftPx` to the left, or `kFallenFrameMirroredLeftPx` when mirrored. A frame's own
mirroring (`PoseFrame::isMirrored`) and the battler's facing combine with exclusive or. A pose step
that shows nothing (`PoseFrame::isNone`) draws no sprite.

Keys name the battler — `mon:<slot>`, `chr:<slot>` — never its place in the draw order, so a
battler stepping in front of another stays the same object to the engine.

### A monster fading out

```cpp
#include "render/battle/monster_death_fade.h"

const std::optional<battle::DeathFade>& fade = shown.deathFade;
const bool dying = fade && fade->dying.has(slot) && fade->inDeathPalette();
Sprites monster = MonsterSprite(key, at.x, at.y, at.mirrored, image, dying ? deathColors : palette, 0);
if (dying) {
    monster = MonsterDeathFade(std::move(monster), *fade);
}
```

While it fades, the monster draws through the death palette (`content.monsterDeathColors`,
uploaded once), each sprite added over what is behind it, its colors taken down by
`fade->darkening()` 31sts per channel — 0 on the first frame in the death palette, 31 on the last.
The darkening is a `ColorFill` region under a Subtract blend on each sprite, so it lands on the
monster's own pixels only.

## The windows

```cpp
#include "render/battle/menu_window.h"

const BattleWindowEntry& box = battleWindow(BattleWindowId::MONSTER_NAMES, Language::EN);
Cells cells = MenuWindow(box.width, box.height, wallpaperSheet, battleWindowColors);
```

`MenuWindow` returns a box of the wallpaper's tiles, `columns` by `rows`, border included: the
four corners, the top and bottom edges alternating their two tiles from the left, the left and
right edges alternating theirs from the top, and inside them the wallpaper's 4 × 4 pattern with its
first column at the window's second. The sheet and colors are the dialog window's
([../text/window.md](../text/window.md)), the colors brightened for battle by `battleWindowColors`.
Where each window sits is `battleWindow` ([../data-layer/battle-gfx-tables.md](../data-layer/battle-gfx-tables.md)).

### The window at the top of the screen

```cpp
#include "render/battle/battle_message.h"

if (shown.message) {
    const battle::ShownMessage& message = shown.message->shown;
    frame.layers.push_back(TileLayer(kMessageWindowKey, kMessageWindowZ,
                                     MessageWindowBox(message.window, lang, wallpaper, windowColors),
                                     messageWindowColumns(message.window, lang),
                                     messageWindowRows(message.window, lang),
                                     {.scroll = messageWindowScroll(message.window, lang),
                                      .regions = MessageShading(message.window, lang)}));
    frame.layers.push_back(SpriteLayer(kMessageTextKey, kMessageTextZ,
                                       {MessageText(message, glyphs, ink, shadow)},
                                       {.regions = MessageTextClip(message)}));
}
```

A name shows in the narrow window, 18 × 4 tiles at column 7, row 1; a message in the wide one,
30 × 4 at column 1, row 1. `MessageShading` darkens the window a 31st more every two lines from
line 8, over the window's own rectangle. `MessageText` draws each letter in the dialog font with its
shadow (`Glyph`, [components.md](components.md)), placed against the pen as the dialog's letters
are: the wide window's pen from tile column 2, the narrow window's from column 9 moved right by the
message's `centering`, a letter's artwork four rows into the 16-pixel line at line 15 and
`text::kGlyphArtLeftOfPen` left of its pen, raised by its `rise` while a line break scrolls it
away. `MessageTextClip` keeps only the line's tiles — 28 columns in the wide window, 22
in the narrow — so a rising line disappears at the top of its line.

### The HUD

```cpp
#include "render/battle/hud_windows.h"
#include "render/battle/hud_text.h"
#include "render/battle/monster_name_list.h"
#include "render/battle/party_status.h"

frame.layers.push_back(TileLayer(kHudWindowKey, kHudWindowZ, HudWindows(lang, wallpaper, windowColors),
                                 hudWindowsColumns(lang), kHudWindowRows,
                                 {.scroll = hudWindowsScroll(lang), .regions = HudShading(lang)}));
frame.layers.push_back(SpriteLayer(kHudTextKey, kHudTextZ,
                                   {MonsterNameList(shown.monsterNames, content.text, lang,
                                                    fontPalettes[kHudTextPalette], smallFont),
                                    PartyStatus(battle, fontPalettes, smallFont)}));
```

`HudWindows` lays the monster names' window and the party's window side by side into one grid,
placed at the HUD's top line, 151; `HudShading` darkens them a 31st more every three lines from
line 151, to 21 31sts on their last line, 214.

The HUD's letters are the small font's 8 × 8 glyphs, each one sprite (`HudText`), drawn through one
of the font's eight palettes (`text::smallFontPalettes`, uploaded in order as `fontPalettes`). The
four text lines fall twelve pixels apart from line 163 (`hudLineY`), every letter shown whole.
`kHudBlank` holds a place and draws nothing.

- `MonsterNameList` writes each line of the presentation's name list at the monster names' column.
- `PartyStatus` writes one line per party slot with a member on the field: the name, the HP in four
  places with the leading zeros blank, and the gauge — its caps around four cells, palette 2 when
  full and 5 while filling, palette 6 for a member in Morph, or "~" and the max HP when the gauge
  is turned off.

### Damage numerals

```cpp
#include "render/battle/damage_numbers.h"

NumeralAnchors anchors{};
anchors[slot.index()] = numeralAnchor(monsterStanding);  // or numeralAnchor(partyStanding)
frame.layers.push_back(SpriteLayer(kDamageNumbersKey, kDamageNumbersZ,
                                   {DamageNumbers(shown, anchors, numeralSheet)}));
```

Every numeral the presentation shows is drawn from its battler's anchor — a monster's center at the
top of its bottom tile row, a party member's center 16 below its point — its digits left to right
from the anchor less the numeral's offset, lifted by `damageNumberLift(frame)`. "Miss" is two cells
and starts one cell further right than its offset. White numerals draw through `numerals.white`,
healing ones through `numerals.green`. A battler with no anchor shows no numeral.

`NumeralSheet` is the numerals' art ([../assets/battle-art.md](../assets/battle-art.md)) uploaded as
twelve 8 × 8 cells — the digits 0 to 9, then "Miss" — with pixel 0 a hole, and two palettes of
sixteen colors whose last four are party slot 0's and slot 1's status colors (`statusColors`).

## A whole screen

`tools/previews/battle/battle_preview.cpp` puts it together: the backdrop, the background, one
`SpriteLayer` per battler in draw order, the window at the top while one is up, the HUD and the
numerals; `tools/previews/battle/main.cpp` puts them on a frame with the screen border. Run `ostinato-preview-battle --capture <dir> 0 41 24` to fight Lobo, a pincer and
a back attack to their ends and write the moments worth checking to `<dir>`.

### Reference frames from the cartridge

`ostinato-frames` (`tools/frames/main.cpp`) is what the preview's captures are compared against. It
hosts the cartridge on the engine's SNES machine with its video on, shows the machine's picture in a
window at 3×, and takes the pad from the keyboard (arrows = d-pad, X = A, Z = B, S = X, A = Y, Q/W =
L/R, Enter = Start, Right Shift = Select). Escapes on the cartridge's own code mark the moments to
record; you choose them on the command line, at least one:

```sh
ostinato-frames <rom> <directory> --record both --battles --gameloaded --fieldmenu
```

`--record frames|durations|both` chooses whether it writes frames, the length of each span, or both;
without it, frames are recorded.

| Trigger | Records | Files |
|---|---|---|
| `--battles` | each battle, from the battle program's entry (`$C2:0000`) to its return to the field (`$C2:00CB`) | `battle<n>-f<offset>.ppm`, the last one `-end` |
| `--gameloaded` | from the moment the field is set up from a loaded save (`$C0:BD20`) until the window closes | `game<n>-f<offset>.ppm` |
| `--fieldmenu` | each opening of the field's main menu, from `$C0:C636` to its return to the field at `$C0:C651` | `menu<n>-f<offset>.ppm`, the last one `-end` |

When recording frames, every fifteenth frame of an open span is written 1:1 as a 256 × 224 PPM.
`<directory>/frames.log` gets a line per frame, and a battle's lines also carry the scroll values the
battle keeps in work RAM. When recording durations, `<directory>/durations.log` gets a line per span:
its length in frames and in milliseconds, and the frames it began and ended on. The tool's own
[README](../../../tools/frames/README.md) has the details. Every escape is printed and logged as it fires, with its address and frame
number. The addresses are the US cartridges'. The machine keeps the cartridge's battery save between
runs, so a game saved in the window is there to load next time.

## Gotchas

- **Every battler is its own layer.** A blend meant for one battler then touches only it, and the
  engine eases nothing between battlers.
- **Keep a shading region on its window.** A `ColorFill` region on a layer also lands on the
  layer's see-through pixels inside it, so `MessageShading` and `HudShading` cover only their
  windows' rectangles, and the death fade's darkening rides on the monster's sprites rather than on
  its layer.
- **The sheets are the caller's.** No component uploads anything; the preview's `uploadFixedSheets`
  and `addBattleSheets` show the carves and holes each component expects, and upload each sheet
  once. A sheet holds its room in the renderer's store until `releaseAtlas` retires it.
- **A shown cell names palette 5, 6 or 7.** `BattleBackground` asserts it; every cartridge
  background's shown rows do.
- **The HUD's palettes are numbered as the battle numbers them.** `PartyStatus` indexes
  `fontPalettes` by `kHudTextPalette`, `kFullGaugePalette`, `kFillingGaugePalette` and
  `kMorphGaugePalette`, so upload all eight in order.

## Where to change things

| To do this | Edit |
|---|---|
| Move the HUD, its text lines, the message text or a shading's bands, or change the battle's blank lines | `src/render/battle/layout.h` |
| Change the border's side columns | `kScreenBorderPx` in `src/render/layers.h` |
| Change where a battler stands | `src/render/battle/battle_placement.cpp` — the formation's places and the party's lines are data (`partyPlacement`) |
| Change how a frame sits about its point | the constants in `src/render/battle/character_sprite.h` |
| Change what a HUD line shows | `src/render/battle/party_status.cpp`, `monster_name_list.cpp` |
| Change how a numeral is laid out | `src/render/battle/damage_numbers.cpp` |
| Add a battle component | A function named for it under `src/render/battle/`, its depth and key in `src/render/layers.h` |

## See also

- [components.md](components.md) — the component contract, `SpriteLayer`, `TileLayer`, `LayerOptions`, `ColorGradient`
- [../battle/presentation.md](../battle/presentation.md) — what is shown, frame by frame
- [../assets/battle-art.md](../assets/battle-art.md) — the pictures these draw
- [../battle/layout.md](../battle/layout.md) — the battle's own layout, which the placement follows
- [../../contracts/battle_presentation.md](../../contracts/battle_presentation.md) — the rules and where each comes from
