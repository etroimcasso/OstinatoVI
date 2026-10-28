# The battle's picture

Drawing a battle: the background on the battlefield, each monster and party member where the
battle stands it, in the order the battle draws them, and the windows the HUD and the messages are
boxed in. Read this to put a battle on screen or to change how one looks.

The components here draw what [`docs/engine/assets/battle-art.md`](../assets/battle-art.md)
decodes, placed by the rules in
[`docs/contracts/battle_presentation.md`](../../contracts/battle_presentation.md). Every one is a
function named for the thing it draws, taking the state it shows and the uploaded sheets, and
returning engine primitives ([components.md](components.md)). `tools/previews/battle/` is a whole
screen built from them.

## Files

| File | Holds |
|---|---|
| `src/render/battle/layout.h` | Where the picture sits on the screen: the battlefield's 151 lines, the HUD from line 152, its text lines at 164 + 12·n |
| `src/render/battle/battle_placement.h` | Where each battler stands and the draw order — `monsterStanding`, `colosseumMonsterStanding`, `partyMemberStanding`, `partyMemberX`, `battlerDrawOrder` |
| `src/render/battle/battle_background.h` | `BattleBackground`, the battlefield's cells; `BattlefieldClip`, the line beneath it; `BackgroundSheet` |
| `src/render/battle/monster_sprite.h` | `MonsterSprite` |
| `src/render/battle/character_sprite.h` | `CharacterSprite`; `CharacterSheet` |
| `src/render/battle/menu_window.h` | `MenuWindow`, a battle window's cells |
| `src/render/layers.h` | The battle's depths (`kBattleBackdropZ` … `kDamageNumbersZ`) and keys (`bfback`, `bfbg`, `bfclip`, `mon`, `chr`, `msgwin`, `hudwin`) |

## The screen

```
line 0   ┌──────────────────────────────┐
         │  the battlefield: the        │   the background's first 19 tile rows, cut off
         │  background and the battlers │   one line short; the battlers over it
line 151 ├──────────────────────────────┤   one line of the backdrop
line 152 │  the HUD: two windows of     │
         │  eight tile rows             │   text lines at 164, 176, 188, 200
line 223 └──────────────────────────────┘
```

The backdrop — a layer of one color at `kBattleBackdropZ` — shows wherever nothing else draws: the
background's see-through pixels, line 151, and the HUD until its windows are drawn. Its color is
the first color of the first small-font palette, as the battle's is.

Depths, from `layers.h`:

| Depth | What |
|---|---|
| `kBattleBackdropZ` 0 | the backdrop |
| `kBattlefieldZ` 1 | the background |
| `kBattlerBaseZ` 10 + rank | one layer per battler, rank 0 at the back |
| `kMessageWindowZ` 20, `kMessageTextZ` 21 | the message windows and their text |
| `kHudWindowZ` 22, `kHudTextZ` 23 | the HUD's windows and text |
| `kDamageNumbersZ` 24 | the damage numerals |

The dialog box and text keep their depths above these.

## The background

```cpp
#include "render/battle/battle_background.h"

const BattleBackgroundPicture picture = content.battleBackgrounds.load(id, *content.routines);
const BackgroundSheet sheet = /* uploaded from the picture, below */;
frame.layers.push_back(TileLayer(kBattlefieldKey, kBattlefieldZ, BattleBackground(picture, sheet),
                                 kBackgroundColumns, kBackgroundRowsShown,
                                 {.regions = BattlefieldClip()}));
```

`BattleBackground` returns the tilemap's first 19 rows as cells, 32 across: each cell names the
sheet, the tile its number maps to (`sheetTile`), the palette its two bits name, and its flips. A
cell whose number no block holds draws the sheet's blank tile. The nineteenth row reaches one line
below the battlefield, so `BattlefieldClip` — one see-through region over line 151 — goes on the
same layer.

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

## The battlers

```cpp
#include "render/battle/monster_sprite.h"
#include "render/battle/character_sprite.h"

// A monster: one sprite, its whole image, mirrored as one.
SpriteLayer("mon:0", kBattlerBaseZ + rank,
            {MonsterSprite("mon:0", lobo.x, lobo.y, lobo.mirrored, loboImage, loboPalette, 0)});

// A party member in its idle pose: one 16 x 32 sprite placed eight above its point.
const PoseFrame idle = characterPoseFrames(/*pose=*/6)[0];
SpriteLayer("chr:0", kBattlerBaseZ + rank,
            {CharacterSprite("chr:0", terra.x, terra.y, idle.frameIndex(),
                             terra.mirrored != idle.isMirrored(), terraSheet, terraPalette, 0)});
```

`MonsterSprite` takes the monster's image uploaded as one cell (`ContentKind::Single`, pixel 0 a
hole) and its sixteen colors as a palette. `CharacterSprite` takes a `CharacterSheet` — the
standing frames uploaded as 32 cells of 16 × 32 (`ContentKind::SpriteSeries`) and the fallen frame
as one cell of 32 × 16 — and a frame: a standing frame is placed `kStandingFrameRaisePx` above the
point so its blank top row sits above it; the fallen frame lies `kFallenFrameDropPx` below and
`kFallenFrameLeftPx` to the left, or `kFallenFrameMirroredLeftPx` when mirrored. A frame's own
mirroring (`PoseFrame::isMirrored`) and the battler's facing combine with exclusive or.

Keys name the battler — `mon:<slot>`, `chr:<slot>` — never its place in the draw order, so a
battler stepping in front of another stays the same object to the engine.

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
Where each window sits is `battleWindow` ([../data-layer/battle-gfx-tables.md](../data-layer/battle-gfx-tables.md)):
the HUD's two windows share one tile layer scrolled to line `kHudTopPx`, the message windows one on
the battlefield's grid.

## A whole screen

`tools/previews/battle/battle_preview.cpp` puts it together: the backdrop, the background with its
clip, then one `SpriteLayer` per battler in draw order. Run `ostinato-preview-battle --capture
<dir> 0 41` to write the standing scenes of the first formation and a pincer to `<dir>`.

## Gotchas

- **Every battler is its own layer.** A fade or a blend meant for one battler then touches only
  it, and the engine eases nothing between battlers. Use the layer's `LayerOptions` for that.
- **The sheets are the caller's.** No component uploads anything; the preview's `uploadSheets`
  shows the carves and holes each component expects.
- **A shown cell names palette 5, 6 or 7.** `BattleBackground` asserts it; every cartridge
  background's shown rows do.

## Where to change things

| To do this | Edit |
|---|---|
| Move the HUD or its text lines | `src/render/battle/layout.h` |
| Change where a battler stands | `src/render/battle/battle_placement.cpp` — the formation's places and the party's lines are data (`partyPlacement`) |
| Change how a frame sits about its point | the constants in `src/render/battle/character_sprite.h` |
| Add a battle component | A function named for it under `src/render/battle/`, its depth and key in `src/render/layers.h` |

## See also

- [components.md](components.md) — the component contract, `SpriteLayer`, `TileLayer`, `LayerOptions`
- [../assets/battle-art.md](../assets/battle-art.md) — the pictures these draw
- [../battle/layout.md](../battle/layout.md) — the battle's own layout, which the placement follows
- [../../contracts/battle_presentation.md](../../contracts/battle_presentation.md) — the rules and where each comes from
