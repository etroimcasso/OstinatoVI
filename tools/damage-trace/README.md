# ostinato-damage-trace

Runs the cartridge on the engine's SNES machine and watches its damage, status and special-effect routines, the
esper's spells learned and the level-up at a won battle's end, and a battler's AI script readied, run on its turn
and run when it answers an attack, with the three commands only a script queues: what each one reads and writes
while the game is played, whether every call so recorded comes out the
same from the port, or whether the port's rules give the same answers the cartridge's own code does.
It is a developer tool and is not part of the game.

```sh
cmake --build build --target ostinato-damage-trace
./build/ostinato-damage-trace <rom> <directory> [--from <state>] [--quit-after-frames <n>]
./build/ostinato-damage-trace <rom> <directory> --headless --frames <n> [--from <state>] [--inputs <script>] [--picture-at <frame>]... [--write-at <frame> <address> <bytes>]... [--watch <address> <count>]...
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
| What writes some cells, and when | `--headless --inputs <script> --frames <n> --watch <address> <count>` |

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

Fifteen routines are traced. An escape on each one's entry and on each instruction its callers return
to opens and closes a call; watches over every cell the routines read or write log each access made
inside a call. A return is logged only while a call of its routine is open, so the command dispatch's
return site, which every command passes, logs only the returns of the three AI commands:

| Routine | Entry | Returns to | Works out |
|---|---|---|---|
| `CalcDmg` | `$C2:2B9D` | `$C2:322E` | an attack's base damage |
| `CalcTargetDmg` | `$C2:0B83` | `$C2:346C` from the attack, `$C2:3509` from a spell's bounce | the damage to one target |
| the apply-and-show routine | `$C2:62EF` | `$C2:3473` from the attack, `$C2:3528` from a bounce | taking the damage off and showing the numerals |
| `UpdateStatus` | `$C2:4391` | `$C2:324D` from the attack, `$C2:24A7` from the battle's start, `$C2:50AA` from a formation change | every battler's waiting statuses taking effect |
| `MagicStatusEffect` | `$C2:4406` | `$C2:345C` from the attack, `$C2:34F9` from a bounce | an attack's statuses on one target |
| `DoAttackerEffect` | `$C2:3E7D` | `$C2:33A9` | an attack's special effect on the attack and its attacker |
| `DoTargetEffect` | `$C2:387E` | `$C2:345F` from the attack, `$C2:34FC` from a bounce | an attack's special effect on one target |
| `LearnGenjuMagic` | `$C2:602A` | `$C2:5E72` | at a won battle's end, the spells a party member's esper teaches learned further |
| `DoLevelUp` | `$C2:60C2` | `$C2:60B4` | at a won battle's end, one level-up of a party member with its esper's bonus |
| `InitAI` | `$C2:2D71` | `$C2:2C3D` as a monster is loaded, `$C2:3041` as a scripted battle seats a party member | a battler's AI script readied |
| `ExecMonsterAction` | `$C2:02DC` | `$C2:013C` from a turn, `$C2:21A1` for a party member with a script whose gauge fills | a script's turn part read |
| `ExecAIRetal` | `$C2:4BF4` | `$C2:4B9A` | a script's counterattack part read |
| `Cmd_2e`, `Cmd_2f`, `Cmd_30` | `$C2:1DFA`, `$C2:1E1A`, `$C2:1E5E` | `$C2:1401`, the command dispatch | a battle variable, a switch, an effect changed |

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
elements, statuses and whether it is away from the field; and what a battler's AI reads and writes
besides — the script reader's cells in direct page (`$7E:00F0-00FF`), the formation, the character
records' party bytes and the party played, the lores known and Strago's slot, the story's flags, the
battle type and a pincer's two sides, the party's spell lists, each battler's gauge, advance wait, block
graphics, script places, wait places, own timer and ATB rate, the counterattack line, the step being read, the battle's variables and
switches, the names listed and the party slots' members (`kTracedRanges` in `damage_oracle.h`). Inside a
call of `LearnGenjuMagic` or `DoLevelUp` the sixteen character records, the spells each character knows
and the first two battle's-end cells in direct page (`$7E:00EE-00EF`) are logged as well
(`kPartyRecordRanges`); inside any other call they are not. The rest of the battle's-end cells — the
win's flags, whether the party has magicite, the place of the spells being learned, the magic points and the level-up's
gains — are the script reader's cells and are logged in every call. The addresses are the US cartridges'.

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
the damage, status and special-effect routines among them.

This fights the same battle to a win with Terra holding an esper and one level-up away: Terrato
(`04`, the fifth esper) in her record's esper byte (`$7E:161E`), her level (`$7E:1608`) set to 3 and her
experience (`$7E:1611`, three bytes) set to the 208 level 4 needs. `press-a.inputs` presses A for 4
frames in every 16, which fights with each party member's first command at its first target:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/esper-win --headless --from build/first-fight/battle.state \
    --inputs tools/damage-trace/press-a.inputs --frames 1500 \
    --write-at 10 '$7E:161E' 04 --write-at 10 '$7E:1608' 03 --write-at 10 '$7E:1611' D00000
```

