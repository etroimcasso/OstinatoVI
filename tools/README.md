# Tools

Developer tools for working on the port. None of them is part of the game, and nothing here ships.

| Tool | What it does |
|---|---|
| [`damage-trace/`](damage-trace/README.md) | `ostinato-damage-trace`: runs the cartridge and logs every read and write its damage routines make as the game is played, or checks the port's damage formulas against those routines over seeded scenarios |
| [`frames/`](frames/README.md) | `ostinato-frames`: runs the cartridge on the engine's SNES machine and records the console's own frames, how long things take, or both, between the moments you name: battles, a loaded save, the field menu |
| [`previews/battle/`](previews/battle/README.md) | `ostinato-preview-battle`: fights battles from the installed cartridge and shows, or captures, the battle's picture |
| [`previews/dialog/`](previews/dialog/README.md) | `ostinato-preview-dialog`: plays field dialog messages from the installed cartridge, in a window or as captures |
| [`asm_parser/`](asm_parser/README.md) | Python scripts that read the game's tables from the disassembly and write the port's data files and test fixtures |

The three C++ tools are CMake targets of the top-level build:

```sh
cmake --build build --parallel --target ostinato-damage-trace ostinato-frames ostinato-preview-battle ostinato-preview-dialog
```

The previews read the cartridge the game installed (`ostinato-vi --install-rom <path>`).
`ostinato-frames` takes a cartridge image on its command line.
