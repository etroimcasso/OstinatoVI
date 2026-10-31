# ostinato-damage-trace

Runs the cartridge on the engine's SNES machine and watches its damage routines: what each one
reads and writes while the game is played, or whether the port's damage formulas give the same
numbers the cartridge's own code does. It is a developer tool and is not part of the game.

```sh
cmake --build build --target ostinato-damage-trace
./build/ostinato-damage-trace <rom> <directory> [--quit-after-frames <n>]
./build/ostinato-damage-trace <rom> <directory> --check <draws>
```

`<rom>` is a US cartridge image (Final Fantasy III 1.0 or 1.1), with or without a copier header.
`<directory>` is created if needed.

## Tracing

Without `--check`, the cartridge plays in a window at 3×, exactly as the console draws it, from the
keyboard:

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

Close the window to quit, or pass `--quit-after-frames <n>` to have it close itself after `n`
frames. The machine keeps the cartridge's battery save between runs, so a game saved in the window is
there to load the next time.

Three routines are traced. An escape on each one's entry and on the instruction its caller returns to
opens and closes a span; watches over every cell the routines read or write log each access made
inside a span:

| Routine | Entry | Returns to | Works out |
|---|---|---|---|
| `CalcDmg` | `$C2:2B9D` | `$C2:322E` | an attack's base damage |
| `CalcTargetDmg` | `$C2:0B83` | `$C2:346C` | the damage to one target |
| the apply-and-show routine | `$C2:62EF` | `$C2:3473` | taking the damage off and showing the numerals |

Every call is logged to `damage-trace.log` under `<directory>`, nothing filtered, one line per event:

```
f1842 enter CalcDmg $C2:2B9D
f1842 r $7E:11A2 01
f1842 r $7E:11AF 05
...
f1842 w $7E:11B0 2C
f1842 w $7E:11B1 00
f1842 return CalcDmg $C2:322E
```

`f<n>` is the frame the event happened on; `r` and `w` are a read and a write of the cell named,
with the byte read or written. The cells watched are the direct-page attack flags and multiplier,
the attack's record and damage, the links between battlers, the damage ledger, the per-command and
per-battle cells, and each battler's flags, stats, monster flags, pending statuses, Forcefield
elements and statuses. The addresses are the US cartridges'.

## Checking

With `--check <draws>` nothing opens. The cartridge is started as far as its reset code and parked
there, in the processor mode the game's code is written for; its base damage, relic bonus and
multiplier routines are bound where the cartridge holds them; and `<draws>` scenarios, drawn from a
fixed seed, are written into the machine's work RAM and run through each routine and through the
port's formula for it (`src/battle/damage.h`). Each scenario is logged to `check.log` with both
answers; any disagreement is printed and fails the run:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc /tmp/damage --check 2000
```

The same draws are what `tests/test_damage_formulas.cpp` sweeps.

## Files

| File | What it holds |
|---|---|
| `main.cpp` | the command line, the window and its escapes and watches, and the check run |
| `damage_oracle.{h,cpp}` | `DamageOracle`, the cartridge's routines isolated and called over a `DamageScenario`; `drawScenario()`, the seeded draws |
