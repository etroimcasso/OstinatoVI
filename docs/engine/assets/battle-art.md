# Battle art

The pictures a battle is drawn from, read out of the player's cartridge: each background's tiles,
tilemap and colors; each monster's picture through its stencil and its sixteen colors; each party
member's battle frames cut from its map sprite sheet and the colors it draws through; the small
font the HUD and menus are lettered in; the damage numerals' digits and "Miss"; and the colors a
dying monster fades out in. None of it is compiled in. A background's art and the numerals' are
compressed in the cartridge and are unpacked by the cartridge's own routine, run on a machine the
engine provides.

Everything here is a view over the ingested cartridge (`assets::IngestedContent`,
[rom-ingestion.md](rom-ingestion.md)) that decodes one thing when asked. The tables that say where
these pictures go — frames, poses, placements — are compiled in and described in
[../data-layer/battle-gfx-tables.md](../data-layer/battle-gfx-tables.md).

## The surface

```cpp
#include "assets/cartridge.h"

const assets::IngestedContent content = assets::ingestCartridge();

// A background: up to three blocks of tiles, a 32 x 32 tilemap, 48 colors.
const BattleBackgroundPicture field =
    content.battleBackgrounds.load(BattleBackgroundId::FIELD_WOB, *content.routines);

// A monster's picture at the size it stands (data/monster_sprites.h).
const MonsterImage lobo = content.monsterImages.image(MonsterId::LOBO, size);

// A party member's 32 frames and the sixteen colors it draws through in party slot 0.
const CharacterBattleSheet terra = content.characterSheets.sheet(CharacterGfxId::TERRA);
const auto colors = characterColors(content.characterSheets, CharacterGfxId::TERRA, 0);

// The small font's 256 glyphs and the eight palettes the battle draws them through.
const text::IndexedImage glyphs = text::buildSmallFontSheet(content.smallFont);
const text::SmallFontPalettes palettes = text::smallFontPalettes(content.smallFontColors, fontColor);

// The damage numerals' ten digits and "Miss", out of the status art.
const DamageNumeralArt numerals =
    damageNumeralArt(content.routines->decompress(assets::compressedStream(content.statusArt)));

// The sixteen colors a dying monster fades out in.
const std::span<const std::uint8_t> deathColors = content.monsterDeathColors;
```

| Type | Header | What it is |
|---|---|---|
| `CartridgeColor` | `data/cartridge_color.h` | One color as the cartridge stores it: a 15-bit word, five bits a channel; `toRgba8()` for the renderer |
| `assets::TileDepth`, `assets::drawTile` | `assets/tile_planes.h` | The cartridge's two-, three- and four-plane tile formats, drawn into an indexed image |
| `assets::CartridgeRoutines`, `assets::compressedStream` | `assets/cartridge_routines.h` | The cartridge's decompressor, placed on a machine and called |
| `BattleBackgroundArt`, `BattleBackgroundPicture`, `BackgroundCell` | `data/battle_background_art.h` | The backgrounds and one decoded |
| `MonsterImages`, `MonsterImage` | `data/monster_image.h` | The monsters' pictures and one decoded |
| `CharacterBattleSheets`, `CharacterBattleSheet`, `characterColors` | `data/character_battle_sheet.h` | The party's battle frames and colors |
| `text::buildSmallFontSheet`, `text::smallFontPalettes` | `text/small_font.h` | The small font's sheet and palettes |
| `DamageNumeralArt`, `damageNumeralArt` | `data/damage_numerals.h` | The numerals' tiles out of the decoded status art |

All of it is plain data — indexed images and color words — with no rendering type, so a caller
uploads the sheets it needs ([../rendering/battle.md](../rendering/battle.md) shows how).

## Colors and tiles

A cartridge color is a 15-bit word: red in the low five bits, then green, then blue, the top bit
unused. `CartridgeColor::toRgba8()` spreads each five-bit channel over eight bits, rounded to the
nearest value, fully opaque. `colorAt(bytes, n)` reads the *n*-th word of a run of color bytes.

