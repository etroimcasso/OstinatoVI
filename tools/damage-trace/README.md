# ostinato-damage-trace

Runs the cartridge on the engine's SNES machine and watches its damage, status and special-effect routines: what
each one reads and writes while the game is played, whether every call so recorded comes out the
same from the port, or whether the port's rules give the same answers the cartridge's own code does.
It is a developer tool and is not part of the game.

```sh
cmake --build build --target ostinato-damage-trace
./build/ostinato-damage-trace <rom> <directory> [--from <state>] [--quit-after-frames <n>]
./build/ostinato-damage-trace <rom> <directory> --headless --frames <n> [--from <state>] [--inputs <script>] [--picture-at <frame>]... [--write-at <frame> <address> <bytes>]...
./build/ostinato-damage-trace <rom> <directory> --to-battle <inputs> [--twice]
./build/ostinato-damage-trace <rom> <directory> --check-state <ticks>
./build/ostinato-damage-trace <rom> <directory> --check <draws> [--only <routine>[,<routine>]...] [--seed <n>] [--replay-traced]
./build/ostinato-damage-trace <rom> <directory> --replay [--log <file>]
```

`<rom>` is a US cartridge image (Final Fantasy III 1.0 or 1.1), with or without a copier header.
`<directory>` is created if needed. A mode goes alone, with the options the lines above show it.

| To check | Run |
|---|---|
| Every routine against the port over seeded scenarios | `--check 2000` |
| One routine, another seed | `--check 2000 --only status-effect --seed 7` |
| The replay itself, on the cartridge's own calls | `--check 200 --replay-traced` |
| A recording against the port | `--replay --log tests/fixtures/damage-trace/first-fight.log` |
| That the script reaches its fight, the same every run | `--to-battle tools/damage-trace/first-fight.inputs --twice` |
| That a restored machine carries on where it stood | `--check-state 300` |
| A fight with scripted buttons, with no window | `--headless --from <state> --inputs <script> --frames 3000`, then `--replay` |
| A fight by hand | `--from <state>`, then `--replay` |
| Where a script has got to | `--headless --inputs <script> --frames <n> --picture-at <frame>` |
| A fight set up by hand | `--headless --from <state> --frames <n> --write-at <frame> <address> <bytes>` |

## Tracing

With no mode named, the cartridge plays in a window at 3×, exactly as the console draws it, from the
keyboard or a gamepad:

| Key | Pad | Button |
|---|---|---|
| Arrows | D-pad | D-pad |
| X | A | A |
| Z | B | B |
| S | X | X |
| A | Y | Y |
| Q / W | L / R | L / R |
| Enter | Start | Start |
| Right Shift | Select | Select |

Close the window to quit, or pass `--quit-after-frames <n>` to have it close itself after `n`
frames. The machine keeps the cartridge's battery save between runs, so a game saved in the window is
there to load the next time.

Seven routines are traced. An escape on each one's entry and on each instruction its callers return
to opens and closes a call; watches over every cell the routines read or write log each access made
inside a call:

| Routine | Entry | Returns to | Works out |
|---|---|---|---|
| `CalcDmg` | `$C2:2B9D` | `$C2:322E` | an attack's base damage |
| `CalcTargetDmg` | `$C2:0B83` | `$C2:346C` from the attack, `$C2:3509` from a spell's bounce | the damage to one target |
| the apply-and-show routine | `$C2:62EF` | `$C2:3473` from the attack, `$C2:3528` from a bounce | taking the damage off and showing the numerals |
| `UpdateStatus` | `$C2:4391` | `$C2:324D` from the attack, `$C2:24A7` from the battle's start, `$C2:50AA` from a formation change | every battler's waiting statuses taking effect |
| `MagicStatusEffect` | `$C2:4406` | `$C2:345C` from the attack, `$C2:34F9` from a bounce | an attack's statuses on one target |
| `DoAttackerEffect` | `$C2:3E7D` | `$C2:33A9` | an attack's special effect on the attack and its attacker |
| `DoTargetEffect` | `$C2:387E` | `$C2:345F` from the attack, `$C2:34FC` from a bounce | an attack's special effect on one target |

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
console's frame count, the direct-page params block, attack flags and multiplier, the party's gil and
steps, the attack's record and damage, each monster's index, the party's control menus and hands, each
battler's bit, the numeral buffer, the script queue, the monsters standing and the cells beside them,
the battle's list of actions, what is to be worked out again for each battler, the links between
battlers, the statuses each battler can be given, the damage ledger, the per-command and per-battle
cells, and each battler's flags, stats, monster flags, the commands it was last hit with, gold, AI
variable, pending statuses, triggers, statuses for a counterattack, four status timers, Forcefield
elements, statuses and whether it is away from the field (`kTracedRanges` in `damage_oracle.h`). The
addresses are the US cartridges'.

## Starting at a battle

