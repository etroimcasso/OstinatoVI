# Engine — Rendering

Guide for putting the game on screen. Read this if you want to draw something new, change how
something already drawn looks, or understand how a frame is put together.

## What this area is

The port draws through the Polyrhythm engine, which takes the whole frame as data — a list of
layers at depths, each holding tiles or sprites — and draws the difference from the frame
before. Nothing in the port draws into a screen. Each thing on screen is a **component**: a
function of the state it shows, named for what it is, that returns the engine primitives it is
made of. A frame is the list of what the components return.

| Kind of component | Returns | Examples |
|---|---|---|
| A leaf, or a group of leaves | its `Sprites` or `Regions` | `Glyph`, `DialogText`, `DialogCursor`, `DialogWipe`, `MapTitle` |
| One that gathers components onto a layer | a `retropp::DrawLayer` | `SpriteLayer` |
| A screen | its `Layers` | — |

The logic that decides *what* to show lives elsewhere, in modules with no rendering type in them
(the dialog engine, for one — see [../text/dialog.md](../text/dialog.md)). Anything that
changes over time advances there, once per frame; a component only reads.

## Index

| File | Covers |
|---|---|
| [components.md](components.md) | The return-type aliases (`types.h`); `SpriteLayer` and the `LayerOptions` it passes through; the depth table, the screen size and the key names (`layers.h`); the dialog components `Glyph`, `DialogText`, `DialogCursor`, `DialogWipe` and `MapTitle`; how to add a component. |
