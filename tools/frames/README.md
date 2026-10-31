# ostinato-frames

Runs the cartridge on the engine's SNES machine and records what happens between the moments you
name on the command line. It can write the console's own frames 1:1, measure how long each span
lasts, or both. Use it to see exactly what the game draws and when, so the port can be compared with
it. It is a developer tool and is not part of the game.

```sh
cmake --build build --target ostinato-frames
./build/ostinato-frames <rom> <directory> [--record frames|durations|both] \
    [--battles] [--gameloaded] [--fieldmenu]
```

`<rom>` is a US cartridge image (Final Fantasy III 1.0 or 1.1). `<directory>` is created if needed.
Name at least one trigger; any combination works. `--record` chooses what is recorded, the same for
every trigger. Without it, frames are recorded.

## Playing

The machine's picture is shown in a window at 3×, exactly as the console draws it. You play it from
the keyboard:

| Key | Button |
|---|---|
| Arrows | D-pad |
| X | A |
| Z | B |
| S | X |
| A | Y |
| Q / W | L / R |
| Enter | Start |
| Right Shift | Select |

Close the window to quit. The machine keeps the cartridge's battery save between runs, so a game saved
in the window is there to load the next time.

## Triggers

Each trigger is an escape on the cartridge's own code, so it fires at the instruction the game itself
runs at that moment:

| Flag | Starts | Ends | Files |
|---|---|---|---|
| `--battles` | each battle, at the battle program's entry (`$C2:0000`) | its return to the field (`$C2:00CB`) | `battle<n>-f<offset>.ppm`, the last one ending `-end` |
| `--gameloaded` | the field being set up from a loaded save (`$C0:BD20`): the title screen's Continue, or an in-game restore. Browsing the load screen's slots does not fire it | when the window is closed | `game<n>-f<offset>.ppm` |
| `--fieldmenu` | each opening of the field's main menu (`$C0:C636`) | the menu's return to the field (`$C0:C651`) | `menu<n>-f<offset>.ppm`, the last one ending `-end` |

`<n>` counts the trigger's spans from 1. `<offset>` is the number of frames since the span began.
Shops, name entry and the load screen use the same menu program, but they don't pass through the
field's main-menu path, so `--fieldmenu` doesn't fire for them. The world map's menu doesn't fire it
either. The addresses are the US cartridges'.

## What it writes

- **Frames** (`--record frames` or `both`): while a trigger's span is open, every fifteenth frame is
  written as a binary PPM (`P6`), 256 × 224, one byte per channel. `frames.log` in `<directory>` gets
  a line per frame written. A battle's lines also carry the scroll values the battle keeps in work
  RAM: each background layer's first scroll entry, the background positions, and the scroll table
  selectors.
- **Durations** (`--record durations` or `both`): `durations.log` in `<directory>` gets a line per
  span, also printed. Each line gives the span's length in the console's frames and in milliseconds
  of its 60.0988 Hz clock, with the frames it began and ended on:

  ```
  menu 1: 74 frames, 1231.3 ms (frames 2210 to 2284)
  ```

  A loaded game's span runs until the next load or until the window closes. Any span still open when
  the window closes is measured to the last frame the machine ran, and its line says so.
- **Escape announcements:** every escape is printed and logged in `frames.log` as it fires, with its
  name, address and frame number. So is the start and end of each span, and the frame the window
  was closed on:

  ```
  escape "field menu opened" ($C0:C636) fired at frame 2210
  menu 1 opened at frame 2210
  ```

Every log is appended to, so several runs into one directory keep all their lines.

## Changing it

The triggers and the frame pitch are constants at the top of `main.cpp` (`kBattleEntry`,
`kSavedGameLoaded`, `kFieldMenuOpened`, `kFramePitch`, …). A new trigger is:

- an address constant;
- a `Mark` registered with `machine.registerEscapes`, armed by its own flag;
- a `Span` for it;
- a branch in the tick that calls `open` and `close` on that span.

Frames and durations then work for it with no further code.

The work-RAM values logged beside battle frames are declared as regions in `declarePlaces` before
the machine runs. A running machine answers reads of declared places only.
