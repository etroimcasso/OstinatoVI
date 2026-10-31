# Battle picture tables

The tables a battle's picture is placed and timed by: the frames and poses a party member is
drawn in and the palette each draws through, where the party stands in each battle layout, the
pose a party member waits in before acting, how a damage numeral bounces, how the HUD's gauge
fills, how long a message stays up, where a colosseum monster stands, the glyphs the numerals are
cut from, the battle windows' tiles and places, where the HUD's text sits, and how each kind of
name in the narrow window is read.

None of this is artwork: the frames say which tiles of a character's sprite sheet go where, not
what the tiles look like, and the sheets themselves are read out of the player's cartridge.

## The surface

```cpp
#include "data/battle_gfx_tables.h"

const PoseFrame step = ostinato::characterPoseFrames(/*pose=*/6)[0];   // frame 17, not mirrored
const auto tiles = ostinato::characterFrameTiles(step.frameIndex());  // its eight sheet tiles
ostinato::battleCharacterPalette(CharacterGfxId::SHADOW);             // BattleCharacterPalette::SHADOW
ostinato::partyPlacement(/*layout=*/1)[2].mirrored;                   // true — back attacks face right
ostinato::commandWaitingPose(BattleCommandId::MAGIC);                 // 9
ostinato::damageNumberLift(/*frame=*/9);                              // 16 — the top of the bounce
ostinato::messageHoldFrames(/*setting=*/0);                           // 32
ostinato::battleWindow(BattleWindowId::PARTY_STATUS, Language::EN);  // column 13, 18 x 8
```

| Function | Returns |
|---|---|
| `characterFrameTiles(frame)` | A frame's eight `SheetTile`s |
| `characterFrames()` | All 32 `{ frame, tiles }` entries |
| `characterPoseFrames(pose)` | A pose's four `PoseFrame` steps |
| `characterPoses()` | All 39 `{ pose, steps }` entries |
| `battleCharacterPalette(graphics)` | The battle palette for a character's graphics |
| `battleCharacterPalettes()` | All 24 `{ graphics, palette }` entries |
| `partyPlacement(layout)` | The four party slots for a battle layout |
| `partyPlacements()` | All four `{ layout, slots }` entries |
| `magitekTrainPartyY(slot)` | A slot's y on the Magitek train's background |
| `commandWaitingPose(command)` | The pose a party member waits in before a command |
| `damageNumberLift(frame)` | How far a damage numeral stands raised on a frame of its life |
| `gaugeCellFills(step)` | The four gauge cells' fills at a step |
| `gaugeEmptyGlyph(language)` | The small-font glyph of an empty gauge cell |
| `messageHoldFrames(setting)` | How many frames a message stays up at a message speed |
| `colosseumAlignOffset(alignment)` | How far a colosseum monster stands from its ground line |
| `numeralGlyphs(language)` | The 21 small-font glyphs the numerals are cut from |
| `battleWindowWallpaper()`, `battleWindowBorder()` | The tiles a battle window is drawn with |
| `battleWindows(language)`, `battleWindow(window, language)` | Each window's layer, place and size |
| `battleTextPlacements(language)`, `battleTextPlacement(placement, language)` | Where each run of HUD and menu text sits |
| `nameKindText(kind)`, `nameKinds()` | How each kind of name in the narrow window is read |

## A party member's frames and poses

A party member is drawn in one of 32 frames. Each frame is eight 8 × 8 tiles cut from the
character's field sprite sheet, a `SheetTile` naming the sheet's tile by its place (`tile()`) or
`blank()`:

- **A standing frame** (1 to 31) is 16 pixels wide and 32 tall, its tiles in pairs from the top
  row down: tiles 0-1, 2-3, 4-5, 6-7. Its top row is always blank, so what shows is 16 × 24.
- **The fallen frame** (0) lies on its side: tiles 0-3 are the left 16 × 16 half (0-1 above 2-3),
  tiles 4-7 the right.

