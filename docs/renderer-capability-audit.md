# Renderer Capability Audit — FF6 Observables → Polyrhythm Surface

**Date:** 2026-08-02
**Status:** Complete
**Inputs:** full PPU-usage survey of the FF6 disassembly (`everything8215/ff6` @ `1ea47b5`, all ten
modules); Polyrhythm engine developer guide + effect-library roadmap (engine checkout @ `7c3707a`).
**Feeds:** engine work-item sequencing (see `docs/DESIGN.md`) and the game-side rendering design.

This document maps every renderer-visible behavior in Final Fantasy VI onto the Polyrhythm engine's
drawing vocabulary and issues one verdict per behavior region:

- **Covered** — the engine expresses the observable directly.
- **Covered with pattern** — the engine expresses it through a documented composition of existing
  surfaces (the pattern is named).
- **Engine work item** — a genuine capability gap; named in engine terms at the end.

## Ground rule: observables, not mechanisms

The port reproduces what the player *sees*, using the engine's native vocabulary. SNES delivery
mechanisms — HDMA channel programs, mid-frame IRQs with same-frame re-arm, scanline latches, the
hardware multiplier/divider, the PPU multiply idiom, the WRAM data port — do not carry into the
port's code in any form. Where a mechanism produced a visible effect (a per-scanline scroll table
produces wavy water), the *effect* is mapped; where it produced none (the menu using the PPU
multiplier as a general-purpose calculator behind an hblank guard — `src/menu/menu_common.asm:1174`
onward), the port uses plain arithmetic and the renderer is not involved.

All source citations below are `file:line` into the disassembly's `src/` tree and were re-verified
against the pinned checkout for this audit.

## Engine vocabulary (reference)

The surfaces the mapping below draws on, all shipped in the engine at the pinned revision:

| Surface | What it gives |
|---|---|
| `FrameDrawState` / `DrawLayer` | Arbitrary N layers, z-ordered, per-layer scroll/alpha/blend; tile or sprite content; whole frame recomputed and submitted per frame |
| `ViewportResolution::Snes` | 256×224 internal viewport preset |
| `DrawLayer::transform` (`Transform`) | Full 3×3 projective per-layer transform — scale, rotation, skew, **perspective** — plus `transformEdge`; `TileWrap::Blank/Repeat/Clamp` governs sampling outside the map |
| `BlendMode` on layer/region/sprite/frame | `Normal, Add, Subtract, Multiply, Screen, Half` — `Half` is `(dst+src)/2` |
| `Region` + `ShapePoints` | Shape-confined effects: circle, capsule, polygon (concave OK), rounded, curves; stroke bands; `inverted()`; per-region `alpha` + `blend` |
| `stencil()` / `Transparency` | Make a shape of a layer (or everything below it) see-through; `TransparentInside/Outside`, feather |
| Built-in effects | `RowDisplacement, Ripple, Swirl, ColorFill, Gleam, ColorSaturation, Bloom, Glow, Transparency` at frame / layer (`Layer`/`Below` scope) / region / sprite sites |
| `ScreenSpaceEffect::paramTable` | Per-row `Vec4` table an effect's shader reads by row (`paramRowAtUv`) — the arbitrary per-scanline-data door; consumed by `Custom` shaders |
| `registerPostProcessStage` (`Custom`) | Game-authored fragment as a first-class effect at every site |
| Indexed atlas + palette store | Per-cell/per-sprite atlas + palette handles, no per-layer set or cap; palettes re-uploadable / re-pointable per frame |
| `Sprite` | Arbitrary `AssetDimensions`, per-sprite z/alpha/blend/transform/effects |

## Region-by-region mapping

### 1. Background modes and layer structure — **Covered**

FF6 uses BG modes 1 (usually `$09`, BG3-priority variant), 2, and 7 — nothing else. No mode
0/3/4/5/6, no EXTBG, no interlace or hi-res (`hSETINI` written six times, always 0 — e.g.
`src/world/init.asm:1376`, `src/field/reset.asm:871`). Field runs mode 1
(`src/field/reset.asm:830`); menus mode 1 with mode-7 credits (`src/menu/menu_init_2.asm:422,578,614`);
world mode 2 (`src/world/world_start.asm:119`) and mode 7 (`src/world/init.asm:1329`); battle drives
the mode per scanline from a three-entry table `$09/$09/$02` (`src/btlgfx/btlgfx_main.asm:2352-2357`).

