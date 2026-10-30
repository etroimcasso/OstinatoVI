# Components

Every component is a function in its own file under `src/render/`, named for the thing it draws.
It takes the state it shows as ordinary parameters and returns engine primitives. Called with the
same arguments it returns the same result, and it remembers nothing between frames.

```cpp
#include "render/layers.h"
#include "render/sprite_layer.h"
#include "render/text/dialog_cursor.h"
#include "render/text/dialog_text.h"
#include "render/text/dialog_wipe.h"

using namespace ostinato::render;

frame.layers.push_back(SpriteLayer(
    "dialog", kDialogTextZ,
    {DialogText(dialog, textX, textY, pitch, glyphSheet, inkPalette, shadowPalette),
     DialogCursor(dialog, textX, textY, pitch, arrowSheet, inkPalette, shadowPalette)},
    {.regions = DialogWipe(dialog, textX, textY, pitch)}));
```

That one statement is the whole dialog text for a frame: the page's glyphs and their shadows,
the choice arrow, and the part of a dismissed page that has already gone. The box behind it is a
second layer, gathered the same way ([The dialog window components](#the-dialog-window-components)).

## The shared vocabulary

### `types.h`

```cpp
using Sprites = std::vector<retropp::Sprite>;
using Cells   = std::vector<retropp::TileCell>;
using Regions = std::vector<retropp::Region>;
using Layers  = std::vector<retropp::DrawLayer>;
```

What components return. A leaf returns `Sprites`, `Cells` (tiles, row by row, for a tile layer)
or `Regions` (for a component that confines an effect to part of a layer); a parent returns what
its children returned.

### `layers.h`

| Name | What it is |
|---|---|
| `kScreenSize` | The screen, 256 × 224 (`retropp::ViewportResolution::Snes`). Every sprite and tile layer covers it. |
| `kDialogWindowZ` | The depth of the dialog box and the map-title box: 25, beneath the text they hold. |
| `kDialogTextZ` | The depth of the dialog text, its arrow and the map title: 30. The field's world and sprite layers take the depths below the box. |
| `kShadowSpriteZ`, `kInkSpriteZ` | Within a sprite layer, every drop shadow (0) sits beneath every piece of ink (1), so no shadow ever darkens a letter. |
| `kDialogGlyphKey`, `kTitleGlyphKey`, `kDialogCursorKey`, `kDialogWipeKey`, `kShadowKeySuffix` | The names sprites and regions go by. See [Keys](#keys). |
| `kDialogWindowKey`, `kWindowShadingKey`, `kWindowClipKey` | The box's layer, its shading regions and its clip. |
| `kBattleBackdropZ` … `kDamageNumbersZ` | The battle's depths, 0 to 24, beneath the dialog box: see [battle.md](battle.md). |
| `kBattleBackdropKey`, `kBattlefieldKey`, `kBattlefieldClipKey`, `kMonsterKey`, `kCharacterKey`, `kMessageWindowKey`, `kMessageShadingKey`, `kMessageTextKey`, `kMessageGlyphKey`, `kMessageTextClipKey`, `kHudWindowKey`, `kHudShadingKey`, `kHudTextKey`, `kHudGlyphKey`, `kHudTextClipKey`, `kDamageNumbersKey`, `kDamageNumeralKey`, `kSingleNumeralKey`, `kDeathFadeKey` | The battle's layers, regions and sprites: see the key table in [battle.md](battle.md). |

A new layer takes its depth from this table, and a new kind of sprite takes its key prefix from
it, so two components never collide.

### Keys

The engine recognizes an object from one frame to the next by its key: an object that keeps
its key eases to wherever it is now, and a new key appears where it is put. A key therefore
names the thing, never its position in a list.

| Object | Key | So that |
|---|---|---|
| A dialog glyph | `dlg:<page>:<line>:<n>` — the *n*-th glyph of a line | A glyph on the next page is a new object, not this one moved. The page count runs on for the life of the dialog state. |
| A map title glyph | `title:<n>` | — |
| The choice arrow | `dlgc:<page>:<choice>` | Moving the selection puts the arrow straight at the new choice instead of sliding it. |
| A wiped half-line | `dlgw:<line>:<half>` | — |
| The dialog box's layer | `dlgwin` | — |
| A run of the box's shading | `dlgsh:<first row of the run>` | — |
| The box's clip | `dlgclip` | — |
| Any shadow | its sprite's key + `:shadow` | — |

## `SpriteLayer`

```cpp
retropp::DrawLayer SpriteLayer(std::string_view key, std::int32_t z,
                               std::initializer_list<Sprites> children,
                               LayerOptions options = {});
```

Gathers sprite components onto one screen-sized layer. The children are declared as a list and
drawn in that order, after each sprite's own `z`. The layer's key, depth, size and content
are SpriteLayer's; everything else comes from `options`.

### `LayerOptions`

| Field | Default | |
|---|---|---|
| `scroll` | `{0, 0}` | Offsets the whole layer. |
| `alpha` | `1.0` | Fades the whole layer. |
| `blend` | `Normal` | How the layer combines with what is beneath it. |
| `effects` | none | Effects over the whole layer. |
| `regions` | none | Effects confined to shapes — `DialogWipe`'s see-through rectangles go here. |
| `transform` | identity | Scales, rotates, skews or warps the layer. |
| `transformEdge` | `Blank` | What a transformed layer shows where it no longer covers. |

Every default is the engine's own, so a layer given no options draws exactly as a plain
`retropp::DrawLayer` would.

### Gotchas

- **The layer's sprites are a view.** The renderer reads them while it draws, after the
  children you passed have gone. SpriteLayer keeps its own copy under `key` until the next call
  with that key, so the returned layer stays valid for the frame.
- **One call per key per frame.** A second call with the same key replaces what the first
  kept, leaving both layers showing the second one's sprites. The engine already requires a
  layer's key to be unique within a frame.

## `TileLayer`

```cpp
retropp::DrawLayer TileLayer(std::string_view key, std::int32_t z, Cells cells,
                             int widthInTiles, int heightInTiles, LayerOptions options = {});
```

Gathers tile cells onto one screen-sized, finite tile layer. `cells` is a row-major grid
`widthInTiles` across and `heightInTiles` down, each cell naming its own sheet, tile and palette.
The grid is finite (`retropp::TileWrap::Blank`): outside it the layer shows nothing and the layers
beneath show through. `options.scroll` places the grid — screen pixel `(x, y)` shows grid pixel
`(x + scroll.x, y + scroll.y)`, so a scroll of `{-8, -144}` puts the grid's top-left corner at
`(8, 144)`. The layer's key, depth, size and content are TileLayer's; everything else comes from
`options`.

Its cells are a view the renderer reads, so TileLayer keeps them under `key` until the next call
with that key, exactly as SpriteLayer keeps its sprites.

## `ColorGradient`

```cpp
#include "render/color_gradient.h"

struct GradientBand {
    int rows;
    std::uint8_t level;               // 0 to kGradientFullLevel (31)
    retropp::BlendMode blend;
    retropp::Rgba8 color = {.r = 255, .g = 255, .b = 255};
};

Regions ColorGradient(std::string_view keyPrefix, retropp::Point topLeft, float width,
                      std::span<const GradientBand> bands);
```

Horizontal bands of color laid over a rectangle `width` pixels wide, top to bottom: the first band
starts at `topLeft` and each one covers `rows` pixel rows directly below the one before. A band's
fill is its `color` at `level` 31sts of full strength on each channel, rounded to the nearest value
— with the default white, level *n* is a gray of *n*/31 — and combines with what lies beneath under
the band's `blend`: `BlendMode::Add` lightens, `BlendMode::Subtract` darkens.

It returns one `ColorFill` region per run of neighboring bands with the same level, color and
blend, keyed `<keyPrefix>:<row>`, where `row` is the run's first row counted from `topLeft`. A band
at level 0 adds or takes away nothing and gets no region, but its rows still count, so the bands
below it keep their place and their keys.

```cpp
// A box lighter toward its top and darker toward its bottom, shading the box's layer only.
const std::array<GradientBand, 3> bands{{
    {.rows = 8, .level = 7, .blend = retropp::BlendMode::Add},
    {.rows = 4, .level = 0, .blend = retropp::BlendMode::Add},
    {.rows = 8, .level = 7, .blend = retropp::BlendMode::Subtract},
}};
frame.layers.push_back(TileLayer("box", z, std::move(cells), columns, rows,
                                 {.regions = ColorGradient("boxshade", {8, 16}, 240, bands)}));
```

Give the regions to a layer's `regions` to shade that layer alone, or to the frame's to shade
everything drawn beneath them. They are in screen pixels: a layer's scroll or transform does not
move them.

## The dialog components

All of them read a `text::DialogState` and take `(x, y)`, the top-left corner of the text area
on screen. They draw from two sheets built from the cartridge's own font and uploaded at load
time (see [../text/font.md](../text/font.md)):

```cpp
const text::DialogFont& font = content.dialogFont;

const text::IndexedImage glyphs = text::buildGlyphSheet(font.glyphArt, font.glyphCount());
const retropp::AtlasManifest glyphSheet = renderer.uploadAtlas(
    glyphs.pixels.data(), glyphs.width, glyphs.height,
    retropp::AssetDimensions{text::kGlyphCellPx, text::kGlyphCellPx},
    retropp::ContentKind::Tileset, retropp::ReadOrder::LeftRightThenDown,
    static_cast<int>(font.glyphCount()), retropp::TransparentIndices::of({text::kBlankPixel}));

const text::IndexedImage arrow = text::buildCursorSheet(font.cursor);
const retropp::AtlasManifest arrowSheet = renderer.uploadAtlas(
    arrow.pixels.data(), arrow.width, arrow.height,
    retropp::AssetDimensions{text::kCursorWidth, text::kCursorCellHeight},
    retropp::ContentKind::Single, retropp::ReadOrder::LeftRightThenDown, 0,
    retropp::TransparentIndices::of({text::kBlankPixel}));
```

The blank pixel is a hole in both sheets, so a sprite shows only its ink, and the ink takes
its color from the palette the sprite draws through. Two palettes do all the dialog's
coloring: one whose ink entry is the font color (white by default) and one whose ink entry is
black, for the shadows.

### `Glyph`

```cpp
Sprites Glyph(std::string_view key, std::uint16_t glyph, int x, int y,
              const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
              retropp::PaletteId shadow);
```

One letter and its drop shadow: the shadow first, the same cell one pixel right and one down
through `shadow`, then the letter at `(x, y)` through `ink`. `(x, y)` is where the top-left of
the glyph's artwork goes. The other text components are lists of these.

### `DialogText`

```cpp
Sprites DialogText(const text::DialogState& state, int x, int y, int linePitch,
                     const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                     retropp::PaletteId shadow);
```

Every glyph on the current page (`state.placed`), each a `Glyph` with its ink starting at its pen
x on its line, `text::kGlyphRowOffset` rows below the line's top. `(x, y)` is the text area's
top-left corner and line *n*'s top is `y + n · linePitch`; both come from the dialog window
(`text::textAreaTop()`, `text::linePitch()`, [../text/window.md](../text/window.md)). The pitch
changes when the window finishes opening, and each glyph keeps its key, so the lower lines move up
rather than being replaced. The artwork starts
`text::kGlyphArtLeftOfPen` to the left of the pen, because the font's artwork carries that
much blank space before its ink.

While a map title is up, the title's glyphs are `MapTitle`'s and this returns nothing. Once the
title is down, its glyphs stay on the page until the next message wipes them; whether they are
seen depends on the window the layer sits in.

### `MapTitle`

```cpp
Sprites MapTitle(const text::DialogState& state, int x, int y,
                 const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                 retropp::PaletteId shadow);
```

The map title's glyphs, keyed `title:<n>`. The title is one line, so there is no pitch: each
glyph is `text::kGlyphRowOffset` rows below `y`, the top of the title's text area
(`text::titleTextAreaTop()`), which sits lower in the title box than a message's does in its box.
Returns nothing once the title is down.

### `DialogCursor`

```cpp
Sprites DialogCursor(const text::DialogState& state, int x, int y, int linePitch,
                       const retropp::AtlasManifest& arrow, retropp::PaletteId ink,
                       retropp::PaletteId shadow);
```

`(x, y)` and `linePitch` as for `DialogText`. The arrow beside the selected choice, when the
dialog shows one (`state.cursor`): at the left
edge of the choice's 16-pixel column, at a glyph's height in the line, with its shadow one pixel
to the right only (`text::kCursorShadowDx`, `text::kCursorShadowDy`). Returns nothing without an
arrow on screen, and nothing given an empty sheet — the Japanese cartridge's arrow has no known
place, so it has no artwork to upload.

### `DialogWipe`

```cpp
Regions DialogWipe(const text::DialogState& state, int x, int y, int linePitch);
```

A page the player dismisses goes half a line per frame — line 0's left half, then its right half,
then line 1, down to line 3's right half — before the next page types. This returns one region
per half-line gone so far, each the half-line's rectangle made see-through on the layer it is
given to: `text::kHalfLinePx` wide, over the line's `text::kLineInkRows` rows — its glyphs, their
shadows and the choice arrow, and nothing of the lines above or below. `(x, y)` and `linePitch`
as for `DialogText`. A glyph straddling the middle of
a line loses exactly the part inside the gone half. Give the result to the layer the text is on,
through `LayerOptions::regions`.

## The dialog window components

The dialog box is its own tile layer at `kDialogWindowZ`, beneath the text layer. All four read a
`text::DialogWindowState` ([../text/window.md](../text/window.md)); the three that place things take
`boxTopLeft`, the screen position of the box's top-left visible corner. The box draws from the
wallpaper sheet the cartridge's art is built into (`text::buildWindowSheet`), uploaded with 8 × 8
cells and no see-through pixel, through a palette whose entries 1 to 7 are the window's colors.

```cpp
#include "render/tile_layer.h"
#include "render/window/dialog_window.h"
#include "render/window/window_clip.h"
#include "render/window/window_shading.h"
#include "render/window/window_squash.h"

const retropp::Point boxTopLeft{.x = 8.0f,
                                .y = static_cast<float>(text::boxTopRow(window) * 8)};
const Regions clip = WindowClip(window, boxTopLeft);

Regions boxRegions = WindowShading(window, boxTopLeft);
boxRegions.insert(boxRegions.end(), clip.begin(), clip.end());

Cells box = DialogWindow(window, wallpaperSheet, windowPalette);
const int rows = static_cast<int>(box.size()) / text::kWindowVisibleColumns;
frame.layers.push_back(TileLayer(
    kDialogWindowKey, kDialogWindowZ, std::move(box), text::kWindowVisibleColumns, rows,
    {.scroll = {.x = -8, .y = -static_cast<int>(boxTopLeft.y)},
     .regions = std::move(boxRegions),
     .transform = WindowSquash(window, boxTopLeft)}));
// The text layer's regions are DialogWipe(...) followed by the same clip.
```

### `DialogWindow`

```cpp
Cells DialogWindow(const text::DialogWindowState& state, const retropp::AtlasManifest& sheet,
                   retropp::PaletteId palette);
```

The box's tiles: the formation's 30 visible columns (`text::kDialogWindowFormation`, columns 1 to
30), all nine rows whenever any of the box shows, or the map-title box's four. Each cell names
`sheet`'s tile for its `WindowTile` and draws through `palette`. Returns nothing while the window is
closed, before its first opening step, and for a text-only message.

### `WindowShading`

```cpp
Regions WindowShading(const text::DialogWindowState& state, retropp::Point boxTopLeft);
```

The light and dark bands across the box, as a `ColorGradient` keyed `dlgsh`: each row of the
shading table is a one-row band at its level in gray (level *n* is *n*/31 of full), with
`BlendMode::Add` above the table's middle and `BlendMode::Subtract` from it down, so the result is
one region per run of rows sharing a level and a side of the middle. Level 0 rows get none. While
the box opens, each step shades the rows it shows with its own height's table; open and closing,
the open box's table covers the rows that show, the rows above them passed as one blank band so
each run keeps the key it had while the box was open. Give it to the box's layer only, so the text
is not shaded.

### `WindowSquash`

```cpp
retropp::Transform WindowSquash(const text::DialogWindowState& state, retropp::Point boxTopLeft);
```

On the first four opening frames, a vertical scale of (rows shown) / 9 about the box's middle row,
so the whole box is drawn squashed into the 1, 3, 5 or 7 rows shown. The identity otherwise.

### `WindowClip`

```cpp
Regions WindowClip(const text::DialogWindowState& state, retropp::Point boxTopLeft);
```

While the window opens or closes, a see-through region over everything outside the rows shown, the
full width of the screen; with no rows shown, the whole layer. Nothing once open, when closed, and
for the title box. On the box's layer it crops the closing box from the outside in; on the text's
layer it keeps the text within the rows shown.

### Gotchas

- **Regions are in screen pixels.** The shading and the clip are placed on the screen, not in the
  box's grid, so they stay where they are when the layer is squashed or scrolled.
- **The clip goes on both layers.** Leaving it off the text layer shows text outside a half-open
  box; leaving it off the box layer shows a closing box whole.
- **The shading goes on the box's layer only.** On the text layer it would grade the letters.

## Adding a component

1. Put the logic that decides what to show in its logic module, advanced once per frame. The
   component gets that state as a parameter.
2. Give the component its own `.h`/`.cpp` under `src/render/` (or a subdirectory for one area),
   and name the function for what it draws.
3. Return primitives. If it needs its own layer, gather it with `SpriteLayer` — or add an
   aggregate of the same shape — rather than building a `DrawLayer` inside the component.
4. Take its depth and key prefix from `layers.h`.
5. Test what it returns: call it with a known state and read back the sprites, regions or layer
   (`tests/test_dialog_components.cpp` builds its sheets with `retropp::sliceLayout`, so no
   renderer is needed).

## Where to change things

| To change | Edit |
|---|---|
| Where the dialog sits on screen, and how far apart its lines are | The `(x, y)` and `linePitch` the caller passes — the dialog window's ([../text/window.md](../text/window.md)). |
| How the box is drawn, shaded, squashed or cropped | `src/render/window/` |
| How bands of color are laid over a layer | `src/render/color_gradient.cpp` |
| The box's tiles, or its shading levels | `src/text/window_layout.h` and the tables it includes, generated by `tools/asm_parser/parse_window_data.py` |
| The shadow's offset, the text area's geometry, where glyphs sit in a line | `src/text/layout.h` |
| A layer's depth, the screen size, a key's name | `src/render/layers.h` |
| How a glyph and its shadow are drawn | `src/render/text/glyph.cpp` |
| The font color or the shadow color | The two palettes the caller uploads and passes in. |