A pose is four steps the party member cycles through as its animation counter runs. Each step is a
frame drawn as it is (`PoseFrame::frame`) or mirrored left to right (`PoseFrame::mirrored`); pose
0 shows nothing (`PoseFrame::none`). Pose 6 is the idle pose, frame 17 in every step.

```cpp
for (const PoseFrame step : characterPoseFrames(pose)) {
    if (step.isNone()) continue;
    const auto tiles = characterFrameTiles(step.frameIndex());
    // draw tiles, mirrored when step.isMirrored()
}
```

Each character's graphics slot draws through one of the battle's character palettes. The slot
after the last named graphics, `kGreenSoldierGraphics`, is the soldier's sprites drawn green.

## Where the party stands

The party stands along lines fanning out from a point above the screen. Each slot of each battle
layout — front, back, pincer, side, `layout` 0 to 3 — has:

| Field | Meaning |
|---|---|
| `y` | The slot's place down the screen. |
| `angle` | The slot's line: 256 points straight down, each 128 is 45 degrees, larger values lean right. |
| `backRowAngle` | Added to `angle` when the party member is in the back row. |
| `mirrored` | The party member faces right rather than left. |
| `handsSwapped` | The party member's weapon hands trade sides. |

On the Magitek train's background each slot stands at its own `magitekTrainPartyY(slot)` instead
of the layout's y.

## Waiting to act

Each command has a pose its user takes while waiting to act: Magic, Summon and Health wait in pose
9, Magitek in pose 0, every other command in pose 11. The table covers the 30 named commands and
the eleven the battle can queue past them; `commandWaitingPose` answers for all 41.

## Damage numerals, the gauge, messages, the colosseum

- **A damage numeral** shows for 65 frames (`kDamageNumberFrames`), raised by
  `damageNumberLift(frame)`: up to 16 pixels and back to the ground by frame 23, a smaller second
  bounce, then still.
- **The gauge** is four cells, each filled a pixel column at a time from empty (0) to full
  (`kGaugeCellFull`, 8). A gauge's fill shifted right by 3 is its step, and step *n* fills
  *n* + 1 columns from the left. A cell filled *n* columns is drawn with the small-font glyph *n*
  past `gaugeEmptyGlyph(language)`.
- **A battle message** stays up `messageHoldFrames(setting)` frames: 32 at the fastest of the eight
  message speeds, 16 more for each step slower.
- **A colosseum monster** stands `colosseumAlignOffset(alignment)` from its ground line: 0 for a
  ground monster, −8 buried, +8 floating, −40 flying. A ceiling monster stands at the top of the
  screen, so its row is never read.

## The battle windows

A battle window fills its middle with the wallpaper's 4 × 4 pattern (`battleWindowWallpaper()`)
and draws its border from the wallpaper's edge tiles (`battleWindowBorder()`: top, bottom,
corners, left, right), each part a run of four `WindowTile`s that repeats across or down. The
tiles are the dialog window's own art ([../text/window.md](../text/window.md)).

`battleWindows(language)` gives each of the fifteen windows (`BattleWindowId`) its layer, column,
row, width and height in 8 × 8 cells, border included. The two message windows at the top of the
screen sit on the battlefield's own layer (`BattleTileLayer::BATTLEFIELD`); the rest on the menu
windows' layer. `battleTextPlacements(language)` places each run of text (`BattleTextPlacementId`)
the same way; each line of text takes two rows.

Four of these tables differ between the English and Japanese builds — the windows, the text
placements, the numeral glyphs and the gauge glyph — so their lookups take a `Language`.

## The names in the narrow window

The narrow window at the top of the screen shows one of six kinds of name. `nameKindText(kind)`
says how each is read: through a battle text command that splices a name by its index
(`command()` — attack, item or command names), from the esper-attack or Bushido-technique lists
the text commands do not reach (`isEsperAttackName()`, `isBushidoName()`), or not at all
(`isNothing()`).

