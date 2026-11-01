# ostinato-damage-trace

Runs the cartridge on the engine's SNES machine and watches its damage routines: what each one
reads and writes while the game is played, whether every call so recorded comes out the same from
the port, or whether the port's damage formulas give the same numbers the cartridge's own code does.
It is a developer tool and is not part of the game.

```sh
cmake --build build --target ostinato-damage-trace
./build/ostinato-damage-trace <rom> <directory> [--quit-after-frames <n>]
./build/ostinato-damage-trace <rom> <directory> --replay
./build/ostinato-damage-trace <rom> <directory> --check <draws>
```

`<rom>` is a US cartridge image (Final Fantasy III 1.0 or 1.1), with or without a copier header.
`<directory>` is created if needed.

## Tracing

Without `--check` or `--replay`, the cartridge plays in a window at 3×, exactly as the console draws it, from the
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

Three routines are traced. An escape on each one's entry and on each instruction its callers return
to opens and closes a call; watches over every cell the routines read or write log each access made
inside a call:

| Routine | Entry | Returns to | Works out |
|---|---|---|---|
| `CalcDmg` | `$C2:2B9D` | `$C2:322E` | an attack's base damage |
| `CalcTargetDmg` | `$C2:0B83` | `$C2:346C` from the attack, `$C2:3509` from a spell's bounce | the damage to one target |
| the apply-and-show routine | `$C2:62EF` | `$C2:3473` from the attack, `$C2:3528` from a bounce | taking the damage off and showing the numerals |

Every call is logged to `damage-trace.log` under `<directory>`, nothing filtered, one line per event:

```
f1842 enter CalcTargetDmg $C2:0B83
f1842 params 00 04 01 00 01 00 00 00 00 00 00 00 00 00 00 00 hits FF rules 02 previous 00
f1842 r $7E:11A6 0A
f1842 r $7E:3BC0 32
...
f1842 w $7E:33D8 2C
f1842 w $7E:33D9 00
f1842 return CalcTargetDmg $C2:346C
```

`f<n>` is the frame the event happened on. Each `enter` is followed by the attack's context as the
routine was entered: the params block (`$7E:00A0-00AF`, its second byte the attacker's slot), the
Launcher's or Super Ball's hits to come (`$7E:3405`), the aiming rules (`$7E:00BA`) and the attacker of
the params block before this one in the script's buffer (`--` when there is none). `r` and `w` are a
read and a write of the cell named, with the byte read or written. The cells watched are the
direct-page attack flags and multiplier, the attack's record and damage, each battler's bit, the
numeral buffer, the script queue, what is to be worked out again for each battler, the links between
battlers, the damage ledger, the per-command and per-battle cells, and each battler's flags, stats,
monster flags, pending statuses, Forcefield elements and statuses (`kTracedRanges` in
`damage_oracle.h`). The addresses are the US cartridges'.

## Replaying

With `--replay` nothing opens. Every call in `<directory>/damage-trace.log` is rebuilt as a battle —
each cell the routine read as it read it, every other cell as a battle starts it — and worked out by
the port: the attacker from the params block (for a bounce, the battler it bounced off — for a
Launcher's or Super Ball's hits, the lowest battler in the bounces that hit — and for the damage
shown after a bounce, the attacker of the block before), the target from the battler cells the
routine touched (for a bounce, the battler its byte names). The port's answer is compared with every cell the routine wrote. Each
call is logged to `replay.log` with its differences; any difference is printed and fails the run:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/opening --replay
```

A call the log opens and never closes — the routine left by some other way than its callers' return
sites — is not replayed. Copy a recording into `tests/fixtures/damage-trace/` (any `.log` file name) and
`tests/test_trace_replay.cpp` replays it with the rest of the damage tests.

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

The same draws are what `tests/test_damage_formulas.cpp` sweeps; `tests/test_target_damage.cpp`,
`tests/test_apply_damage.cpp` and `tests/test_reflected_attack.cpp` sweep the damage to one target,
the damage taken and shown, and a spell bounced off Reflect the same way, each from its own draws.

## Files

| File | What it holds |
|---|---|
| `main.cpp` | the command line, the window and the trace on it, the replay run and the check run |
| `damage_oracle.{h,cpp}` | `DamageOracle`, the cartridge's routines isolated and called over a `DamageScenario`: the formulas, the damage to one target, the damage taken and shown, and a spell bounced off Reflect; `drawScenario()`, `drawTargetScenario()`, `drawApplyScenario()` and `drawReflectScenario()`, the seeded draws; `portTargetDamage()`, `portApplyDamage()`, `portReflectedAttack()` and `differences()`, the port read back the same way and compared cell by cell; `DamageTrace`, the trace's escapes, watches and lines, on any machine; `parseTrace()` and `DamageOracle::replay()`, a recorded call read back and replayed against the port |
