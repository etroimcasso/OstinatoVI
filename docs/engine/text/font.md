# The dialog font

The font the field dialog and the map title are typed in: each glyph's artwork, how far the
pen moves after it, and the arrow drawn beside the selected choice. All three come from the
player's cartridge when the game starts; none of it is compiled into the program.

`src/text/font.h` is the view over what the cartridge held and the two functions that turn it
into sprite sheets. It holds no rendering type, so the sheets are plain indexed images that the
caller uploads.

## Where it comes from

`assets::ingestCartridge()` reads the three families and hands them back as one
`text::DialogFont`:

```cpp
#include "assets/cartridge.h"
#include "text/font.h"

const assets::IngestedContent content = assets::ingestCartridge();
const text::DialogFont& font = content.dialogFont;

font.glyphArt;      // every glyph's artwork
font.metrics;       // the width table — what DialogContext::metrics wants
font.cursor;        // the choice arrow
font.glyphCount();  // 224 on a US cartridge, 1,248 on the Japanese one
```

| Family | `RomAsset` | Size | |
|---|---|---|---|
| Glyph artwork | `LARGE_FONT` | 5,120 bytes (US) / 27,456 (Japanese) | 22 bytes per glyph from glyph code `$20`. The US cartridge stores a little past its last glyph. |
| Widths | `FONT_WIDTH` | 256 / 1,280 bytes | One per glyph code from `$00`. It sits between the two font files, in both languages. |
| Choice arrow | `DLG_CURSOR_GFX` | 12 bytes | US cartridges only — see [Gotchas](#gotchas). |

The spans in `DialogFont` stay valid for the life of the program; the engine's data library
keeps the bytes.

## The width table

`FontMetrics` is a view over the table: `width(glyph)` is how far the pen moves after drawing
`glyph`, in pixels, and what the English word wrap adds up. Control codes (`$00`-`$1F`) have a
width of 12 and are never drawn; the space `$7F` is 5 pixels and the wide space `$FF` is 6.

## The artwork

A glyph's artwork is `kGlyphRows` (11) rows of `kGlyphArtWidth` (16) pixels. Each row is two
bytes, low byte first, with the leftmost pixel in the top bit. Every glyph's ink starts
`kGlyphArtLeftOfPen` (4) pixels into its artwork, which is why the dialog draws a glyph's
artwork that far to the left of the pen.

The arrow is `kCursorRows` (12) rows of `kCursorWidth` (8) pixels, one byte per row, leftmost pixel
in the top bit: a right-pointing triangle one pixel tall at its tips and six wide at its widest,
starting two pixels in, with a blank last row. `CursorArt::ink(x, y)` reads it.

## The sheets

```cpp
const text::IndexedImage glyphs = text::buildGlyphSheet(font.glyphArt, font.glyphCount());
const text::IndexedImage arrow = text::buildCursorSheet(font.cursor);

// Glyph g is cell glyphSlot(g) of the glyph sheet.
const std::size_t cell = text::glyphSlot(0x20);  // 'A' -> 0
```

| Sheet | Size | Layout |
|---|---|---|
| Glyphs | 256 pixels wide, as tall as the glyphs need | `kSheetColumns` (16) glyphs per row, each in a `kGlyphCellPx` (16) square cell with its artwork in the top 11 rows. Glyph `g` is cell `glyphSlot(g)` = `g − kFirstDrawnGlyph`, read along the rows. |
| Arrow | `kCursorWidth` × `kCursorCellHeight` (8 × 16) | One cell, the artwork in its top 12 rows. |

Every pixel is `kBlankPixel` (0) or `kInkPixel` (1). Upload both sheets with the blank pixel as
a structural hole (`retropp::TransparentIndices::of({text::kBlankPixel})`), so a sprite shows
only its ink and the palette it draws through colors it. The same glyph sheet serves the letter
and its shadow — they differ only in palette and position. The upload calls are shown in
[../rendering/components.md](../rendering/components.md#the-dialog-components).

Both cell sizes are whole multiples of the 8-pixel grid the engine reads sprites on.

## Gotchas

- **The Japanese cartridge has no arrow.** The Japanese build moves the field code, and with it
  the arrow, by an amount only assembling that build would settle, and no Japanese cartridge is
  available to check one against. Its `DLG_CURSOR_GFX` region is absent, so `font.cursor` is
  empty (`loaded()` is false). Don't call `buildCursorSheet` without checking; `DialogCursor`
  draws nothing given an empty sheet. The two US cartridges place the arrow identically.
- **The views don't own the bytes.** `FontMetrics` and `CursorArt` hold spans. The ones
  `ingestCartridge` returns are good for the life of the program; a view you build over your own
  buffer is good for that buffer's life.
- **Artwork past the last glyph.** On a US cartridge `glyphArt` is longer than
  `glyphCount() × kGlyphArtBytes`. Use `glyphCount()`, never the span's size, to know how many
  glyphs there are.

## Where to change things

| To change | Edit |
|---|---|
| Where a family is read from | The region table: `tools/asm_parser/parse_rom_regions.py` (and `parse_font_metrics.py`, which locates the width table and the arrow), then regenerate. |
| How the sheets are laid out | `kSheetColumns`, `kGlyphCellPx`, `kCursorCellHeight` in `src/text/font.h`. The components read the cells through the uploaded sheet's slots, so nothing else changes. |
| How artwork is decoded | `buildGlyphSheet` / `CursorArt::ink` in `src/text/font.cpp`. |

`tests/test_font.cpp` reads all three families from the cartridge, checks the widths and the
arrow against the values the source states, and checks the glyph sheet pixel by pixel against
every glyph's artwork.