## Changing a table

Edit the row in the matching file under `src/data/generated/battle_*_data.inc`:

```cpp
{ .frame = 1, .tiles = { SheetTile::blank(), SheetTile::blank(), SheetTile::at(0), ... } },
{ .pose = 6, .steps = { PoseFrame::frame(17), PoseFrame::frame(17), ... } },
{ BattleCommandId::MAGIC, 9 },
{ .window = BattleWindowId::PARTY_STATUS, .layer = BattleTileLayer::MENU_WINDOWS, .column = 13, .row = 0, .width = 18, .height = 8 },
```

Then update the matching bytes in `tests/fixtures/battle_gfx_tables_expected.h`.
`ostinato-vi-battle-preview-tests` compares every row against them in both builds and re-checks
each table's invariants — a standing frame drawing in its top row, a gauge step that does not fill
its columns left to right, two windows on one layer that disagree about where that layer starts.

## Gotchas

- **A sheet tile is not a frame.** `SheetTile::at(n)` is the sheet's *n*-th 8 × 8 tile; frames
  are made of eight of them.
- **A pose's frame index already has the mirroring taken out.** Use `frameIndex()` to look up the
  frame and `isMirrored()` to flip it; the raw byte holds both.
- **Layouts are numbered.** `partyPlacement` takes the layout as 0 front, 1 back, 2 pincer,
  3 side.
- **Window places are cells on a layer, not screen pixels.** Where a layer sits on the screen is
  the layer's own scroll.
- **The gauge glyph depends on the build.** Add the fill to `gaugeEmptyGlyph(language)`, never to
  a literal.

## Where to change things

| Change | File |
|---|---|
| A frame's tiles | `src/data/generated/battle_character_frames_data.inc` (+ the fixture) |
| A pose's steps | `src/data/generated/battle_character_poses_data.inc` (+ the fixture) |
| A character's battle palette | `src/data/generated/battle_character_palettes_data.inc` (+ the fixture) |
| Where the party stands | `src/data/generated/battle_party_placement_data.inc`, `battle_magitek_train_party_data.inc` (+ the fixture) |
| A command's waiting pose | `src/data/generated/battle_command_waiting_poses_data.inc`, `battle_unnamed_command_waiting_poses_data.inc` (+ the fixture) |
| The numeral's bounce | `src/data/generated/battle_damage_number_bounce_data.inc` (+ the fixture) |
| How the gauge fills | `src/data/generated/battle_gauge_steps_data.inc`, `battle_gauge_empty_glyph_data.inc` (+ the fixture) |
| Message hold times | `src/data/generated/battle_message_speed_data.inc` (+ the fixture) |
| A colosseum offset | `src/data/generated/battle_monster_align_offset_data.inc` (+ the fixture) |
| The numeral glyphs | `src/data/generated/battle_numeral_glyphs_{en,jp}_data.inc` (+ the fixture) |
| A window's tiles | `src/data/generated/battle_window_wallpaper_data.inc`, `battle_window_border_data.inc` (+ the fixture) |
| A window's or text run's place | `src/data/generated/battle_windows_{en,jp}_data.inc`, `battle_text_placements_{en,jp}_data.inc` (+ the fixture) |
| How a name kind is read | `src/data/generated/battle_name_kinds_data.inc` (+ the fixture) |
| The entry types or accessors | `src/data/battle_gfx_tables.h`, `src/data/battle_gfx_tables.cpp` |

## See also

- [battle-backgrounds.md](battle-backgrounds.md) — the battle backgrounds the party stands on
- [monster-sprites.md](monster-sprites.md) — how big each monster stands
- [monster-tables.md](monster-tables.md) — each monster's vertical alignment
- [battle-commands.md](battle-commands.md) — the commands the waiting poses are keyed by
- [../text/window.md](../text/window.md) — the window art and colors these windows draw with