Frame 1400 shows "Got 96 GP" with both guards gone. `tests/fixtures/damage-trace/esper-win.log` is this
run: the fight, Terrato's three spells learned with the battle's 0 magic points, Terra's level-up from 3
to 4 and Wedge's and Vicks's from 1 to 2.

This lets the guards take their turns before the party acts, and gives the guard in battler slot 6 a
counterattack part: `hold-then-press-a.inputs` presses nothing for 1,200 frames and then fights as
`press-a.inputs` does, and the guard's counterattack place (`$7E:3274`) is set to Wild Cat's turn part,
which starts at place 3410 (`$0D52`) of the cartridge's script block:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/guards-counter --headless --from build/first-fight/battle.state \
    --inputs tools/damage-trace/hold-then-press-a.inputs --frames 2600 --write-at 10 '$7E:3274' 520D
```

The guards take four turns between them, each a Fight with their Battle or Special attack, and when one
guard is left it answers the party's attack with Fire Ball, as Wild Cat's part does with one monster
standing. `tests/fixtures/damage-trace/guards-counter.log` is this run.

The same fight with Retainer's counterattack part (if it is dead, its Special; place 5603, `$15E3`) in
that guard's counterattack place, and Petrify written into the guard's statuses to set
(`$7E:3DE0`) before the party acts:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/petrified-guard --headless --from build/first-fight/battle.state \
    --inputs tools/damage-trace/hold-then-press-a.inputs --frames 3000 \
    --write-at 10 '$7E:3274' E315 --write-at 1180 '$7E:3DE0' 40
```

The statuses take effect at frame 1378: the guard is petrified and marked as having died. Its
counterattack part runs at frame 1728 under the restriction a battler answering its own death runs
under, and queues its Special at a party member; a second counterattack call for it in the same step
runs nothing. `tests/fixtures/damage-trace/petrified-guard.log` is this run.

