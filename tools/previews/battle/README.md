# ostinato-preview-battle

Fights battles from the installed cartridge and shows the battle's picture: the background, the
monsters and the party, the windows, the messages and the damage numerals. Nobody drives it. A
scripted player answers every menu with Fight at the first monster still standing, and confirms each
message that waits for a key. Every hit takes a fixed 25 HP, or shows "Miss" (`preview_damage.h`).

```sh
cmake --build build --target ostinato-preview-battle
./build/ostinato-preview-battle [--capture <dir>] [--party A,B,C] [--background <n>] \
    [--message-speed <n>] <formation>...
```

Install a cartridge first with `ostinato-vi --install-rom <path>`. The preview reads the same copy.

## Options

| Option | Effect |
|---|---|
| `<formation>...` | the formations to fight, by index (0 to 575), one after another |
| `--party A,B,C` | one to four of `TERRA`, `LOCKE`, `CYAN`, `SHADOW`, `EDGAR`, `SABIN`, `CELES`, `STRAGO`, `RELM`, `SETZER`, `MOG`, `GAU`, `GOGO`, `UMARO`, `BANON`, `LEO`, at level 1. The default is Terra, Locke and Edgar |
| `--background <n>` | the battle background, by index. The default is the first field background |
| `--message-speed <n>` | 0 (fastest) to 5. The default is a new game's |
| `--capture <dir>` | open no window; write the moments worth checking to `<dir>` instead |

## In a window

The battles play one after another, with a second's pause between them. The window closes itself
after the last one.

## With `--capture`

Each formation is fought to its end with no window. The first time each of these moments happens, it
is written as `battle-<formation>-<moment>.ppm` (binary PPM, 256 × 224):

| Moment | The frame |
|---|---|
| `scene` | the battle's first frame |
| `name` | the first monster name, once its text is all up |
| `message` | the first message, once its text is all up |
| `hit` | the first numeral at the top of its bounce |
| `hp` | the first frame a party member's HP has changed |
| `death` | a monster halfway through fading out |
| `end` | the frame the scene closes |

A battle without a name or a message writes neither. A battle still running after ten minutes of
frames fails the run.

```sh
./build/ostinato-preview-battle --capture /tmp/battles 0 41 24
```

## Files

| File | What it holds |
|---|---|
| `main.cpp` | the command line, the cartridge read, the art decoded and uploaded once per background, monster and party member, and the two ways to run |
| `battle_preview.{h,cpp}` | `BattleScreen(…)`, the screen's layers for one frame |
| `scripted_player.h` | the player that answers the menus and confirms the messages |
| `preview_damage.h` | the stand-in damage |

The components it puts together are described in
[`docs/engine/rendering/battle.md`](../../../docs/engine/rendering/battle.md).