Every piece of artwork the cartridge holds is 8 × 8 tiles stored as bit planes: eight bytes a
plane, one per row, the leftmost pixel in the top bit, and a pixel's color index is one bit from
each plane. The planes are interleaved in pairs. `assets::drawTile` writes one tile at the given
depth into an indexed image at a position; `assets::blankImage` makes the image to draw into.

| `TileDepth` | Bytes a tile | Colors | Used by |
|---|---|---|---|
| `TwoPlanes` | 16 | 4 | the small font |
| `ThreePlanes` | 24 | 8 | some monsters |
| `FourPlanes` | 32 | 16 | backgrounds, the party, most monsters |

## The cartridge's decompressor

Every background's tilemap and most of its graphics blocks are compressed streams. The routine
that unpacks them is the cartridge's own, and the port runs it rather than rewriting it:
`assets::CartridgeRoutines` reads the routine's bytes out of the installed image
(`RomAsset::DECOMPRESS_ROUTINE`, 147 bytes) and places them on a SNES the engine runs for the
purpose, hosting no cartridge. `decompress(stream)` writes the stream into the machine's work
RAM, calls the routine, and returns exactly the bytes it wrote.

```cpp
assets::CartridgeRoutines routines{content.region(RomAsset::DECOMPRESS_ROUTINE)};
const std::span<const std::uint8_t> stream = assets::compressedStream(bytesAtThePointer);
const std::vector<std::uint8_t> tilemap = routines.decompress(stream);   // 2,048 bytes
```

A stream opens with a word giving its own compressed length, header included, so
`compressedStream` cuts the stream out of the bytes that begin at a pointer. The routine says
nothing about how much it decoded; `decompress` measures it by running the stream twice over two
different fills and taking the bytes the two runs agree on. The first tilemap decodes in about ten
milliseconds.

`IngestedContent::routines` holds one of these for the life of the program, or nothing on a
cartridge whose decompressor has no known place (the Japanese one). The machine is move-only, so
`IngestedContent` is too.

## Backgrounds

A background (`BattleBackgroundProperties`, [../data-layer/battle-backgrounds.md](../data-layer/battle-backgrounds.md))
names up to three graphics blocks, a tilemap and a palette. `BattleBackgroundArt::load` decodes
one:

- **The blocks.** Each is 128 tiles of 32 bytes (4,096 bytes), or 256 for a double-width first
  block. A block whose pointer lies in the battle backgrounds' own graphics is a compressed
  stream; one whose pointer lies in the field's map graphics is copied as it is. The blocks are
  decoded one after another into a single buffer and each is taken from its start, so a block
  whose art is shorter than its transfer carries the previous block's bytes after its own — as
  the cartridge's loader does. `tiles` holds them end to end; `blockFirstTile`, `blockTiles` and
  `blockOwnTiles` say where each block starts, how much was taken and how much was its own.
- **Tile numbers.** Block 1 holds tiles 0-127 (0-255 when double-width), block 2 tiles 128-255,
  block 3 tiles 896-1023; the numbers between are nobody's. `sheetTile(number)` maps a cell's
  tile number to a tile in `tiles`, or nothing.
- **The tilemap.** 32 × 32 cells of two bytes (`BackgroundCell`): a tile number, one of palettes
  5-7, two flips, and a priority bit. The battlefield shows the first 19 rows.
- **The colors.** 48 words, the three palettes in order.

The tests decode all 56 backgrounds and pin the three that reach outside their own art
([../../Bugs.md](../../Bugs.md)): one block copied past the end of its family, one background
showing a previous block's leftovers, and one naming tiles no block holds.

## Monsters

`MonsterImages::image(monster, size)` decodes a monster at the size it stands — its stencil's
extent cut down to its formation slot's box (`MonsterSprites::size`). The monster's graphics
properties say where its tiles begin, whether they are stored in three planes or four, and which
palette it draws through. The tiles run through the stencil's filled cells row by row, each taking
the next tile; an empty cell shows nothing, so every pixel of it is 0. `MonsterImage::pixels` is
`size.width * 8` by `size.height * 8`, every pixel a color 0-15 (0-7 for a three-plane monster),
and `colors` its sixteen colors. `paletteIndex(monster)` is the palette's place in the family, in
eight-color steps.

The Ghost Train is a one-cell stencil whose tile is blank: its picture is its background.