Each `--watch <address> <count>` watches `<count>` cells of work RAM from `<address>` (the same form as
`--write-at`'s) and logs to `cells.log` under `<directory>`, one line each, every write the cartridge makes
to them as it lands, their bytes after every frame, and every entry to the battle program:

```
battle entered <frame>
write <frame> $BB:AAAA <byte>
cells <frame> $BB:AAAA <the range's bytes>
```

A write made by a memory transfer rather than by an instruction shows only in the `cells` lines, and so
do the writes to cells the trace already watches (`writes not watched: $BB:AAAA` says which). This
follows the list of items obtained and its two counts from power-on through the first fight, won, and on
to the next battle (`first-two-battles.inputs`):

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/items-obtained --headless --frames 30000 \
    --inputs tools/damage-trace/first-two-battles.inputs --watch '$7E:602D' 80 --watch '$7E:64DA' 2
```

The battles begin at frames 10216 and 12224. Both counts become 0 three frames into each; the list's first
64 bytes become `$FF` at frames 10300 and 12274; and the list's last fifteen bytes are written only at
frames 64 and 9039, by the title and cutscene program being laid over `$7E:5000-$7E:7C62`.

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
A level-up takes its record from the record cells the call touched. The esper's spells take the
character from the spell cells read and the esper from which spells those are: each spell the esper
teaches is read once, and no two espers teach the same spells. The magic points are the cell the call
read, and a spell counts as stored when the call wrote the win's flags.
A battler's AI is rebuilt as the battle a script reads, from every cell the call read, with the cells
the oracle writes for a script everywhere else; a monster slot's held bit and its monster's index follow
each other, whichever of the two the call read. A script readied is the slot whose turn place the call
wrote, given the script whose turn part starts there. A turn and a counterattack are the battler whose
script place the call read (a counterattack that already ran this step reads only the battler's bit),
a counterattack answers the targets it read, and only the actions the call added to the battler's lists
are compared. A command is the action a script's step can hold whose bytes are the ones the call read,
and is worked out for each battler the cartridge's own command, given the same reads, makes the same
writes for. `SetStatus` and `ClearStatus` make an attack, whose own calls the log holds and replays;
the command's call itself, with another traced call inside it, is not among the calls the log holds.
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
`tests/test_trace_state.cpp` checks that it runs both effect routines, Metamorph's among them.
`esper-win.log` is the same fight won with Terrato written in (above), and `tests/test_espers_oracle.cpp`
checks what its four record calls read and wrote. The cartridge's own calls of the two status routines,
traced through the oracle, are replayed by `tests/test_status_oracle.cpp`, of the two effect routines by
`tests/test_effects_oracle.cpp`, and of the two routines on the party's records by
`tests/test_espers_oracle.cpp`. `tests/test_ai_trace.cpp` replays the cartridge's own AI calls traced
through the oracle — every script readied, every script's turns, every counterattack part on every path
through its guard and the three commands — and checks what `guards-counter.log` and
`petrified-guard.log` hold.

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
| `params` | `InitGfxParams` | `tests/test_cover_and_params.cpp` |
| `cover` | `CoverEffect` | `tests/test_cover_and_params.cpp` |
| `esper-learning` | `LearnGenjuMagic` | `tests/test_espers_oracle.cpp` |
| `level-up` | `DoLevelUp` | `tests/test_espers_oracle.cpp` |
| `ai-targets` | `CheckAITarget`, the battlers a monster's script names | `tests/test_ai_oracle.cpp` |
| `ai-conditions` | the condition routines a monster's script tests, through the cartridge's table of them | `tests/test_ai_oracle.cpp` |
| `ai-setup` | `InitAI`, a script readied into a slot that held another | `tests/test_ai_run_oracle.cpp` |
| `ai-turns` | `ExecMonsterAction`, one turn of a script | `tests/test_ai_run_oracle.cpp` |
| `ai-counters` | `ExecAIRetal`, a counterattack part on one of the eight paths through its guard | `tests/test_ai_run_oracle.cpp` |
| `ai-commands` | `Cmd_2e`, `Cmd_2f` and `Cmd_30`, the three commands only a script queues, with every effect; `SetStatus` and `ClearStatus` run the whole attack frame | `tests/test_ai_run_oracle.cpp` |
| `attack` | `ExecAttack`, every attack of a command's attack, a Fight's and a Capture's hand loaded at each swing among them, against `battle::runAttack()` | `tests/test_attack_oracle.cpp` |
| `self-attack` | `ExecSelfAttack`, an attack aimed at its attacker, against `battle::runSelfAttack()` | `tests/test_attack_oracle.cpp` |
| `own-actions` | `Cmd_22`, `Cmd_26`, `Cmd_29` and `Cmd_2d`, the battle's own damage over time, spell with no caster, status timers running out and Seize's drain, and `Cmd_30`'s `SetStatus` and `ClearStatus`, each run whole against `CommandExecutor::execute()` | `tests/test_own_actions_oracle.cpp` |
| `add-exp` | `AddExp`, a share of a win's experience added to a record | `tests/test_rewards_oracle.cpp` |
| `item-learning` | `LearnItemMagic`, the spells a record's equipment teaches, with the Cursed Shield's count | `tests/test_rewards_oracle.cpp` |
| `level-ups` | `CheckLevelUp`, every level a record reaches with what each teaches and the messages told | `tests/test_rewards_oracle.cpp` |
| `abilities` | `LearnAbilities`, what an actor learns at a level | `tests/test_rewards_oracle.cpp` |
| `rages` | `LearnRage`, the rages of the six monster slots | `tests/test_rewards_oracle.cpp` |
| `items-obtained` | graphics command `$05`, an item added to the items obtained, against `battle::obtainItem()` | `tests/test_battle_end_oracle.cpp` |
| `items-merged` | graphics command `$0A`, the items obtained merged into the inventory, against `battle::mergeItemsObtained()` | `tests/test_battle_end_oracle.cpp` |
| `wrap-up` | `_c24903`, the close every ending but two runs, against `battle::wrapUpBattle()`, with every cell it reads written first | `tests/test_battle_end_oracle.cpp` |
| `win` | `WinBattle`, the win whole — the celebration, the wrap-up, the colosseum, experience, levels, spells, lores, the dance, drops and gold, every message — against `battle::playBattleEnd()` | `tests/test_battle_end_oracle.cpp` |
| `loss` | `LoseBattle`, the wrap-up and the message, against `battle::playBattleEnd()` and the defeat switch `battle::writeBack()` sets | `tests/test_battle_end_oracle.cpp` |
| `write-back` | `UpdateSRAM`, what the game keeps, against `battle::writeBack()` | `tests/test_battle_end_oracle.cpp` |

While one of the reward routines runs, its calls to the graphics program (`ExecBtlGfx`, `$C2:6411`) are
answered by the oracle at the graphics program's entry (`$C1:0000`): each call is recorded with the cells
the graphics program reads — the command, the party slot at `$7E:0010`, the battle script's first five
bytes, the item record at `$7E:2E72` and the four message variables — and the commands that wait on the
screen or the sound return at once. Command `$05` (an item added to the items obtained) and command `$0A`
(those items merged into the inventory) run as the cartridge holds them, each found through the
cartridge's own table of graphics commands at `$C1:0008`. A message is compared on the numbers its text
shows.

`--replay-traced` also traces the traced routines' calls in each draw and replays them against the
port, as `tests/test_trace_replay.cpp`, `tests/test_status_oracle.cpp`, `tests/test_effects_oracle.cpp`
and `tests/test_espers_oracle.cpp` do; a target-effect draw's calls are given the draw's target. On
`attack`, `self-attack` and `own-actions` the traced routines are called by the frame itself, so each call closes at
the frame's own return site (the return column above).

While the wrap-up, the win or the loss runs, its calls to the graphics program are answered the same way;
a run of frame waits is compared as one wait of that many frames, a merge shows nothing, an item added and
merged at once (a drop, the prize) shows only its message, and the win's celebration (command `$08`) is
compared on whether the fanfare and battle event 1 play, as the formation's flags and the field's flags
decide.

Each `attack`, `self-attack`, `own-actions`, `wrap-up`, `win`, `loss` and `write-back` call also watches the battle's work RAM, `$7E:0000-3FFF`, for a cell
the frame reads before anything wrote it. `DamageOracle::unwrittenReads()` lists them after a call;
`kCellsLeftAsTheMachineHasThem` in `damage_oracle.h` holds the few the frame reads and nothing it shows
or keeps depends on, each with its reason. Any other cell in the list is one the scenario must write.

## Files

| File | What it holds |
|---|---|
| `main.cpp` | the command line, the window and the trace on it, the trace with no window and its watched cells (`CellLog`), the scripted run to a battle, the state check, the replay run and the check run |
| `machine_state.{h,cpp}` | `MachineState`, `writeMachineState()`, `readMachineState()`, `stateIsOf()` and `cartridgeName()`, a state's file; `InputStep`, `readInputScript()` and `buttonsAt()`, an input script; `CellWrite`, `readCellWrite()`, `declareWorkRamBytes()` and `writeCells()`, the bytes `--write-at` writes |
| `first-fight.inputs` | the buttons from power-on to the first fight in Narshe |
| `press-a.inputs` | A pressed for 4 frames in every 16, which fights the first fight to a win from its state |
| `hold-then-press-a.inputs` | nothing pressed for 1,200 frames, then A for 4 frames in every 16: the guards act first, then the party fights the first fight to a win |
| `first-two-battles.inputs` | from power-on through the first fight, won as `press-a.inputs` wins it, and on through the town to the next battle |
| `damage_oracle.{h,cpp}` | `DamageOracle`, the cartridge's routines isolated and called over a `DamageScenario`: the formulas, the damage to one target, the damage taken and shown, a spell bounced off Reflect, the statuses taking effect, an attack's statuses on one target, its special effect on its attacker and on one target, the cover and the attack's animation set up; over a `RecordScenario`: the esper's spells learned and the level-up; over an `AiScenario`: the battlers a monster's script names, the conditions it tests, a script readied, its turns and counterattacks, and the three commands only a script queues; the whole attack frame, `attack()` and `selfAttack()`, and the battle's own actions, `ownAction()`, with `unwrittenReads()`; `drawScenario()`, `drawTargetScenario()`, `drawApplyScenario()`, `drawReflectScenario()`, `drawStatusChangesScenario()`, `drawStatusEffectScenario()`, `drawAttackerEffectScenario()`, `drawTargetEffectScenario()`, `drawCoverScenario()`, `drawLearningScenario()`, `drawLevelUpScenario()`, `drawAiScenario()`, `drawAiTargetsCase()`, `drawAiConditionCase()`, `drawAttackScenario()`, `drawSelfAttackScenario()`, `drawOwnActionCase()`, `drawEndingScenario()` and `drawItemsObtainedScenario()`, the seeded draws; over an `EndingScenario`: the wrap-up, `wrapUp()`, the win, `win()`, the loss, `loss()`, and what the game keeps, `writeBack()`, with `drawWinScenario()`; over an `ItemsObtainedScenario`: the graphics program's two commands for the items obtained; `itemsObtainedTailAtPowerOn()`, the title program laid out and the list's last three places read; `portTargetDamage()`, `portApplyDamage()`, `portReflectedAttack()`, `portStatusChanges()`, `portStatusEffect()`, `runAttackerEffect()`, `runTargetEffect()`, `portInitParams()`, `portCoverTarget()`, `portLearnFromEsper()`, `portLevelUp()`, `portAiTargets()`, `portAiCondition()`, `portAttack()`, `portSelfAttack()`, `attackResult()`, `portOwnAction()` and `differences()`, the port read back the same way and compared cell by cell; `DamageTrace`, the trace's escapes, watches and lines, on any machine; `parseTrace()` and `DamageOracle::replay()`, a recorded call read back and replayed against the port, a battler's AI rebuilt by `readAiScenario()` |