A "mode" is not an engine concept. Each SNES BG in use maps to a `DrawLayer` with its own scroll and
z; the mode-1 BG3-priority bit and all per-mode priority rules collapse into explicit `DrawLayer::z`
ordering, chosen by the game per scene. Offset-per-tile (mode 2's headline feature) is used by the
world map for per-column effects; its observable — per-region tile displacement — falls under §4.
Tile-size toggles (battle's 16×16 tiles, `src/btlgfx/btlgfx_main.asm:26296`) are a data concern:
16×16 art occupies four 8×8 cells or a sprite-sized asset.

### 2. Mode 7 — world map, credits, opening, battle spell animations — **Covered**

The engine's per-layer `Transform` is a true projective matrix with per-pixel perspective — a strict
superset of the mode-7 affine matrix — so every mode-7 observable maps onto `DrawLayer::transform`:

- **World map** (`src/world/rotate.asm:377,813` computes M7A–D per scanline; fed via four HDMA
  channels, `src/world/world_start.asm:662-682`; M7X/Y per frame, `src/world/interrupt.asm:125-131`):
  the per-scanline coefficient tables are generated from a perspective camera law (the surrounding
  math in `rotate.asm:422-523`). The port derives the camera parameters (angle, height, focal
  geometry) from that math and expresses the floor as one `Transform::rotation(...).then(perspective(...))`
  per frame — the per-scanline table was the SNES's *approximation* of exactly this projection.
- **Credits airship** (`src/menu/menu_init_2.asm:282-341`): per-scanline M7A–D *and* per-scanline
  M7X/M7Y (channel 2 targets `$211f/$2120`). A projective transform expresses a moving-center
  perspective directly; if any credits table proves non-projective when the camera law is derived at
  contract time, the fallback is a `Custom` stage + `paramTable` (per-scanline warp — see §4
  pattern). `M7SEL = $80` (transparent outside the map, `src/menu/ending.asm:1743-1744`) is
  `TileWrap::Blank`; the mode-7 h/v-flip command (`src/btlgfx/btlgfx_main.asm:31071-31081`) is a
  negative-scale transform.
- **Battle spell animations** (H-Bomb, Purifier, True Edge, Atom Edge, S.Cross, Overcast —
  `InitMode7` at `src/btlgfx/btlgfx_main.asm:47007-47037`): the animation command mutates A, D, X, Y
  only — scale about a moving center, no rotation (`:31085-31143`) — a scale+translate `Transform`.

Note: there is **no mode-7 battle swirl** in FF6 — the field→battle transition is a mosaic ramp
(`src/field/battle.asm:26-60`; see §8).

### 3. Mid-frame mode splits — **Covered with pattern**

Three sites compose a mode-1-class strip above a mode-7-class strip in one frame:

- Opening: 84 scanlines of mode `$09` above mode `$07` (`OpeningBGModeHDMATbl`,
  `src/cutscene/opening.asm:643-646`).
- Credits big airship: 71 scanlines of mode 1 above mode 7 (`BigAirshipMode7HDMATbl`,
  `src/menu/ending.asm:2301-2305`).
- World vehicles: mode-2 sky strip above the mode-7 ground, split at the IRQ line
  (`src/world/interrupt.asm:233-268`), plus the liftoff sequences (`src/world/liftoff.asm:114,244,451`).

**Pattern:** two layer stacks, one per strip. The perspective ("mode 7") layer carries its transform;
the flat strip is an ordinary tile layer; each is confined to its scanband with a rectangular
`stencil()` region (or by layer size + placement where the split line is fixed). The strips meet at a
horizontal boundary — a rectangle region edge lands exactly on the viewport pixel grid under the
default `EvaluationGrid::Viewport`, so the seam is crisp by construction. The split line animates by
resubmitting the rectangle each frame (regions are per-frame data).

### 4. Per-strip / per-scanline scroll — **Covered with pattern**

The heaviest HDMA consumer. Field drives eight channels over 28 strips (27×8 + 1×7 = 223 lines;
`src/field/hdma.asm:22-149`): BG1/BG2/BG3 scroll per strip (wavy-screen tables generated at
`src/field/hdma.asm:1120-1136`), plus the §5–§7, §8, §10 registers. World scrolls BG2 per strip for
parallax (`src/world/rotate.asm:16`). Battle re-points three scroll channels per animation
(`src/btlgfx/btlgfx_main.asm:663-694, 43343-43386`). Menus scroll list panes with HOFS tables
(skills/status/shop/save/name-change/bushido/party/item).

**Patterns, by observable:**

- **Wavy screen / water / heat shimmer** (sinusoidal tables): `RowDisplacement` — the built-in is
  exactly this effect, per layer or region-confined, with `Blank`/`Stretch` edge choice. Waveform
  fidelity to FF6's specific tables is a contract-time question; if the built-in's curve family
  can't reproduce a specific table, the general door below applies.
- **Arbitrary per-scanline scroll** (non-sinusoidal tables the game computes): `Custom` stage +
  `paramTable` — one `Vec4` per row, `paramRowAtUv` in the shader, the direct expression of "this
  frame's per-line offsets". Shipped surface; no engine change.
- **Static per-strip parallax** (world BG2, field parallax bands): one layer per band with its own
  `scroll` — the plain engine idiom; no effect involved.
- **Menu list scrolling**: a layer per scrolling pane (each pane its own `DrawLayer` with its own
  scroll), not a per-scanline effect at all.
- **Per-strip tilemap base swaps** (battle channel 6 re-points BG3SC per strip;
  BG1SC/BG2SC via channel 3): the observable is "different content per band" — one layer per band,
  each with its own `TileContent`, stenciled to its strip (§3 pattern).

### 5. Color math — **Covered with pattern**, one operator gap → engine work item E2

SNES color math (CGSWSEL/CGADSUB) appears in four shapes:

- **Field:** per-map data supplies the CGADSUB byte (`src/field/scroll.asm:64-89`,
  `src/field/color.asm:49-133`) — add/subtract/half over selected layers, data-driven per map, per
  strip via channel 1.
- **World:** full add `$23` + fixed color (`src/world/init.asm:1371-1375`), full subtract `$83`
  (Serpent Trench, `src/world/fade.asm:90-93`), mid-frame half-add `$63` and full-subtract `$a3`
  splits (`src/world/interrupt.asm:272-273, 292-295`).
- **Battle:** a fully data-driven per-strip machine — channel 4 streams
  TSW+CGWSEL+CGADSUB+COLDATA per strip (writer `SetColorMathHDMA`,
  `src/btlgfx/btlgfx_main.asm:28695-28713`; bit-layout documented in-source at `:28684-28693`).
- **Menu:** half-subtract `$d1` (`src/menu/menu_init_2.asm:592-602, 634-638`); the credits gradient
  machine (§6).

**Mapping.** The engine's container blends are the SNES operator set *nearly* one-for-one: add →
`BlendMode::Add`, subtract → `Subtract`, half-add → `Half`, and the "main screen vs subscreen"
distinction collapses into *which container carries the blend* — a blended `DrawLayer` (subscreen
add/sub of layer content) or a `ColorFill` region (fixed-color add/sub), per strip via rectangle
regions carrying their own per-region `blend` (a run of ColorFill rectangles collapses to one pass in
the engine). Layer selection bits (`bo4321`) become *which layers sit below the blend container's z*.
CGRAM manipulation — the field's 20 palette-fade/manipulation modes (`src/field/color.asm:182-880`)
— is palette data, not compositing: re-upload or re-point palettes per frame (`uploadPalette` /
per-cell `PaletteId` rewrite), an existing engine door.

**The gap:** SNES half color math also pairs with subtraction — `(dst − src)/2` — and FF6 uses it:
`$d1` = half-subtract on OBJ+BG1 in menu initialization. The engine's `Half` is the add form only.
One missing operator → **engine work item E2**.

> **Correction recorded during verification.** The world mid-frame value `$a3`
> (`src/world/interrupt.asm:294`) had been previously noted as "half-subtract"; decoding it against
> the CGADSUB bit layout (bit 7 = add/subtract, bit 6 = half — the disassembly's own reference at
> `src/btlgfx/btlgfx_main.asm:28684-28686` confirms) gives **full subtract** of backdrop+BG2+BG1.
> The inline comment at `src/world/init.asm:1120` ("half add") mislabels the same value; the
> bit-layout doc, not inline comments, is authoritative. The engine-side conclusion is unchanged —
> half-subtract is still consumed (menu `$d1`) — but the consumer list is corrected here.

### 6. Fixed-color gradients (COLDATA ramps) — **Covered with pattern**

Per-strip fixed-color writes produce vertical gradients: field channel 2 per-strip COLDATA
(`src/field/hdma.asm:68-77`), the world horizon gradient (`src/world/world_start.asm:690-697` wiring;
generator at init), the credits ramp (`CreditsFixedColorHDMATbl`, `src/menu/ending.asm:2350-2365` —
intensity `$e0→$ed→…→$e0` over 14 bands), and independent R/G/B channel writes
(`src/world/cutscene.asm:900-904`).

**Pattern:** a stack of rectangle `ColorFill` regions, one per band, each carrying the band's color
and the operator (§5 blends). The engine renders a contiguous run of ColorFill regions as **one pass
regardless of count**, so a 14-band or 28-band gradient costs one pass — structurally the same thing
the HDMA table was. A smooth (per-scanline) gradient, if ever preferred over the banded original,
is a `Custom` stage + `paramTable` ramp — but the *faithful* observable is the banded version.

### 7. Windows — shaped clipping and masks — **Covered with pattern**

- **Static rectangles** (menu panes, config screens: `src/menu/menu_init.asm:187-210`,
  `src/menu/config.asm:359-394`; field/world defaults `src/field/reset.asm:847-863`,
  `src/world/init.asm:1350-1361`): rectangle `stencil()` regions per layer.
- **Per-scanline shapes** — the field flashlight circle (Bresenham, double-buffered,
  `src/field/screen.asm:82-126`), spotlights (`:755`), the pyramid (`:308`), the world bomb circle
  (`src/world/ppu.asm:98-181`): these tables *rasterize a shape into scanline spans*; the engine
  takes the shape itself — `ShapePoints::circle` / polygon — as a region, with `Transparency` or
  darkness outside (`inverted()` + `ColorFill`), moved per frame by resubmitting the shape.
- **Battle spell masks** (channel 5 window shapes + channel 7 window logic,
  `src/btlgfx/btlgfx_main.asm:43065-43072, 43380-43386`; ~40 per-animation sites; animation command
  `$80/$57`, `:30568-30577`): per-animation shaped masks map to regions (circle/capsule/polygon up
  to 64 vertices, or baked curve masks for smooth boundaries); window *logic* (AND/OR/XOR of two
  windows) maps to region composition — multiple regions, `inverted()` shapes, and stroke bands.
  Any mask whose per-scanline span table resists a clean shape description has the `paramTable`
  door (§4).

### 8. Mosaic — **Engine work item E1**

The engine has no mosaic/pixelation surface (it is on the engine's effect-library candidate list, as
"Pixelate / mosaic"). FF6's consumers:

- The **field→battle transition** — the game's signature mosaic ramp: size 0→7 twice, then 0→15
  once, all BGs, 32 frames (`BattleMosaic`, `src/field/battle.asm:26-60`).
- Field event command `$62` "mosaic screen" with scripted speed (`src/field/event.asm:2759-2769`)
  and the field per-strip mosaic channel (`src/field/hdma.asm:35-44`, generator `:675-734`, runtime
  `src/field/screen.asm:1060-1096`).
- World: poison-swamp effect (`src/world/move.asm:165-176`), train transition (`:251-253`).
- Menu: `CreateMosaicTask` at ~12 transition sites (party/config/equip/item/shop/colosseum; applied
  in NMI, `src/menu/menu_common.asm:3426-3427`).
- Battle animation command `$be` — per-strip mosaic (`src/btlgfx/btlgfx_main.asm:35117-35128`).

Verdict: a real capability gap with a dozen-plus consumers across five modules, including the most
recognizable transition in the game. Named as **E1** below.

### 9. Sprites — **Covered**

OBJ sizes are `$03` (8×8/16×16) nearly everywhere, `$61` (16×16/32×32) in battle
(`src/btlgfx/btlgfx_main.asm:43412-43413`), isolated `$01/$63/$00` — all subsumed by arbitrary
per-sprite `AssetDimensions`. OAM priority is logical (battle command `$80/$21`,
`:31917`) → `Sprite::z` / layer z. The survey found **no reliance on the 32-sprite/34-tile scanline
overflow** (no flicker-as-feature): the world horizon rows draw 15 sprites per row deliberately
under the limit (`src/world/sprite.asm:29`). No engine concern.

### 10. Per-strip screen designation (TM/TS) — **Covered with pattern**

Field channel 6 toggles main/sub screen layer enables per strip (`src/field/hdma.asm:90-99`); battle
channel 7 carries WBGLOG+TM/TS (`src/btlgfx/btlgfx_main.asm:43380-43386`). The observable — "this
layer is visible only in these scanbands" — is the §3 stencil pattern: rectangle `Transparency`
regions on the affected layer (or band-split layers). Main-vs-sub membership per se collapses into
the §5 blend-container mapping.

### 11. Brightness, forced blank, fades — **Covered**

INIDISP brightness ramps and forced-blank fades (`src/field/screen.asm:1197-1221`,
`src/world/interrupt.asm:186-190`, battle pause half-brightness
`src/btlgfx/btlgfx_main.asm:1707-1714`) map to a whole-frame black `ColorFill` region with animated
`alpha` (fade), `Multiply` at 0.5 strength (half-brightness), or full black (forced blank). Screen
shake is scroll manipulation (`src/field/screen.asm:1100`) → `LayerScroll` jitter. Palette-level
fades are §5's CGRAM-as-data mapping.

### 12. Non-renderer mechanisms — no engine surface required

For completeness, the surveyed hardware uses that map to plain game code, not renderer vocabulary:
CPU multiply/divide (`$4202-$4217` — battle damage `src/battle/battle_main.asm:11804-11830`, field
movement, world matrix math, menu, cutscene); the menu's PPU-multiply calculator
(`src/menu/menu_common.asm:1174-1291` and 65 `hMPYL` reads); the WRAM data-port write pointer
(~400 sites); H/V-IRQ arming, scanline latching, and the 16-bit store that deliberately straddles
`$420a/$420b` (`src/world/tilemap.asm:298,566`) — all mechanisms whose observables are already
accounted for above or that have none. Interlace/hi-res: unused (§1).

## Engine work items

Named in engine terms; sequencing is an engine-side decision (`docs/DESIGN.md` records the
project's standing engine asks).

### E1 — Mosaic (pixelation) built-in effect

- **Capability:** quantize the covered pixels to an N×N block grid (each block shows one source
  value, block-anchored to the site's origin), `blockSize` animatable per frame from 1 (identity)
  to at least 16.
- **Consumer need:** FF6 transitions and effects at a dozen-plus sites in five modules (§8),
  including per-strip application (battle `$be`) and staged ramps (field→battle: 0→7, 0→7, 0→15
  over 32 frames).
- **Acceptance shape:** available at layer scope and region-confined (rect strips); crisp on the
  viewport grid (blocks land on whole viewport pixels); `blockSize = 1` bit-identical to no effect.
  Already on the engine roadmap's candidate list ("Pixelate / mosaic", Low complexity); this names
  its first real consumer.

### E2 — Half-subtract blend operator

- **Capability:** a `BlendMode` completing the SNES color-math operator set:
  `B(dst, src) = (dst − src)/2` — the subtractive sibling of the existing `Half` (`(dst+src)/2`).
- **Consumer need:** menu initialization composites with half-subtract (`$d1`:
  `src/menu/menu_init_2.asm:592-602, 634-638`); field per-map color math is data-driven over the
  full CGADSUB operator space, so any map may select it.
- **Acceptance shape:** one enumerator + operator in the container blend surface (CPU mirror +
  compositor shaders), same clamp semantics as `Subtract`; existing modes byte-identical.

### Standing items (already recorded, not re-audited here)

- **SNES audio backend** — SPC700 + S-DSP hosting for the original sound driver, plus the
  audio-pack replacement backend.
- **65C816 VM backend** — for CPU-fidelity routines (RNG at minimum).

## Verdict summary

| # | Behavior region | Verdict |
|---|---|---|
| 1 | BG modes / layer structure | Covered |
| 2 | Mode 7 (world, credits, opening, battle anims) | Covered |
| 3 | Mid-frame mode splits | Covered with pattern (band-split layers + stencil) |
| 4 | Per-strip / per-scanline scroll | Covered with pattern (`RowDisplacement`; `paramTable`; per-band layers) |
| 5 | Color math | Covered with pattern (container blends) — half-subtract → **E2** |
| 6 | Fixed-color gradients | Covered with pattern (banded `ColorFill` region stack) |
| 7 | Windows / shaped masks | Covered with pattern (regions, stencil, shape composition) |
| 8 | Mosaic | **Engine work item E1** |
| 9 | Sprites | Covered |
| 10 | Per-strip screen designation | Covered with pattern (§3/§5 patterns) |
| 11 | Brightness / fades / shake | Covered |
| 12 | Non-renderer mechanisms | No renderer surface needed |

Two genuine engine gaps (E1 mosaic, E2 half-subtract), both small and well-bounded; everything else
FF6's renderer does is expressible today on the engine's shipped vocabulary.