`--to-battle <inputs>` makes a state to start from, and `--from <state>` starts the trace from it, so a
fight is played without playing up to it:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/opening --to-battle tools/damage-trace/first-fight.inputs
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/opening --from ~/traces/opening/battle.state
```

With `--to-battle` nothing opens. The cartridge runs from power-on, with no battery save, holding the
buttons the script names. The run writes `battle.state` under `<directory>`: the machine as it stood on
the tick before the battle program's entry (`$C2:0000`), so a trace started from it sees the whole
battle, the battle's start among it. It prints the frame the battle began on and exits 0. A run that
goes 600 frames past the script's last line with no battle prints `no battle by frame <n>`, writes the
picture the machine last finished to `to-battle-end.ppm` and exits 1; the picture shows where the script
left the game.

A script has one line per change, `<frame> <buttons>`. The buttons are `-` for none, or names from
`A B X Y L R Start Select Up Down Left Right` joined by `+`, held from that frame until the next line's.
`#` starts a comment, blank lines are passed over, and the frames strictly increase. Two runs of one
script on one cartridge write the same state, byte for byte; `--twice` runs it a second time, compares
the two states and exits 1 if they differ.
`first-fight.inputs` plays from power-on to the first fight in Narshe on Final Fantasy III 1.1.

With `--from` the trace starts at the state, logs `state restored from <file>` first, and counts frames
from 0. The machine keeps no battery file, so a save made in that window is gone when it closes and the
save of a plain trace is untouched.

A state file names the cartridge revision it was taken on, and `--from` refuses a state from another
revision, or a ROM that is none of the three revisions, before anything opens:

```
~/traces/opening/battle.state is a state of US_1_1; rom/other.sfc is US_1_0
```

The file is the cartridge's memory, so `*.state` is ignored by git.

`--check-state <ticks>` checks the state itself, with nothing open. A machine runs `<ticks>` ticks from
power-on, and its state goes through `check.state` under `<directory>` onto a second machine. Both run
240 ticks on: one tick short their memory must differ, and at 240 their memory and picture must match.
It prints all three and exits 1 if any fails.

## Tracing with no window

`--headless --frames <n>` runs the same trace with nothing open, for `<n>` frames, from power-on or
`--from <state>`, on a machine with no battery file. `--inputs <script>` holds the script's buttons, its
frames counted from the run's first frame, and the log goes to `damage-trace.log` under `<directory>`
as in a window. Each `--picture-at <frame>` writes that frame's picture to `frame-<frame>.ppm`:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/scripted --headless --from ~/traces/opening/battle.state --inputs fight.inputs --frames 3000 --picture-at 1200
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/scripted --replay
```

The log is appended to, so a run that starts over wants a directory of its own.

Each `--write-at <frame> <address> <bytes>` writes bytes into work RAM before that frame runs, so the
game sees them from that frame on. The address is `$BB:AAAA` in `$7E:0000-$7F:FFFF` (the `$` may be
left off; a shell wants it quoted), and the bytes are an even number of hex digits, written from the
address on in the order given. Writes on one frame land in the order given, so a later one to the same
cell wins. A frame that is not a number, an address outside work RAM, digits that are not whole bytes,
and bytes running past `$7F:FFFF` are refused before anything runs.

This sets up a failed Metamorph in the first fight, from `first-fight.inputs`'s state: the metamorph byte of
the guard in battler slot 6 (`$7E:3CA0`) gets rate 7, which never succeeds, and Terra's waiting action —
list slot `$50` — is Summon (`$19`) with Ragnarok (`$46`) at that guard, put at the head of the action
queue:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/metamorph --headless --from ~/traces/opening/battle.state \
    --frames 361 --picture-at 268 \
    --write-at 160 '$7E:3CA0' E0 --write-at 160 '$7E:31D0' 50 --write-at 160 '$7E:34C0' 1946 \
    --write-at 160 '$7E:35C0' 0004 --write-at 160 '$7E:36C0' 00 --write-at 160 '$7E:32CC' 50 \
    --write-at 160 '$7E:3820' 00 --write-at 160 '$7E:3A66' 0001
```

Frame 268 shows "Better off Green Cherry!" over both guards, still standing.
`tests/fixtures/damage-trace/failed-metamorph.log` is this fight run for 2,400 frames, from
`build/first-fight/battle.state`: Terra's Ragnarok and the guards' attacks after it, every call of
the seven routines among them.

## Replaying

