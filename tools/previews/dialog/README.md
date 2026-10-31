# ostinato-preview-dialog

Plays field dialog messages from the installed cartridge, in the game's own font and window. Nobody
drives it. A scripted player holds each page that waits for A, then presses A. On the first page that
offers a choice, it moves the arrow down once first.

```sh
cmake --build build --target ostinato-preview-dialog
./build/ostinato-preview-dialog [--capture <dir>] [--title <map>] [--text-only] <index>...
```

Install a cartridge first with `ostinato-vi --install-rom <path>`. The preview reads the same copy.

## Options

| Option | Effect |
|---|---|
| `<index>...` | the script's messages to play, by index, one after another. Their boxes alternate between the top and the bottom of the screen, starting at the top |
| `--title <map>` | show that map's title first, for as long as the game shows it |
| `--text-only` | every message after it shows no box |
| `--capture <dir>` | open no window; write the moments worth checking to `<dir>` instead |

A message that names an item or an amount of gil is given the Mithril Knife and 500 GP. Every
character has their default name.

## In a window

The messages play at the field's rate, with each waiting page held for two seconds and a second's
pause between messages. The window closes itself after the last one.

## With `--capture`

Each message is played straight through with no window, and the screen is written to `<dir>` as a
binary PPM (256 × 224):

| File | The frame |
|---|---|
| `title-<map>.ppm` | the map title |
| `dialog-<index>-open<rows>.ppm` | each of the five frames the box opens on (rows 1, 3, 5, 7, 9) |
| `dialog-<index>-page<n>.ppm` | page `n`, waiting for A |
| `dialog-<index>-page<n>-moved.ppm` | the same page once the arrow has moved, on the first page with a choice |
| `dialog-<index>-wipe.ppm` | the first page wipe after its first half-line |
| `dialog-<index>-close.ppm` | the first frame the box closes |

A message still running after ten minutes of frames fails the run.

```sh
./build/ostinato-preview-dialog --capture /tmp/dialog 10 11
```

## Files

| File | What it holds |
|---|---|
| `main.cpp` | the command line, the scripted player, the cartridge's font and window art uploaded once, and the two ways to run |
| `dialog_preview.{h,cpp}` | `DialogPreviewScreen(…)`, the screen's layers for one frame |

The dialog engine and its window are described under [`docs/engine/text/`](../../../docs/engine/text/).
