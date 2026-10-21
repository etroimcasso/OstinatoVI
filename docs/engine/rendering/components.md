# Components

Every component is a function in its own file under `src/render/`, named for the thing it draws.
It takes the state it shows as ordinary parameters and returns engine primitives. Called with the
same arguments it returns the same result, and it remembers nothing between frames.

```cpp
#include "render/layers.h"
#include "render/sprite_layer.h"
#include "render/text/dialogue_cursor.h"
#include "render/text/dialogue_text.h"
#include "render/text/dialogue_wipe.h"

using namespace ostinato::render;

frame.layers.push_back(SpriteLayer(
    "dialogue", kDialogueTextZ,
    {DialogueText(dialogue, textX, textY, glyphSheet, inkPalette, shadowPalette),
     DialogueCursor(dialogue, textX, textY, arrowSheet, inkPalette, shadowPalette)},
    {.regions = DialogueWipe(dialogue, textX, textY)}));
```

That one statement is the whole dialogue picture for a frame: the page's glyphs and their
shadows, the choice arrow, and the part of a dismissed page that has already gone.

## The shared vocabulary

### `types.h`

```cpp
using Sprites = std::vector<retropp::Sprite>;
using Regions = std::vector<retropp::Region>;
using Layers  = std::vector<retropp::DrawLayer>;
```

What components return. A leaf returns `Sprites` (or `Regions`, for a component that confines an
effect to part of a layer); a parent returns what its children returned.

### `layers.h`

| Name | What it is |
|---|---|
| `kScreenSize` | The screen, 256 × 224 (`retropp::ViewportResolution::Snes`). Every sprite layer covers it. |
| `kDialogueTextZ` | The depth of the dialogue text, its arrow and the map title: 30. The field's world and sprite layers take the depths below it. |
| `kShadowSpriteZ`, `kInkSpriteZ` | Within a sprite layer, every drop shadow (0) sits beneath every piece of ink (1), so no shadow ever darkens a letter. |
| `kDialogueGlyphKey`, `kTitleGlyphKey`, `kDialogueCursorKey`, `kDialogueWipeKey`, `kShadowKeySuffix` | The names sprites and regions go by. See [Keys](#keys). |

A new layer takes its depth from this table, and a new kind of sprite takes its key prefix from
it, so two components never collide.

### Keys

The engine recognises an object from one frame to the next by its key: an object that keeps
its key eases to wherever it is now, and a new key appears where it is put. A key therefore
names the thing, never its position in a list.

| Object | Key | So that |
|---|---|---|
| A dialogue glyph | `dlg:<page>:<line>:<n>` — the *n*-th glyph of a line | A glyph on the next page is a new object, not this one moved. The page count runs on for the life of the dialogue state. |
| A map title glyph | `title:<n>` | — |
| The choice arrow | `dlgc:<page>:<choice>` | Moving the selection puts the arrow straight at the new choice instead of sliding it. |
| A wiped half-line | `dlgw:<line>:<half>` | — |
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
| `regions` | none | Effects confined to shapes — `DialogueWipe`'s see-through rectangles go here. |
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

## The dialogue components

All of them read a `text::DialogueState` and take `(x, y)`, the top-left corner of the text area
on screen. They draw from two sheets built from the cartridge's own font and uploaded at load
time (see [../text/font.md](../text/font.md)):

```cpp
const text::DialogueFont& font = content.dialogueFont;

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
its colour from the palette the sprite draws through. Two palettes do all the dialogue's
colouring: one whose ink entry is the font colour (white by default) and one whose ink entry is
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

### `DialogueText`

```cpp
Sprites DialogueText(const text::DialogueState& state, int x, int y,
                     const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                     retropp::PaletteId shadow);
```

Every glyph on the current page (`state.placed`), each a `Glyph` with its ink starting at its pen
x on its line, `text::kGlyphRowOffset` rows below the line's top. The artwork starts
`text::kGlyphArtLeftOfPen` to the left of the pen, because the font's artwork carries that
much blank space before its ink.

While a map title is up, the title's glyphs are `MapTitle`'s and this returns nothing. Once the
title is down, its glyphs stay on the page until the next message wipes them; whether they are
seen depends on the window the layer sits in.

### `MapTitle`

```cpp
Sprites MapTitle(const text::DialogueState& state, int x, int y,
                 const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                 retropp::PaletteId shadow);
```

The map title's glyphs, placed the same way, keyed `title:<n>`. Returns nothing once the title is
down.

### `DialogueCursor`

```cpp
Sprites DialogueCursor(const text::DialogueState& state, int x, int y,
                       const retropp::AtlasManifest& arrow, retropp::PaletteId ink,
                       retropp::PaletteId shadow);
```

The arrow beside the selected choice, when the dialogue shows one (`state.cursor`): at the left
edge of the choice's 16-pixel column, at a glyph's height in the line, with its shadow one pixel
to the right only (`text::kCursorShadowDx`, `text::kCursorShadowDy`). Returns nothing without an
arrow on screen, and nothing given an empty sheet — the Japanese cartridge's arrow has no known
place, so it has no artwork to upload.

### `DialogueWipe`

```cpp
Regions DialogueWipe(const text::DialogueState& state, int x, int y);
```

A page the player dismisses goes half a line per frame — line 0's left half, then its right half,
then line 1, down to line 3's right half — before the next page types. This returns one region
per half-line gone so far, each the half-line's rectangle (`text::kHalfLinePx` by
`text::kLineHeight`) made see-through on the layer it is given to. A glyph straddling the middle of
a line loses exactly the part inside the gone half. Give the result to the layer the text is on,
through `LayerOptions::regions`.

## Adding a component

1. Put the logic that decides what to show in its logic module, advanced once per frame. The
   component gets that state as a parameter.
2. Give the component its own `.h`/`.cpp` under `src/render/` (or a subdirectory for one area),
   and name the function for what it draws.
3. Return primitives. If it needs its own layer, gather it with `SpriteLayer` — or add an
   aggregate of the same shape — rather than building a `DrawLayer` inside the component.
4. Take its depth and key prefix from `layers.h`.
5. Test what it returns: call it with a known state and read back the sprites, regions or layer
   (`tests/test_dialogue_components.cpp` builds its sheets with `retropp::sliceLayout`, so no
   renderer is needed).

## Where to change things

| To change | Edit |
|---|---|
| Where the dialogue sits on screen | The `(x, y)` the caller passes. |
| The shadow's offset, the text area's geometry, where glyphs sit in a line | `src/text/layout.h` |
| A layer's depth, the screen size, a key's name | `src/render/layers.h` |
| How a glyph and its shadow are drawn | `src/render/text/glyph.cpp` |
| The font colour or the shadow colour | The two palettes the caller uploads and passes in. |