With `--replay` nothing opens. Every call in `<directory>/damage-trace.log`, or the file `--log` names, is rebuilt as a battle —
each cell the routine read as it read it, every other cell as a battle starts it — and worked out by
the port: the attacker from the params block (for a bounce, the battler it bounced off — for a
Launcher's or Super Ball's hits, the lowest battler in the bounces that hit — and for the damage
shown after a bounce, the attacker of the block before), the target from the battler cells the
routine touched (for a bounce, the battler its byte names). The statuses taking effect need neither,
and replay from any of their three callers; an attack's statuses on one target take the target from
the battler cells the call touched. An attack's special effect takes its attacker from the params
block — for a bounce, the battler it bounced off, or for a Launcher's or Super Ball's first hit, the
attacker of the block before — and its effect on one target takes the target the statuses just before
it were worked out for. The script's queue and params blocks stood elsewhere in the game than in the
rebuilt battle, so what the call read and wrote of them is moved to the rebuilt battle's places. A party
member's last hit and a hold in Seize that the call did not read stand as the attack frame left them.
The port's answer is compared with every cell the routine wrote.
Each call is logged to `replay.log` with its differences; any difference is printed and fails the
run:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/opening --replay
```

A call the log opens and never closes — the routine left by some other way than its callers' return
sites — is not replayed. A call that cannot be rebuilt counts as a difference, with the reason logged.
Copy a recording into `tests/fixtures/damage-trace/` (any `.log` file name) and
`tests/test_trace_replay.cpp` replays it with the rest of the damage tests. `first-fight.log` there is
the first fight in Narshe, played from `first-fight.inputs`'s state, in which Terra's Bio Blast sets
Poison and Sap on both guards; `tests/test_trace_state.cpp` checks that it holds a status being set.
`failed-metamorph.log` is the same fight with the failed Metamorph written in (above), and
`tests/test_trace_state.cpp` checks that it runs both effect routines, Metamorph's among them. The
cartridge's own calls of the two status routines, traced through the oracle, are replayed by
`tests/test_status_oracle.cpp`, and of the two effect routines by `tests/test_effects_oracle.cpp`.

## Checking

With `--check <draws>` nothing opens. The cartridge is started as far as its reset code and parked
there, in the processor mode the game's code is written for; its routines are bound where the
cartridge holds them; and `<draws>` scenarios, drawn from a fixed seed, are written into the machine's
work RAM and run through each routine and through the port's rule for it. Each scenario is logged to
`check.log`; any disagreement is printed and fails the run:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc /tmp/damage --check 2000
```

`--only` takes a comma-separated list of these names, and `--seed <n>` draws from another seed:

| Name | The routine | The test that sweeps the same draws |
|---|---|---|
| `base-damage` | `CalcDmg` | `tests/test_damage_formulas.cpp` |
| `relic-bonus` | `RelicDmgEffect` | `tests/test_damage_formulas.cpp` |
| `multiplier` | `ApplyDmgMult` | `tests/test_damage_formulas.cpp` |
| `target-damage` | `CalcTargetDmg` | `tests/test_target_damage.cpp` |
| `apply-damage` | the apply-and-show routine | `tests/test_apply_damage.cpp` |
| `reflected-attack` | `CalcReflectDmg` | `tests/test_reflected_attack.cpp` |
| `status-changes` | `UpdateStatus` | `tests/test_status_changes.cpp` |
| `status-effect` | `MagicStatusEffect` | `tests/test_status_effect.cpp` |
| `attacker-effect` | `DoAttackerEffect` | `tests/test_effects_oracle.cpp` |
| `target-effect` | `DoTargetEffect` | `tests/test_target_effects.cpp` |

`--replay-traced` also traces the traced routines' calls in each draw and replays them against the
port, as `tests/test_trace_replay.cpp`, `tests/test_status_oracle.cpp` and
`tests/test_effects_oracle.cpp` do; a target-effect draw's calls are given the draw's target.

## Files

| File | What it holds |
|---|---|
| `main.cpp` | the command line, the window and the trace on it, the trace with no window, the scripted run to a battle, the state check, the replay run and the check run |
| `machine_state.{h,cpp}` | `MachineState`, `writeMachineState()`, `readMachineState()`, `stateIsOf()` and `cartridgeName()`, a state's file; `InputStep`, `readInputScript()` and `buttonsAt()`, an input script; `CellWrite`, `readCellWrite()`, `declareWorkRamBytes()` and `writeCells()`, the bytes `--write-at` writes |
| `first-fight.inputs` | the buttons from power-on to the first fight in Narshe |
| `damage_oracle.{h,cpp}` | `DamageOracle`, the cartridge's routines isolated and called over a `DamageScenario`: the formulas, the damage to one target, the damage taken and shown, a spell bounced off Reflect, the statuses taking effect, an attack's statuses on one target and its special effect on its attacker and on one target; `drawScenario()`, `drawTargetScenario()`, `drawApplyScenario()`, `drawReflectScenario()`, `drawStatusChangesScenario()`, `drawStatusEffectScenario()`, `drawAttackerEffectScenario()` and `drawTargetEffectScenario()`, the seeded draws; `portTargetDamage()`, `portApplyDamage()`, `portReflectedAttack()`, `portStatusChanges()`, `portStatusEffect()`, `runAttackerEffect()`, `runTargetEffect()` and `differences()`, the port read back the same way and compared cell by cell; `DamageTrace`, the trace's escapes, watches and lines, on any machine; `parseTrace()` and `DamageOracle::replay()`, a recorded call read back and replayed against the port |