## The party

A party member's frames are cut from its map sprite sheet, one of the `MAP_SPRITE_GFX` family's
items; `characterSheet(graphics)` (the compiled-in table) says where each slot's sheet lies.
`CharacterBattleSheets::sheet(graphics)` returns two images ready to upload:

- `standing`: 32 cells of 16 × 32 one under another, frame *f* in rows 32·*f* to 32·*f* + 31. A
  frame is two columns by four rows of tiles, its top row blank. Cell 0 is blank: the fallen frame
  is laid out differently.
- `fallen`: one cell of 32 × 16, the fallen frame's left half beside its right.

Two frames have their leg tiles mirrored in place as they are cut (`kFramesWithMirroredLegs`), as
the cartridge's loader mirrors them. A sheet shorter than the frames reach — Gestahl's is — reads on
into the next sheet, as the loader reads.

A party member draws through sixteen colors: twelve from its battle palette
(`paletteColors(graphics)`, or the green soldier's) and the four status colors of its party slot
(`statusColors(slot)`, compiled in). `characterColors` puts them together.

## The small font

The small font is 256 glyphs of 8 × 8 in two planes. `text::buildSmallFontSheet` lays them out
sixteen to a row, glyph *g* at row *g* / 16, column *g* % 16, every pixel 0-3.

The battle draws it through eight four-color palettes, `text::smallFontPalettes`: the cartridge's
first sixteen font colors become palettes 0-3, with the player's font color replacing the third
entry of palette 0; palette 4 is black; the last twelve colors become palettes 5-7. Which palette a
run of HUD text draws through is part of the text.

## The damage numerals

The numerals are part of the status art (`content.statusArt`), a compressed stream that decodes
through the decompressor to 8,192 bytes — 256 four-plane tiles. `damageNumeralArt` copies out the
twelve the numerals use: "Miss" is the two tiles at byte `$1800` (`miss`), and digit *d* the tile at
`$1840` + 32·*d* (`digit(d)`). Their pixels are 0 (see-through), 12 (the outline) and 13 (the fill)
only — the first two of the four status colors a party member's palette ends in — so a palette of
sixteen colors whose last four are `statusColors(0)` draws a white numeral, and `statusColors(1)` a
green one.

## A dying monster's colors

`content.monsterDeathColors` is the sixteen color words a dying monster is drawn in while it fades
out, read in place of its own palette; empty on a cartridge where they have no known place (the
Japanese one).

## Gotchas

- **The decompressor needs a cartridge whose place is known.** `IngestedContent::routines` is null
  on the Japanese cartridge; `load` cannot be called without one.
- **Decoding is on demand, not cached.** `load` and `image` decode every time. A caller that draws
  the same background every frame decodes it once and keeps the result.
- **A monster's size is the caller's.** `image` takes the size the monster stands; pass the one
  `MonsterSprites::size` gives for its formation slot, or the stencil's whole extent.
- **Row seven of a glyph is cut off on the HUD.** The HUD shows seven of a glyph's eight rows, and
  101 glyphs — the digit zero among them — have ink in the eighth ([../../Bugs.md](../../Bugs.md)).

## Where to change things

| To do this | Edit |
|---|---|
| Decode a family nothing decodes yet | A view in `src/data/` over the family's region, built in `ingestCartridge` (`src/assets/cartridge.cpp`) beside these |
| Run another routine of the cartridge's | `src/assets/cartridge_routines.cpp` — place it from its region the way the decompressor is placed |
| Change how a tile depth decodes | `src/assets/tile_planes.cpp` |
| Change which colors a party member draws through | `characterColors` in `src/data/character_battle_sheet.cpp` |

## See also

- [rom-ingestion.md](rom-ingestion.md) — how the cartridge gets in and where each family lives
- [../data-layer/battle-backgrounds.md](../data-layer/battle-backgrounds.md) — the background records these decode
- [../data-layer/monster-sprites.md](../data-layer/monster-sprites.md) — the stencils and sizes
- [../data-layer/battle-gfx-tables.md](../data-layer/battle-gfx-tables.md) — the frames, sheet places, status colors and the numerals' bounce
- [../rendering/battle.md](../rendering/battle.md) — drawing these
