# ostinato-damage-trace

Runs the cartridge on the engine's SNES machine and watches its damage, status and special-effect routines, the
esper's spells learned and the level-up at a won battle's end, a battler's AI script readied, run on its turn
and run when it answers an attack, with the three commands only a script queues, and a battle's end — the
win's steps on the party's records, and the win, the loss and the wrap-up whole: what each one reads and writes
while the game is played, whether every call so recorded comes out the
same from the port, or whether the port's rules give the same answers the cartridge's own code does.
It is a developer tool and is not part of the game.

```sh
cmake --build build --target ostinato-damage-trace
./build/ostinato-damage-trace <rom> <directory> [--from <state>] [--quit-after-frames <n>]
./build/ostinato-damage-trace <rom> <directory> --headless --frames <n> [--from <state>] [--inputs <script>] [--picture-at <frame>]... [--write-at <frame> <address> <bytes>]... [--watch <address> <count>]... [--seam]
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
| That the script reaches its fight, the same every run | `--to-battle tools/damage-trace/inputs/first-fight.inputs --twice` |
| That a restored machine carries on where it stood | `--check-state 300` |
| A fight with scripted buttons, with no window | `--headless --from <state> --inputs <script> --frames 3000`, then `--replay` |
| A fight by hand | `--from <state>`, then `--replay` |
| Where a script has got to | `--headless --inputs <script> --frames <n> --picture-at <frame>` |
| A fight set up by hand | `--headless --from <state> --frames <n> --write-at <frame> <address> <bytes>` |
| What writes some cells, and when | `--headless --inputs <script> --frames <n> --watch <address> <count>` |
| What the graphics program hands the battle program in a fight | `--headless --from <state> --inputs <script> --frames <n> --seam` |

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

Nineteen routines are traced call by call. An escape on each one's entry and on each instruction its
callers return to opens and closes a call; watches over every cell the routines read or write log each
access made inside a call. A return is logged only while a call of its routine is open, so the command
dispatch's return site, which every command passes, logs only the returns of the three AI commands:

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
| `AddExp` | `$C2:6235` | `$C2:5E4F`, and `$C2:5E59` for an Exp. Egg's second share | at a won battle's end, a share of experience added to a record |
| `LearnItemMagic` | `$C2:5FEF` | `$C2:5E6A` | at a won battle's end, the spells a record's equipment teaches, with the Cursed Shield's count |
| `LearnAbilities` | `$C2:61B6` | `$C2:60BF` | at a level-up, the natural spell, SwdTech or Blitz the level teaches |
| `LearnRage` | `$C2:4A07` | `$C2:48EE` | after a Leap, the rages of the six monster slots |

`LearnItemMagic`'s only call in the battle program is the win's; three bytes at `$C2:7477` that read
like a second call lie in the compressed cutscene program, which the battle never runs.

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
play clock's frames, the direct-page params block, attack flags and multiplier, the party's gil and
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
switches, the names listed and the party slots' members (`kTracedRanges` in `src/trace/trace_log.h`). Inside a
call of one of the six routines on the party's records — `LearnGenjuMagic`, `DoLevelUp` and the four
above — the sixteen character records, the spells each character knows, the Morph supply with the
SwdTechs and Blitzes known, the rages and dances known and the first two battle's-end cells in direct
page (`$7E:00EE-00EF`) are logged as well (`kPartyRecordRanges`); inside any other call they are not.
The rest of the battle's-end cells — the win's flags, whether the party has magicite, the place of the
spells being learned, the magic points and the level-up's gains — are the script reader's cells and are
logged in every call. The addresses are the US cartridges'.

`LearnAbilities` works out a level for the actor and level its caller read from the record just before
the call, so its `enter` is followed by the last actor byte and level byte read from any record:

```
f1355 enter LearnAbilities $C2:61B6
f1355 params ...
f1355 abilities actor 00 level 06
```

Three routines are traced whole, as what their cells hold, because each calls traced routines of its
own: the win (`WinBattle`, `$C2:5D57`, returning to `$C2:488F`), the loss (`LoseBattle`, `$C2:5FCA`,
returning to `$C2:4831` from the end check and `$C2:48F5` from Banon's ending) and the wrap-up
(`_c24903`, `$C2:4903`, returning to `$C2:48A4`, `$C2:4A28`, and inside the win and the loss to
`$C2:5D74` and `$C2:5FD3`). On entry and on return every range of `kEndingRanges` — the party, the
inventory, the items obtained with their counts, the battlers' cells the ending reads, the battle's own
cells and switches — is logged as one line of bytes, and inside them, as inside any open call, each
battle message is logged as `ShowMsg` hands it to the graphics program (`$C2:5FEA`), with its four
variables:

```
f1101 snapshot WinBattle enter $C2:5D57
f1101 snapshot WinBattle $7E:00B0 0000DEFF061D838F0000024300000204
...
f1226 message 27 000020 000000 000000 000060
...
f1504 snapshot WinBattle return $C2:488F
f1504 snapshot WinBattle $7E:00B0 0080DEFF061D838F0000024300000404
...
```

A snapshot, a message and an `enter`'s context are what the cells hold at that moment: the machine
answers a read with what its last step published, and the trace lays over it the last byte written to
each cell it watches — by the cartridge, or by `--write-at`, which it is told of. It watches the cells of
`kEndingRanges` too, logging none of their accesses.

## Starting at a battle

`--to-battle <inputs>` makes a state to start from, and `--from <state>` starts the trace from it, so a
fight is played without playing up to it:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/opening --to-battle tools/damage-trace/inputs/first-fight.inputs
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
    --inputs tools/damage-trace/inputs/press-a.inputs --frames 1500 \
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
    --inputs tools/damage-trace/inputs/hold-then-press-a.inputs --frames 2600 --write-at 10 '$7E:3274' 520D
```

The guards take four turns between them, each a Fight with their Battle or Special attack, and when one
guard is left it answers the party's attack with Fire Ball, as Wild Cat's part does with one monster
standing. `tests/fixtures/damage-trace/guards-counter.log` is this run.

The same fight with Retainer's counterattack part (if it is dead, its Special; place 5603, `$15E3`) in
that guard's counterattack place, and Petrify written into the guard's statuses to set
(`$7E:3DE0`) before the party acts:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/petrified-guard --headless --from build/first-fight/battle.state \
    --inputs tools/damage-trace/inputs/hold-then-press-a.inputs --frames 3000 \
    --write-at 10 '$7E:3274' E315 --write-at 1180 '$7E:3DE0' 40
```

The statuses take effect at frame 1378: the guard is petrified and marked as having died. Its
counterattack part runs at frame 1728 under the restriction a battler answering its own death runs
under, and queues its Special at a party member; a second counterattack call for it in the same step
runs nothing. `tests/fixtures/damage-trace/petrified-guard.log` is this run.

This wins the same fight with Terra at level 5 (`$7E:1608`) and 666 experience (`$7E:1611`, `9A0200`), 6
short of the 672 level 6 needs, and the battle's random-number position (`$7E:00BE`) set to 2 on the
frame the win begins, so the first fallen guard's draw is 31, below 32, and gives its rare drop:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/rewards-win --headless --from build/first-fight/battle.state \
    --inputs tools/damage-trace/inputs/press-a.inputs --frames 1600 --picture-at 1400 \
    --write-at 10 '$7E:1608' 05 --write-at 10 '$7E:1611' 9A0200 --write-at 1101 '$7E:00BE' 02
```

The win runs from frame 1101 to 1504: 32 experience each, a level for Wedge, Vicks and Terra, Terra's
Antdot, a Tonic (frame 1400 shows "Got Tonic × 1") and 96 GP. `tests/fixtures/damage-trace/rewards-win.log`
is this run. And this loses it: the three party members' HP (`$7E:3BF4`, three words) set to 1, nothing
pressed until the guards have felled them, then A to close "Annihilated":

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/rewards-loss --headless --from build/first-fight/battle.state \
    --inputs tools/damage-trace/inputs/hold-then-press-a.inputs --frames 1600 --write-at 10 '$7E:3BF4' 010001000100
```

The loss begins at frame 1097 and returns at 1211. `tests/fixtures/damage-trace/rewards-loss.log` is this
run. With nothing pressed at all the message waits for a button and the loss never returns.

Each `--watch <address> <count>` watches `<count>` cells of work RAM from `<address>` (the same form as
`--write-at`'s) and logs to `cells.log` under `<directory>`, one line each, every write the cartridge makes
to them as it lands, their bytes after every frame, and every entry to the battle program:

```
battle entered <frame>
write <frame> $BB:AAAA <byte>
cells <frame> $BB:AAAA <the range's bytes>
```

A write made by a memory transfer rather than by an instruction shows only in the `cells` lines. A write
to a cell the trace already watches is heard through the trace's own watch and logged the same way. This
follows the list of items obtained and its two counts from power-on through the first fight, won, and on
to the next battle (`first-two-battles.inputs`):

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/items-obtained --headless --frames 30000 \
    --inputs tools/damage-trace/inputs/first-two-battles.inputs --watch '$7E:602D' 80 --watch '$7E:64DA' 2
```

The battles begin at frames 10216 and 12224. Both counts become 0 three frames into each; the list's first
64 bytes become `$FF` at frames 10300 and 12274; and the list's last fifteen bytes are written only at
frames 64 and 9039, by the title and cutscene program being laid over `$7E:5000-$7E:7C62`.

`--seam` logs, in place of the trace, what the graphics program hands the battle program: every write the
graphics program or the interrupt makes to the battle's cells (`$7E:0000-3FFF`), and every read the battle
program makes of a cell whose last write was one of theirs, with the code that wrote it. The log is
`seam.log` under `<directory>`, one line each:

```
battle entered <frame>
graphics <frame> <command>
clock <frame>
write <frame> <writer> $7E:AAAA <byte>
read <frame> <reader> $7E:AAAA <byte> <writer>
battle returned <frame>
```

The code running is told apart by where it is entered. The battle program runs from its entry
(`$C2:0000`) to `TerminateBattle`'s return (`$C2:00CB`), and nothing outside it is logged. The graphics
program runs from its entry (`$C1:0000`) to the battle program's instruction after the call to it
(`$C2:6429`); inside a graphics call, the battle program's clock runs from its entry (`$C2:0003`) to the
graphics program's instruction after either call of it (`$C1:0223`, `$C1:0249`), each entry is a `clock`
line, and its reads are logged as `clock`;
the interrupt runs from `BattleNMI` (`$C1:0BA7`) to its return (`$C1:0D4A`) and is logged as
`interrupt-in-graphics`, `interrupt-in-battle` or `interrupt-in-clock`, by what it interrupted. A
`graphics` line names the command a call asks for, the first of the graphics program's handlers entered
after its entry. A write the battle program makes is not logged; it makes the cell the battle program's
again. A write made by a memory transfer rather than by an instruction is not seen. `--seam` and
`--watch` go apart, since the seam watches every cell `--watch` would. This runs it over the first fight:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/seam --headless --from build/first-fight/battle.state \
    --inputs tools/damage-trace/inputs/press-a.inputs --frames 1600 --seam
```

Over the seven recordings' fights the battle program reads, of what the graphics side writes: the frame
counter `$0E`; the player's records `$2BAE-$2BCD`; the menu's time stop `$2F41`; the run buttons
`$2F45`; the four equipment-changed flags `$2F30-$2F33`; the inventory, after an obtained item is merged
into it; the field's timer flags `$1DD1`, from the interrupt; the monsters standing `$2F2F` and the
monsters that can be aimed at `$2F46`, from the opening command `$00`; the multiply's scratch `$00EB`;
and the stack. In all seven the opening `$00` runs 159 pictures, the interrupt's body 75 times: 37 with no
clock call, then 36 clock calls over the last 38, two of the waits taking two frames.
Two more cells the graphics side writes for the battle program are read only where none of the seven
fights goes: the play clock's frames `$021E`, which its interrupt steps and the Fixed Dice read, and
the monsters on each side of a pincer `$2EAC-$2EAD`, which its opening lays out.

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
A step of a won battle is rebuilt as the party and the win's cells it read: the experience added and the
spells equipment teaches take their record from the record cells the call touched, the spells learned
being the list the win's spell-list place names; the abilities take the actor and level logged at the
call's entry; the rages take the monsters read. The messages the call showed are compared with the
port's. The port's answer is compared with every cell the routine wrote.

An ending is rebuilt from its entry snapshot: its cells written, read back as the battle at its end
(`DamageOracle::readEndingScenario()`) and played out by the port's own ending — `battle::playBattleEnd()`
with `BattleWon`, `BattleLost` handed the message its caller hands (Banon fell from `$C2:48F5`; otherwise
"Annihilated", or none in the colosseum), or the wrap-up alone. The port's party, inventory, items
obtained, hands, meters, switches and random-number position are compared with the return snapshot, and
its messages with the ones the ending showed, in order. What else an ending shows — menus closing, the
wait, the celebration — is not in the log and is not compared here; the `wrap-up`, `win` and `loss`
checks compare it. An ending with an equipment change pending cannot be rebuilt.

Each call and each ending is logged to `replay.log` with its differences; any difference is printed and
fails the run:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc ~/traces/opening --replay
```

A call the log opens and never closes — the routine left by some other way than its callers' return
sites — is not replayed. A call that cannot be rebuilt counts as a difference, with the reason logged.
Copy a recording into `tests/fixtures/damage-trace/` (any `.log` file name) and
`tests/oracle/test_trace_replay.cpp` replays it with the rest of the damage tests. `first-fight.log` there
is the first fight in Narshe, played from `first-fight.inputs`'s state, in which Terra's Bio Blast sets
Poison and Sap on both guards; `tests/oracle/test_trace_state.cpp` checks that it holds a status being
set. `failed-metamorph.log` is the same fight with the failed Metamorph written in (above), and
`tests/oracle/test_trace_state.cpp` checks that it runs both effect routines, Metamorph's among them.
`esper-win.log` is the same fight won with Terrato written in (above), and
`tests/oracle/test_espers_oracle.cpp` checks what its four record calls read and wrote. The cartridge's
own calls of the two status routines, traced through the oracle, are replayed by
`tests/oracle/test_status_oracle.cpp`, of the two effect routines by `tests/oracle/test_effects_oracle.cpp`,
and of the two routines on the party's records by `tests/oracle/test_espers_oracle.cpp`.
`tests/oracle/test_ai_trace.cpp` replays the cartridge's own AI calls traced through the oracle — every
script readied, every script's turns, every counterattack part on every path through its guard and the
three commands — and checks what `guards-counter.log` and `petrified-guard.log` hold.
`tests/oracle/test_rewards_trace.cpp` replays the win's steps and the wrap-up, the win and the loss traced
through the oracle, and checks and replays `rewards-win.log` and `rewards-loss.log`. A brief run of the
tests skips every replay (§ Checking).

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
| `base-damage` | `CalcDmg` | `tests/oracle/test_damage_formulas.cpp` |
| `relic-bonus` | `RelicDmgEffect` | `tests/oracle/test_damage_formulas.cpp` |
| `multiplier` | `ApplyDmgMult` | `tests/oracle/test_damage_formulas.cpp` |
| `target-damage` | `CalcTargetDmg` | `tests/oracle/test_target_damage.cpp` |
| `apply-damage` | the apply-and-show routine | `tests/oracle/test_apply_damage.cpp` |
| `reflected-attack` | `CalcReflectDmg` | `tests/oracle/test_reflected_attack.cpp` |
| `status-changes` | `UpdateStatus` | `tests/oracle/test_status_changes.cpp` |
| `status-effect` | `MagicStatusEffect` | `tests/oracle/test_status_effect.cpp` |
| `attacker-effect` | `DoAttackerEffect` | `tests/oracle/test_effects_oracle.cpp` |
| `target-effect` | `DoTargetEffect` | `tests/oracle/test_target_effects.cpp` |
| `params` | `InitGfxParams` | `tests/oracle/test_cover_and_params.cpp` |
| `cover` | `CoverEffect` | `tests/oracle/test_cover_and_params.cpp` |
| `esper-learning` | `LearnGenjuMagic` | `tests/oracle/test_espers_oracle.cpp` |
| `level-up` | `DoLevelUp` | `tests/oracle/test_espers_oracle.cpp` |
| `ai-targets` | `CheckAITarget`, the battlers a monster's script names | `tests/oracle/test_ai_oracle.cpp` |
| `ai-conditions` | the condition routines a monster's script tests, through the cartridge's table of them | `tests/oracle/test_ai_oracle.cpp` |
| `ai-setup` | `InitAI`, a script readied into a slot that held another | `tests/oracle/test_ai_run_oracle.cpp` |
| `ai-turns` | `ExecMonsterAction`, one turn of a script | `tests/oracle/test_ai_run_oracle.cpp` |
| `ai-counters` | `ExecAIRetal`, a counterattack part on one of the eight paths through its guard | `tests/oracle/test_ai_run_oracle.cpp` |
| `ai-commands` | `Cmd_2e`, `Cmd_2f` and `Cmd_30`, the three commands only a script queues, with every effect; `SetStatus` and `ClearStatus` run the whole attack frame | `tests/oracle/test_ai_run_oracle.cpp` |
| `attack` | `ExecAttack`, every attack of a command's attack, a Fight's and a Capture's hand loaded at each swing among them, against `battle::runAttack()` | `tests/oracle/test_attack_oracle.cpp` |
| `self-attack` | `ExecSelfAttack`, an attack aimed at its attacker, against `battle::runSelfAttack()` | `tests/oracle/test_attack_oracle.cpp` |
| `own-actions` | `Cmd_22`, `Cmd_26`, `Cmd_29` and `Cmd_2d`, the battle's own damage over time, spell with no caster, status timers running out and Seize's drain, and `Cmd_30`'s `SetStatus` and `ClearStatus`, each run whole against `CommandExecutor::execute()` | `tests/oracle/test_own_actions_oracle.cpp` |
| `add-exp` | `AddExp`, a share of a win's experience added to a record | `tests/oracle/test_rewards_oracle.cpp` |
| `item-learning` | `LearnItemMagic`, the spells a record's equipment teaches, with the Cursed Shield's count | `tests/oracle/test_rewards_oracle.cpp` |
| `level-ups` | `CheckLevelUp`, every level a record reaches with what each teaches and the messages told | `tests/oracle/test_rewards_oracle.cpp` |
| `abilities` | `LearnAbilities`, what an actor learns at a level | `tests/oracle/test_rewards_oracle.cpp` |
| `rages` | `LearnRage`, the rages of the six monster slots | `tests/oracle/test_rewards_oracle.cpp` |
| `items-obtained` | graphics command `$05`, an item added to the items obtained, against `battle::obtainItem()` | `tests/oracle/test_battle_end_oracle.cpp` |
| `items-merged` | graphics command `$0A`, the items obtained merged into the inventory, against `battle::mergeItemsObtained()` | `tests/oracle/test_battle_end_oracle.cpp` |
| `wrap-up` | `_c24903`, the close every ending but two runs, against `battle::wrapUpBattle()`, with every cell it reads written first | `tests/oracle/test_battle_end_oracle.cpp` |
| `win` | `WinBattle`, the win whole — the celebration, the wrap-up, the colosseum, experience, levels, spells, lores, the dance, drops and gold, every message — against `battle::playBattleEnd()` | `tests/oracle/test_battle_end_oracle.cpp` |
| `loss` | `LoseBattle`, the wrap-up and the message, against `battle::playBattleEnd()` and the defeat switch `battle::writeBack()` sets | `tests/oracle/test_battle_end_oracle.cpp` |
| `write-back` | `UpdateSRAM`, what the game keeps, against `battle::writeBack()` | `tests/oracle/test_battle_end_oracle.cpp` |
| `start` | the battle program whole from its entry to the loop's first frame wait, over a drawn start (`drawStartScenario()`), against `battle::startBattle()`: every cell compared, and the write-set, carried-read and read-set gates (`checkStart()`, below) | `tests/oracle/test_battle_start_oracle.cpp` |
| `battle` | the battle program whole from its start to its end, a loop step at a time, over a drawn battle with no player, aimed now and then at a row of the reach table (`drawBattle()`), against `battle::BattleSim`: every step compared with both gates, the requests, the end and what the game keeps (`checkBattle()`, below) | `tests/oracle/test_battle_whole_oracle.cpp` |

`start` also prints, after its draws, how many reached each row of its reach table (`startReachRows()`):
each battle type, a preemptive strike, the colosseum, the final battle, a scripted setup, Gau's return, an
empty party slot, a morphed member, a randomized formation, a conditional battle, and each branch of the
battle type's choice a Back Guard, a Gale Hairpin and a party of fewer than three take. A row no draw
reached is named. The test's sweep draws 1,300 starts and fails on any row at zero; a short `--check` run
draws too few to reach every row and only names them. `battle` prints its reach table the same way
(`battleReachRows()`, below), and its test's sweep draws 100 battles and fails on any row at zero. A
battle the oracle cannot run or lay is reported as a difference, with the reason.

With `--brief` each routine draws no more than its own brief count, 20 for every routine but `battle`,
whose battles take about a second each and which draws 2:

```sh
./build/ostinato-damage-trace rom/cartridge.sfc /tmp/damage --check 20 --brief
```

While one of the reward routines runs, its calls to the graphics program (`ExecBtlGfx`, `$C2:6411`) are
answered by the oracle at the graphics program's entry (`$C1:0000`): each call is recorded with the cells
the graphics program reads — the command, the party slot at `$7E:0010`, the battle script's first five
bytes, the item record at `$7E:2E72` and the four message variables — and the commands that wait on the
screen or the sound return at once. Command `$05` (an item added to the items obtained) and command `$0A`
(those items merged into the inventory) run as the cartridge holds them, each found through the
cartridge's own table of graphics commands at `$C1:0008`. A message is compared on the numbers its text
shows. Each place of the items obtained is compared every byte, an empty place too, since a battle hands
its last three on as they stand; an inventory slot is compared as a `battle::BattleItem` holds it. The
ending's scenario lays the last record's targeting at `$7E:2E74`
(`BattleInventory::lastRecordTargeting`), which a colosseum win with no prize records.

`--replay-traced` also traces the traced routines' calls and endings in each draw and replays them
against the port, as `tests/oracle/test_trace_replay.cpp`, `tests/oracle/test_status_oracle.cpp`,
`tests/oracle/test_effects_oracle.cpp`, `tests/oracle/test_espers_oracle.cpp` and
`tests/oracle/test_rewards_trace.cpp` do; a
target-effect draw's calls are given the draw's target. The oracle tells the trace what it hands a
routine itself: the actor and level of an `abilities` call and the message of a `loss` call, logged as
`snapshot LoseBattle handed <message>`. On
`attack`, `self-attack` and `own-actions` the traced routines are called by the frame itself, so each call closes at
the frame's own return site (the return column above).

While the wrap-up, the win or the loss runs, its calls to the graphics program are answered the same way;
a run of frame waits is compared as one wait of that many frames, a merge shows nothing, an item added and
merged at once (a drop, the prize) shows only its message, and the win's celebration (command `$08`) is
compared on whether the fanfare and battle event 1 play, as the formation's flags and the field's flags
decide.

Each `attack`, `self-attack`, `own-actions`, `wrap-up`, `win`, `loss` and `write-back` call also watches the battle's work RAM, `$7E:0000-3FFF`, for a cell
the frame reads before anything wrote it. `DamageOracle::unwrittenReads()` lists them after a call;
`kCellsLeftAsTheMachineHasThem` in `src/scenarios/battle/battle_cells.h` holds the few the frame reads and nothing it shows
or keeps depends on, each with its reason. Any other cell in the list is one the scenario must write.

### The tests that ask the cartridge

The test binaries ask the oracle two ways. A hand check works a case out by hand from the original,
runs the port on it and, beside it, the cartridge's own routine; the port must give the hand-worked
value and the cartridge the same. Hand checks sit in `tests/` with the port's other tests: the damage
formulas, the damage to one target and the damage taken and shown, the statuses, the special effects
and the control menu, the cover and the attack's animation, the espers at a battle's end, the rewards,
the attack frame, the battle's own actions, the AI's targets, conditions, runs and commands, and the
last places of the items obtained at power-on. A test that asks only the cartridge — a sweep over
drawn scenarios, a replay of a trace or a recording, a read-set or write-set gate, the battle whole,
the oracle's own checks of where the routines are — sits in `tests/oracle/`, under the file name of
the hand checks it goes with (`tests/oracle/test_damage_formulas.cpp` holds the formulas' sweeps), and
is built into the same test binary.

How much of the cartridge a run of the tests asks is chosen by the environment variable
`OSTINATO_CARTRIDGE_CHECKS`, read in `tests/with_cartridge.h`:

| Value | The run |
|---|---|
| unset or empty | everything: every test runs, and every hand check asks the cartridge beside the port |
| `brief` | the hand checks run on the port alone, and every test that hosts the cartridge is skipped, giving the reason `asks the cartridge on the engine's machine; a brief run leaves it out (OSTINATO_CARTRIDGE_CHECKS=brief)` — but for the oracle's own tests (`tests/oracle/test_damage_oracle.cpp`) and `DamageTraceTool.ChecksEveryRoutineBriefly`; the trace's parsers, state files, input scripts and cell writes need no machine and run |
| anything else | the first test that asks fails, naming the variable and the value |

With `FF6_VANILLA_ROM` naming the cartridge, from the repository's root:

```sh
ctest --test-dir build -j 3                                    # everything
OSTINATO_CARTRIDGE_CHECKS=brief ctest --test-dir build -j 3    # brief
```

`DamageTraceTool.ChecksEveryRoutineBriefly` runs the tool itself, `--check 20 --brief` through every
routine above, in both runs (`cmake/run_damage_trace_check.cmake`); it passes on the tool's exit code, and
the directory the tool writes is removed after.

A new hand check takes its cartridge from `withCartridge()` and asks it through a helper that does
nothing where there is no cartridge to ask; a new test that asks only the cartridge goes in
`tests/oracle/`, opens with `OSTINATO_SKIP_WHEN_BRIEF()` and takes the oracle from
`withStartedOracle()`. `tests/with_cartridge.h` opens with an example of each.

## The battle whole

`DamageOracle::battleStart(scenario)` calls the battle program whole, as the field calls it
(`Battle_ext`, `$C2:0000`, a long call that returns once the battle closes). A `StartScenario` is what a
battle's start reads that the field sets first — the `battle::BattleStart` the port's own start takes,
the party with the HP and MP boost bits the port keeps no field for, and the battle switches — and
`writeStartScenario()` lays it into those cells: the formation, the background and the field's flags, the
play clock's frames, the frame count, the byte above the random numbers' position (`$00BF`,
`StartScenario::byteAboveRngPosition`), the story countdown's flag cleared, the battle settings and the spell order, the switches, the story's flags for
Shadow and Gau, the party played, the inventory, the play time at the last save, the last three places of
the items obtained (`BattleStart::itemsObtainedTail`, as five-byte item records), and a colosseum match
or the final battle's line of fighters. `readStartScenario()` reads them back, from a written machine or
from one restored to the tick before a battle; `trace::differences()` names every field two scenarios
part on. `battleStartAsItStands()` makes the same call from the cells as they stand. The byte above the
random numbers' position picks the 256 bytes the Chain Saw's roll reads, and `setByteAboveRngPosition()`
sets it with the port's `BattleStart::chainSawRollEntries`, read from the hosted image at the RNG table's
place plus 256 × the byte (`chainSawRollEntries()`, `DamageOracle::cartridge()`); `readStartScenario()`
does the same. A drawn start holds 0 three times in four and 1 to 255 otherwise, from a generator of its
own.

While the battle program runs, its calls to the graphics program are answered as the reward routines'
are, each with no frame and the battle's time stopped. The answer to the opening command, `$00`, also
lays what the graphics program's own opening lays for the battle program: the windows' wallpaper
(`$2F34`), the configuration's (`$1D4E` & 7); the monsters standing (`$2F2F`), the formation's monsters
present; the monsters that can be aimed at (`$2F46`), every one, or
the reach byte of the scripted setup the start turned on; the four equipment-changed flags, cleared;
the items obtained, its two counts (`$64DA-$64DB`) zeroed and its first 64 bytes (`$602D-$606C`, every
item of the first thirteen places) emptied to `$FF`, the last three places left as they are; and the
monsters on each side of a pincer (`$2EAC-$2EAD`), from the port's own `layOutMonsters()` with the
monsters' sprites handed over by `useMonsterSprites()`, which a whole battle needs before its call. At
the loop's first frame wait — the first command `$01` — the battle's cells, `$7E:0000-3FFF`, are read into
`StartResult::cells`, with every cell the start read before anything wrote it
(`StartResult::unwrittenReads`). The battle is then ended by the cartridge's own end-of-battle event 4,
"end the battle now", so the wrap-up, what the game keeps and the close run, and the call returns:
`StartResult::returned` is true when the last command asked for is the close, `$09`. The loop runs the
interrupting actions before its end check, so the ones the start left — an opening message, a pose
reset — are taken off their list (`$340A`) as the battle is ended; the cells read at the wait hold them.

`checkStart(scenario)` runs one start whole and checks it: `battleStart()`, the port's start over the
same scenario (`portStart()`), the cells compared (`compareBattleState()`), the write-set and
carried-read gates, and every read before anything wrote it that is not left as the machine has it,
in the battle's cells or past them. It returns a `StartCheck`: one line per difference, each gate's
lines under its name, with the cartridge's `StartResult` and the port's `BattleState`. It needs the
monsters' sprites and the game's text (`useMonsterSprites()`, `useText()`).

The watch past the battle's cells (`$7E:4000-$7F:FFFF`) is armed by a whole battle and left armed, so
starts called one after another arm it once; any other routine's call disarms it first. Arming or
disarming it walks every watched page of the machine, about half a second each way.

### A battle run to its end

`wholeBattle(scenario)` runs the battle program from its start to its end, a loop step at a time, so no
call passes the engine's guard on a bound call (1,000,000 instructions). A `BattleScenario` is a
`StartScenario`, a `PresentationModel`, what is laid at the first loop step (`FirstStepLaid`) and a frame
cap. `InitRAM` and `InitBattle` are called as
`BattleMain` calls them, then `BattleLoop` (`$C2:0019`) once per step. A step's call ends when the step
comes back round to the loop's head, where an escape answers in the loop's place, and the last one at
`TerminateBattle`'s `plp` (`$C2:00CA`), once its close (`$09`) has been made. The stack pointer found at
each end is kept in `WholeBattleResult::stackPointers`, one value when every step left the stack as it
found it.

```cpp
const trace::BattleScenario scenario{.start = start, .model = trace::measuredPresentationModel(seed)};
const trace::WholeBattleResult cartridge = oracle.wholeBattle(scenario);
const trace::PortBattle port = trace::portBattle(scenario, text, sprites);
const trace::BattleCheck check = oracle.checkBattle(scenario);   // both, compared
```

The model (`scenarios/battle/presentation_model.h`) says how the graphics program spends frames, and both
sides are driven by it. A frame is one run of the interrupt's body: the party's running (`$2F45`) is set
or cleared, the frame counter (`$0E`) and the play clock's frames (`$021E`) step, the story countdown's
flag (`$1DD1` bit 5) is set on the frame the model says it runs out, and the battle's clock (`UpdateBattleTime`, called through `$C2:0003` from inside the
answer) runs unless time is stopped. The opening (`$00`) takes the model's opening frames; as measured
(`measuredPresentationModel()`) that is 37 frames with no clock, then 36 clock calls over 38 frames. A
script (`$04`) takes 1 to 12 frames drawn from the model's seed and its place among the battle's scripts,
with time stopped on all of them when it plays a battle event other than Gau's (`$1B`) and Wrexsoul's
(`$1F`). A frame wait (`$01`) is one frame with the clock whoever asks for it (`BtlGfx_01`,
`btlgfx_main.asm:392-409`): the loop's (`:72-73`), and each of the wrap-up's 32 (`:12076-12080`), whose
port side is one script holding a `Wait` of 32 frames, each with the clock, taking no place among the
scripts and shown as nothing. The celebration (`$08`), when the fanfare plays (`$2F49` bit 1 clear,
`btlgfx_main.asm:248-250`), takes the frames its menus take to close (`_c10105`, `:229-237`; the model's
`celebrationMenuCloseFrames`, 1 as measured and 1 to 8 drawn) and the fanfare's two (`:257-258`), the
clock called on each, then a script's frames with time stopped, in its place among the scripts, for battle
event 1 (`:273`, `:38208`). On the port's side the script holding battle event 1 that a won battle's end
asks for takes the same frames. With the fanfare off the celebration takes no frames on either side. A
script holding the final battle's scroll (`$12`,
`:6185-6193`; on the port's side the script asked for after `ScrollFinalBattleBackground`) stops time as a
battle event does: the scroll's command stops it (`btlgfx_main.asm:135`) and the scroll's end starts it
again (`:22104`), so the next stage's first frame wait is the first frame with the clock. When it has
played, the oracle lays what the scroll lays for the next stage, as the opening does (`layTheMonsters()`:
the monsters standing, the monsters that can be aimed at, the pincer's sides). On the port's side a
frame is one `BattleSim::tick`: the opening's frames and all but a script's last are ticks with
`presentationIdle` false, and `timeRunning` is the frame's clock; `portBattle()` ticks on through every
frame of the battle's end and records a loop step only while the battle has no outcome. The party runs
on a frame (`PresentationModel::partyRuns()`, `BattleState::partyRunning` on the port's side) when the
model holds the run buttons from an earlier frame (`runButtonsHeldFrom`), the formation allows running
(`$2F4B` bit 0, `FormationAux::runningDisabled()`), the graphics program has not stopped the running —
from the celebration to the battle's close (`btlgfx_main.asm:245`) and from the final battle's scroll to
the next stage's first frame wait (`:137`, `:22102`) — and the frame's clock runs.
Which way each battler faces (`$2F50-$2F51`) the graphics program writes on every frame wait from its own
sprites, just before the clock (`UpdateFacingDir`, `btlgfx_main.asm:39958-39976`). `checkBattle()` runs the
port's battle first and hands its facings, one mask a frame as each frame began (`PortBattle::facings`),
to `wholeBattle()`, which writes each frame's mask before the clock is called; the state compares them at
every step. A start runs no frame, so `checkStart()` leaves them out.

`FirstStepLaid` is written into both sides as the first loop step's frame wait is reached, before that
step is compared: statuses added to battlers and battlers' HP. It brings in what no record or equipment
can bring into a battle, such as Berserk or Muddled, and cuts the monsters' HP so a drawn battle is short.
A status is laid with what its set handler leaves beside its bit (`SetStatusTbl`,
`battle_main.asm:11661-11693`):

| Status laid | What its handler leaves | Lines |
|---|---|---|
| Berserk | nothing (`SetStatusNoEffect`) | `:11673` |
| Muddled | the battler in the flipped set (`$2F53`, `BattleState::flippedByControl`) and, for a party member, in the status-changed set (`$3A4A`, `BattleState::statusChanged`) | `SetStatus_0d` `:11366-11383` |
| Morph (`FirstStepLaid::morph`, a morph under way) | laid as `UpdateMorph` leaves a member it has just morphed: Morph and Revert traded in the member's commands (`$202E`), the member as the one morphed (`$3EE2`, `BattleState::morphedBattler`), the meter and its speed of the scenario's choosing (`$3F30`, `$3F32`, `BattleState::morphCounter`, `morphCounterSpeed`) | `SetStatus_1b` `:11585-11588`, `UpdateMorph` `:1679-1756` |

`drawBattle()` (`scenarios/battle/battle_draw.h`) draws a battle with no player: a start from
`drawStartScenario()`, then each member seated Berserk or Muddled (laid at the first step), Umaro, or a
member whose menu opens and is never answered, or a colosseum match. A Berserk member holds Rage among
their commands now and then, and a Muddled one Rage or Dance, so the battle's own choosers begin a rage or
a dance for them. The party's HP is cut now and then and every monster's HP is cut. The frame model is
`drawPresentationModel()`'s, and one with no story countdown of its own is given one running out between
frames 8,000 and 12,000, so every drawn battle ends inside the frame cap; the drawn battle switches have the
field's countdown already run out (`$1DD1` bit 5, which ends a battle at its first end check) one battle in
eight. One battle in eight holds the run buttons from a drawn frame. A Muddled member picks among
`ConfusedCmdTbl`'s commands, Tools one of them, and Tools a tool the party owns (`RandCharAction`
`:656-745`, `RandTools` `:906-914`): one battle in two with a Muddled member gives the first Muddled
record not aimed at something else Tools, in a slot holding neither the Rage nor the Dance drawn for it,
and makes the Chain Saw the only tool the party owns, from a generator of its own so the rest of the draw
is what it would be without it. The Chain Saw's roll then reads the RNG table at the position plus 256 ×
the byte the start drew above it (`Rand` `:12378-12384` with the index registers wide, from
`ToolsEffect_03` `:7127`; `docs/Bugs.md`). `drawBattleScenario()` is the same draw's battle alone.

Four rows of the reach table hang on the cartridge's own roll or on a chain no odds make likely, so one
battle in ten is aimed at each of them (`BattleAim`). `drawAimedBattleScenario(rng, text, sprites, aim)`
draws a battle given what its aim needs and runs the port's battle over it (`portBattle()`); while that
battle misses the aim it draws again whole, every draw from the same generator in order, at most
`kAimTries` (128) times, and the first that reaches it is the battle. One that never does is the last
drawn, marked missed. The cartridge is then asked about the chosen battle step by step, as about any
other, so a row the port reaches and the cartridge does not is a difference.

| Aim | What the battle is given |
|---|---|
| a dancing member | the first record of the party played Muddled, holding Dance and no Rage |
| Gau turning up | every member Berserk and the monsters' HP cut to 100 at most, in a start where Gau may appear (a start of another kind is a try spent) |
| Shadow walking off | Shadow's record in the party played and standing, free to walk off at a win (`BattleStart::shadowAvailable` set, the switch that keeps him clear), every member Berserk and the monsters' HP cut to 100 at most |
| a final stage scrolled on | every member Berserk and the monsters' HP cut to 100 at most, in a start at the final battle's first stage (a start of another kind is a try spent) |
| Terra's Magic and her event (the proofs' alone) | Terra's record in the party played and standing, Muddled, holding Magic and Fire with the MP for it, and battle switch 65 set |

A character's record is brought into the party played by swapping its place with the first record played,
so each character stays in one record and a scripted setup still finds its cast. `battleReached()` names
the rows of its reach table (`battleReachRows()`) a checked battle took: the part each member took —
Berserk, Muddled, dancing, raging, Umaro, a member whose menu is never answered, the colosseum — and how it
ended — won, lost, fled, a final stage scrolled on, Gau turning up, Shadow walking off, a story countdown
running out, or an ending a battle event or the story chose — and a Chain Saw worked with the byte above
the random numbers' position not 0, whose roll read past the table. After the table the sweep prints, for each
aim, how many draws were aimed at it and how many missed; a row at zero with no misses beside it is one no
draw was aimed at.

At each loop step's frame wait (`lda #$01` at `$C2:0023` marks it) the battle's cells are read into a
`CartridgeStep`, before the frame runs, with every cell the battle program wrote since the step before
and every read it made since then of a cell nothing had written since. For the first such read of a cell
wholly on `cellsNotCompared()`, the step also keeps the stack's two pages (read once from the parked
machine as the battle starts and kept from the battle program's writes after, since a read of the
running machine answers a tick old), the trace's last line and the battle program's last 24 accesses,
which the check prints to name the reader. A read of a cell compared in part is held by its compared
bits and keeps none. A battle not ended by the frame
cap is ended by the cartridge's event 4 at its next end check and marked `timedOut`. What the game keeps
(`WriteBackResult`) is read from the cells once the battle returns.

One carried read is held by a rule of its own. `CreateAction` copies the targets being made, `$B8-$B9`,
into the new action's slot (`lda $b8` at `$C2:4F00`, then `sta $3520,y`, `:12960-12961`), and some of the
battle's own actions are made with `$B8` as a step before left it (the Gau and Shadow events, the timer's
Revert, damage over time, the party's escape, the pose reset). An escape at the copy marks its two reads:
read carried, they are kept apart (`CartridgeStep::carriedTargetCopies`) and the slot they went into is
noted (`slotsHoldingCarriedTargets`, until anything else writes its targets). At the step's comparison
such a slot must be one whose targets the state leaves out; one whose targets are compared fails the
check, naming the slot and its command. Any other carried read of `$B8` or `$B9` fails the carried-read
gate.

A second rule holds one carried read of the attack's third flags byte, `$11A4`. Magic, cast by Terra while
battle switch 65 calls for her event, queues battle event `$06` and then sets the undodgeable bit in
whatever the last record left (`lda #$20; tsb $11a4` at `$C2:175A`, `:3671-3672`); the spell's own record
replaces the byte before anything reads it (`InitCmdTarget` `:4150-4152`, `LoadMagicProp` `:6858-6868`). An
escape there marks the next read of `$11A4` as the `tsb`'s, and that read, carried or not, is passed. Any
other carried read of `$11A4` fails the gate. A battle can be run with cells written into the cartridge's
side at a step's frame wait (`BattleScenario::cartridgeWrites`), so a rule like this one is proved by two
runs with the cell set two ways, each agreeing with the port to the end.

`wholeBattleInOneCall(scenario)` runs the same battle as one call of `Battle_ext`, with the same answers,
frames and frame waits. A battle short enough for one call gives the same graphics calls, steps and end
as the stepped run, which is how the steps are proved to add up to the whole; a longer one is abandoned
at the guard.

`checkBattle(scenario)` runs both sides and compares them step by step until the first step that parts:
the frame each step is reached on, the requests made since the step before, named alike on both sides
(`requestShown()`, `callShown()`; frame waits and the items' merge have no request) and a script command
by command (below), the state
(`compareBattleState()`) with the write-set gate over the step's writes, the carried-read gate over its
carried reads against the step before's state and the copy rule above, then whether either side ran past
the frame cap, the steps' count, the end, the requests from the last step's frame wait to the close —
in order, the port's celebration shown as the cartridge's one call (`requestsShown()`), and an item a drop
or the prize adds after the wrap-up's merge shown as nothing, as the port's end obtains and merges it with
no request — and the frame the battle closed on, then what the game keeps, and the last three places of
the items obtained as the battle hands them back (`WholeBattleResult::itemsObtainedTail`, read from `$7E:606E` once the
battle closes), every byte: a record made for no item reaches the items obtained only as the colosseum's
prize, at the place its count names from zero, so it never stands in a place the battle hands on. A step's carried reads pass the rows of
`kCellsLeftAsTheMachineHasThem` not marked for the start alone (`leftAsTheMachineHasItInAStep()`). At each
step, a record a party member was seated from at that step or any before it keeps the HP and MP boosts its
equipment gives (`battle::deriveEquipment()`), as the battle's equipment refresh writes them into the
record at every stage's start (`UpdateEquip` `:2349-2362`); every other record keeps the boosts it came in
with. The same holds for what the game keeps. Each gate lays the lines and the player's records from the step's own cells, the carried-read gate
from the step before's, so a ring is laid where the cartridge's stood at that step. It needs
`useMonsterSprites()` and `useText()`.

A script is compared command by command. As the battle asks for a script to play, the cartridge's side
reads the queue from `$7E:2D6E` (four bytes a command, up to `$FF`), the params blocks from `$7E:2C6E` and
the numeral buffers from `$7E:2BCE` into the port's commands (`readScript()`,
`oracle/battle/script_read.h`), kept by the call's place in `WholeBattleResult::scripts`. Both sides' scripts
are then shown one line per queue command (`scriptShown()`, `scenarios/battle/script_shown.h`), each line the
command and every operand its handler in the graphics program reads:

```text
script: name 1 166; animation 9 3 | attacker 0 targets 2 hit 2 ...; numeral 1:1415
```

A command the port has no kind for shows by its bytes (`command $07 $00 $00 $00`), so it parts from
whatever the port's script holds there. What each line leaves out is what no handler reads:

- an animation's fourth byte, except where the attack wrote it (`fourthByteIsTheAttacks()`): a weapon's
  swing, a monster's special attack, a Dance, a Sketch that hit. Every other animation carries the byte
  the last of those left;
- the attack byte of the animations that read only their block (Runic's, the party running away), and the
  attack and the block of those that read nothing (Mimic's, Defend's, the two AI commands', the roulette
  cursor's, a command with its top bit set);
- a monster animation's monsters for the sound effect, which reads a pan and a sound, and the monsters
  hidden by an entrance that revives some;
- a message's numbers, except those its text shows (`numbersShown` in `script_shown.cpp`, as the port's
  text reads its codes): a number code shows the whole number, a name code a name from its low byte.

Tools and Blitz show an index where the port keeps what it names: a tool's index among the tools'
animations, from the animation's item, or 8 for the Chain Saw's kill; a blitz's among the blitzes', the
attack less Pummel's. A damage numeral shown one at a time is a queue command each, so a port command
showing several in turn is a line for each.

`tests/oracle/test_battle_whole_oracle.cpp` checks the measured opening against the seven recorded fights'
counts, that a script's frames come from its place and number 1 to 12, that the first fight called a step
at a time adds up to the same fight called once, and the first fight in Narshe, from its written scenario
with the measured opening, run to its close with `checkBattle()` showing no difference: 7,012 loop steps,
then the end's frames. It also runs the sweep's first drawn battle handed a tail of the items obtained no
item's record could fill, and checks that both sides hand back every byte of it. The first of the sweep's
drawn battles that the port wins away from the colosseum and that a meter of 1 to 255 time updates lasts
to the wrap-up's wait in, laid with a morph under way whose meter runs out at the wait's first time update
(the least meter that lasts to the wait, found on the port's side), agrees to its close and leaves the
kept Morph supply at 0 on both sides; the test records the draw's number and the meter. Four tests prove a rule the carried-read gate passes a cell by:
each runs a battle twice with the cell written two ways at a step's frame wait
(`BattleScenario::cartridgeWrites`), expecting `checkBattle()` to show no difference both times —
`$11A4` and `$00B7` on a battle aimed at Terra's Magic and her event, and `$B8-$B9` on one aimed at Gau
turning up, each at the step before the battle event that matters is queued
(`drawAimedBattleScenario()`); `$B8-$B9` again on the sweep's first drawn battle the port wins away from
the colosseum, laid with a morph whose meter runs out four time updates in, at the step before the step that holds no morph, the meter's Revert made and run
between them.

Five more prove what an empty party slot's refresh at a later final-battle stage writes reaches nothing
(the empty slot, below). Each takes the sweep's battles aimed at a final stage scrolled on whose next stage
seats nobody in a slot, and writes cells 0 and then 255 for each such battle. Four take only the battles
whose first step's refresh ends on a seated member. Three of them write at the stage's second step: the
slot's stat, equipment and immunity cells with the Genji Glove byte, its command rows' verdicts, and the
two boost bytes in bank `$7F`. The fourth writes at the stage's first step, before the refresh reads
them: the slot's hands, command rows, `$3A21,x` and `$3330-$3331,x`. The fifth takes every battle whose first
step's refresh takes an empty slot, wherever it ends, and writes the whole block `$11A0-$11DE` at the
stage's second step. Where a seated member was refreshed last, it writes the two cells the comparison
holds as the port holds them. Each run agrees with the port to the battle's end.

`AChainSawsRollWithTheByteAboveThePositionAtOneReadsPastTheTable` takes draw 12 of the sweep's, whose Chain
Saw kills otherwise with the byte above the random numbers' position at 1 than at 0, starts it with the
byte at 1, and checks it agrees with the port to its end.

Two more take an empty party slot's hands, which the start reads from bank `$7F` (`docs/Bugs.md`).
`AColosseumWinForNoPrizeWithNothingHeldLeavesTheEmptySlotsTargetingOut` takes the first colosseum match of
the sweep's draws that the port wins once the fighter's hands and the inventory are emptied and the prize
taken away, checks it agrees whole with `$2E74` left out, and records the draw, the two bytes the parked
machine holds at `$7F:161E-$7F:161F` and the targeting the cartridge's first step holds at `$2E74`. Laid
with `$2E74` compared at every step, it parts at the first: the port's `$FF` against the targeting of the
item the two bytes name.
`AnEmptySlotsHandRecordsReachNothing` takes the first drawn battle away from the final battle that seats
nobody in a slot and writes the slot's two hand records 0 and then 255 at the first step: both runs agree
with the port to the battle's end.

`tests/oracle/test_battle_scripts_oracle.cpp` checks the script reader on queues laid by hand: each
command the battle program writes reads as the port's command its handler shows, the params blocks go to
the commands that play them (an animation and a bounce take one each, the pointer step passes one by, the
ready stance reads the next and leaves it), and each numerals-at-once command takes the next buffer. It
checks that an animation's fourth byte shows only where the attack wrote it and a message's numbers only
where its text shows them. Those five run in every run of the tests. The sixth runs the sweep's first
ten drawn battles whole and checks every script they play reads as the port's commands, none by its
bytes, one for each call that plays a script; it records how many scripts and lines of each kind it read.

Eight cases each take the first of the sweep's draws whose port battle reaches what they are about
(`theDraw()` names it by its number) and read, at the step it happens, what the listing holds there on the
cartridge's cells and in the port's state, the battle agreeing whole: a monster's turn queued as its AI's
turn (`$3AA0,x` bit 5, command `$1F`), a counterattack's flag at `$B1` bit 0, a Berserk member's actions
among Fight, Capture, Rage, Jump and Magitek, a condemned count leaving 2 and Doom shown after it, the
member who escaped at `$3A38` in a fled battle, the countdown's flag in battle switch byte `$3EBC` as a
timed-out battle closes, Gau's join mask, enemy mask, `$3A46` bit 2 and event `$1B` as he turns up, and
the next final-battle stage's formation and scroll.

`AStateWithItsListOutOfOrderWrittenIntoAMachineReadsBackTheSame` takes the first step of the sweep's
drawn battles whose cartridge list of actions is not the block from slot 127 down, writes the port's state
there into a fresh machine (`writeBattleState()`), and checks that the machine's list is that block and
that its cells read back as the state written.

`DrawnBattlesAreThePorts` checks 100 battles of the sweep's draws (`drawBattleScenario()` from
`kSweepSeed`) with `checkBattle()`, expecting no difference in any, and every row of the reach table
taken by at least one.

### The whole state compared

`compareBattleState(state, boosts, cells)` lays a `battle::BattleState` into the battle's cells and
compares them with a cartridge's, cell by cell, one line per cell where they part. `writeBattleState()`
does the laying, through the writers every routine's scenario is written by — the attack frame's, the
AI's, the scripts' and the ending's — with what those writers also lay of an attack, a command, a
script or a message left out: a writer lays those through `writeInFlightByte()` and its kin, and only
`writeByte()` and its kin lay the state. A last pass, `writeStateAsTheCartridgeHoldsIt()`, lays the
cells where the cartridge holds the state its own way — an empty inventory record or hand, which keeps
the targeting of the record loaded before it, the last record's targeting at `$2E74`
(`BattleInventory::lastRecordTargeting`, left out while `lastRecordsTargetingUnknown()` holds: the start
read an empty party slot's hands and made no record for an item after them), an empty party slot's record offset, the formation's first
byte with its top four bits turned over, the battle's flags at `$B1` (bit 1 the party cannot run, bit 2 a
monster cannot be run from), the field's flags at `$11E4`, each battler's flags at `$3AA0`, `$3AA1`,
`$3205` and `$3E4C` whole, the reach cells, the windows' wallpaper (`$2F34`, which a status change's
carry out of the fourth equipment-change flag reaches), the byte above the random numbers' position (`$00BF`,
the scenario's, which no step writes, while `checkStart()` or `checkBattle()` compares), each seated member's record with the HP and MP boosts its
equipment gives in the top bits of its maximums, and the final battle's line with each place a stage
took from holding `$FF` — and leaves out the cells no field decides: the hands and control menus of a
slot with none, what a battler was last hit with before it has been hit, the places of a line's ring
outside the line. Every battler's special attack (`$322D,x`) and the monster it is named for
(`$33A8-$33A9,x`, `$FFFF` for a battler with no monster and no rage, read by `ShowAttackName_06`
`:8745-8747`) are laid and compared. It also lays the party played
(`$1A6D`) and each record's party byte (`$1850-$185F`), which Gau's joining reads and writes. A cell two
writers lay differently at a bit with meaning is a line of its own.

The same pass lays the cells no routine's scenario writes, in four parts:

- `layMenusAsTheCartridgeHoldsIt()` — each party slot's four commands, its spell list (the esper, the
  spells by their places, the lores), the party's own list and every attack's place in it, the espers
  summoned and the Genji Gloves worn, and the SwdTechs, Blitzes, lores, rages and dances known.
- `layBattlersAsTheCartridgeHoldsIt()` — each battler's ATB gauge and rate, advance wait and its
  counter, status timers, damage-over-time beat and poison multiplier, Morph gauge and condemned count,
  speed, equipment statuses and relics, run progress, experience, dance and the name it is listed under.
- `layBattleAsTheCartridgeHoldsIt()` — the flags at `$B0`, the last attack's level (`$11AF`, which an
  attack's setup writes and a party member's equipment refresh leaves as the shield hand's hit rate) and
  whether it was physical (`$11A2` bit 0), the settings, Roulette's
  choice, the run buttons, the sets of battlers the battle keeps between steps, the battlers acting and
  counterattacking, the three lines, the player's records, Mimic's memory, the tools owned and the
  end-of-battle events.
- `layActionsAsTheCartridgeHoldsIt()` — the battle's one list of actions, 128 slots: the interrupting
  actions and each battler's turn actions and counterattacks, each a chain of slots from its first. In
  a comparison each list takes the slots the cartridge's own list of it holds, and a list longer than
  the cartridge's takes the highest slots no list uses; written into a machine, the lists take the
  slots from 127 down, the interrupting actions first, as a start hands them out. A command's slot holds
  its command, attack, targets and cost; a pose reset, a battle message, a spell with no caster, a
  monster's exit or entrance and Scan hold what the routine that made them stored, their targets left
  out where that routine stored whatever `$B8` held — a Revert among a battler's counterattacks, the one
  a morph running out makes, among them — and the high byte left out where the routine stored the low
  byte alone. Those targets reach nothing once the action runs, so a read of them is passed by the
  carried-read gate (`LaidState::carriedToNothing`): the reasoning, command by command, heads
  `oracle/battle/state_actions.cpp`.

A line and the player's records are rings, laid from where the machine's ring starts: in a
comparison, the cartridge's. The slots from there hold the battlers, or the records, in order, and the
start itself is held by what is laid from it (`LaidState::positions`). A battler taken out of a line
leaves its place holding `$FF` until the line's front passes it, and the loop passes over such a place
(`RemoveAllActions` `:1583-1591`; `BattleLoop` `:127-128`, `:141-142`); the port's line drops the
battler at once. In a comparison the cartridge's `$FF` places before its end are laid as it holds them,
the line's battlers go in the other places in order, and the end is where the last of them falls, so a
battler one side kept and the other dropped still parts the two. A party member seated with no
record has its record offset laid as `$FFFF` and everything the start loads through that offset left
out (`docs/contracts/battle_core.md`, the start's seating).

An empty party slot at a final-battle stage after the first keeps the start's `$FF` as its
equipment-change flag, so the stage's first loop step refreshes it, summing the bytes below the first
character record (`docs/Bugs.md`). The port does not run that refresh. At such a stage the state laid
leaves out what the refresh writes for the slot: its stat, equipment and immunity cells, the Genji Glove
byte and bit 2 of its `$3AA1,x`, instant death's protection. The cells the refresh reads and writes back, its hands and its command
rows are held as carried to nothing (`leaveOutAsCarriedToNothing()`), and so is its relic effects 4,
`$3C58,x`, which the cover check reads for every battler and acts on only for one present. When an empty
slot is the last the refresh takes, its sums stand as the last attack's level (`$11AF`) and its physical
bit (`$11A2` bit 0), where the port keeps the refresh before's. `checkBattle()` leaves the two out
(`stepsWithTheLastAttackFromAnEmptySlot()`) from the step whose window holds that refresh to the step
before the one in whose window the battle program writes the level again, by a refresh or an action. A
read of either before that write fails the carried-read gate.

A cell is compared by the bits a field decides (`cellMeanings()`, `meaningBits()`): `$3205,x` by its
bits 7, 2 and 1, a battler mask by the bits that name a battler, a command row's and a spell entry's
second byte by its bit 7, an item record's fifth byte by its four party slots' bits, `$11A2` by its bit 0
(the last attack physical), `$1EDE` by its bit 3 (Shadow available, `BattleState::shadowAvailable`, which
a win's end check tests and his leaving clears), `$2F49` without
bit 7 and `$2F4A` not at all — the scripted setup in force, which the start lays for its own opening. A
state can leave bits of a cell out for itself (`LaidState::bitsLeftOut`, `comparedBits()`): with no battler
acting, the acting battler's byte (`$3406`) is compared by bit 7 alone, the other bits being the rotate's
leavings (`:195-196`), and the counterattacking battler's (`$3407`) the same. Every other bit of a cell the battle program writes must be on
`cellsNotCompared()` with its reason, which the whole battle's two gates hold to.

Two gates keep the comparison whole. `uncoveredWrites(state, boosts, StartResult::battleWrites)` names
every cell the battle program wrote up to the loop's first frame wait that the comparison does not cover
in full. `carriedReads(state, boosts, StartResult::readsAfterThePoint)` names every read the battle
program made after that wait of a cell the comparison does not hold and nothing had written since. A
cell compared in part is held by its compared bits, and its row on `cellsNotCompared()` names every
reader of the rest; a ring's start is held; the stack — `$7E:1500-15FF` for a battle the field calls,
`$7E:0100-01FF` for one called from the machine's reset — is never carried, since a return reads what
its own call pushed; and a cell left as the machine has it is passed, as the read-set gate passes it —
the field's record of the battles Gau can find on the Veldt among them, which the battle's end reads
only to set the formation's bit in it. A watch is handed the cell and the byte, not the instruction, so
a gate's line names those two. `StartResult::readsBeyondTheBattlesCells` holds every read of
`$7E:4000-$7F:FFFF` not listed — an empty party slot's hands, read through its record offset `$FFFF`
from bank `$7F`, are listed. `portStart(scenario, text, sprites)` is the port's start over the same
scenario.

`tests/oracle/test_battle_start_oracle.cpp` checks that the battle program's entry is where the US cartridges
hold it and that an image without it is refused; calls it from the first fight as `first-fight.inputs`
leaves the machine and from that fight's scenario written into a machine fresh from its reset code, each
opening, reaching the loop and returning; checks that a scenario written reads back the same; compares
the first fight's start with the port's, from the machine and from its written scenario, and a scripted
setup's start that keeps monster slot 0 out of reach, and the same setup's start with no record for
the character it seats; checks that the start reads nothing it was not handed but the listed cells;
checks each gate on cells whose answer is known; and checks both gates empty over the first fight's
start. From the first fight's party it then checks, each against what the original says of the case
and with `checkStart()`, an ordinary front attack with no opening message, a preemptive strike, a back
attack, a pincer, a colosseum match, the final battle's first stage, a formation chosen from four and a
party with a slot left empty; then every formation once, the colosseum's with a match and the final
battle's with the party in line, each scripted setup's cast given a record (`giveTheCastRecords()`);
and 1,300 drawn starts (`drawStartScenario()`, the `start` sweep's draws) with every row of the reach
table taken.

## Files

The tool is `inputs/`, the input scripts, and `src/`, the sources, laid out by concern. Where a concern
has a port side, a cartridge side and cells, the three sit at the same path under `scenarios/`, `oracle/`
and `cells/`. `src/` is the include root: a file names another from there (`#include
"oracle/damage_oracle.h"`). The build makes two libraries of it, `ostinato-battle-scenarios` (`scenarios/`
and the trace's parser, with no machine in any file) and `ostinato-damage-oracle` (`oracle/`, the trace
log and `machine/`), and the tool and the test binaries link them.

A file's `_shared.h` declares the helpers it defines that files of other concerns use, in namespace
`detail`; the cells headers hold `$7E:xxxx` addresses in namespace `cells`, and `oracle/routines.h` the
routines' addresses in namespace `routines`.

**`inputs/`**

| File | What it holds |
|---|---|
| `first-fight.inputs` | the buttons from power-on to the first fight in Narshe |
| `press-a.inputs` | A pressed for 4 frames in every 16, which fights the first fight to a win from its state |
| `hold-then-press-a.inputs` | nothing pressed for 1,200 frames, then A for 4 frames in every 16: the guards act first, then the party fights the first fight to a win |
| `first-two-battles.inputs` | from power-on through the first fight, won as `press-a.inputs` wins it, and on through the town to the next battle |

**`src/tool/`, the command line**

| File | What it holds |
|---|---|
| `main.cpp` | the command line read, and the mode it names run |
| `starting_state.{h,cpp}` | the cartridge image read from a file, the state a run carries on from, and the trace in a window |
| `pictures.{h,cpp}` | a picture written as a PPM file, and the scripted run to a battle |
| `cell_log.{h,cpp}` | the cells a `--watch` logs, and `CellLog`, which logs them to `cells.log` |
| `seam_log.{h,cpp}` | the run with no window, `SeamLog` (what the graphics program hands the battle program, to `seam.log`), and the state check |
| `check.{h,cpp}` | the `--check` sweeps and the `--replay` of a recorded trace |

**`src/machine/`**

| File | What it holds |
|---|---|
| `machine_state.{h,cpp}` | `MachineState`, `writeMachineState()`, `readMachineState()`, `stateIsOf()` and `cartridgeName()`, a state's file; `InputStep`, `readInputScript()` and `buttonsAt()`, an input script; `CellWrite`, `readCellWrite()`, `declareWorkRamBytes()` and `writeCells()`, the bytes `--write-at` writes |

**`src/trace/`**

| File | What it holds |
|---|---|
| `trace_log.{h,cpp}` | the routines the trace follows, the ranges it watches, and `DamageTrace`, the trace's escapes, watches and lines, on any machine |
| `trace_parse.{h,cpp}` | `parseTrace()`, `parseEndings()` and `messageShownBy()`: a trace read back as calls, endings and the messages shown |
| `cells.h` | the cells the trace and its parsers both read |

**`src/oracle/`, the cartridge's side**

| File | What it holds |
|---|---|
| `damage_oracle.h` | `DamageOracle`, `TickClock` and `runOnLoop()`, with every concern's scenarios and results: what a test includes |
| `routines.h` | each routine's address, the calls that return to it, its opening bytes and the frame it is called in |
| `machine.cpp` | the oracle hosting the cartridge, starting it, calling a routine, the read-set gate and the graphics program's answers; `runOnLoop()` |
| `cell_access.cpp` | the oracle's reads and writes of cells, `unwrittenReads()` and the accesses past the battle's own cells |
| `replay.cpp`, `replay_shared.h` | `replay()`: a recorded call read back and replayed against the port |
| `damage/damage.cpp` | the formulas, the damage to one target, the damage taken and shown, and a spell bounced off Reflect |
| `damage/cover.cpp` | the cover and the attack's animation set up |
| `damage/attack.cpp` | the whole attack frame, `attack()` and `selfAttack()` |
| `status/status.cpp` | the statuses taking effect and an attack's statuses on one target, with their replay |
| `status/attacker_effects.cpp`, `attacker_effects_shared.h` | an attack's special effect on its attacker; `paramsFromBytes()` and `frameParamsFromBytes()`, a params block read back |
| `status/target_effects.cpp` | an attack's special effect on one target |
| `status/effects_replay.cpp` | a recorded special effect rebuilt and replayed |
| `ending/records.cpp`, `records_shared.h` | over a `RecordScenario`: the esper's spells learned and the level-up, with their replay |
| `ending/rewards.cpp` | the steps of a win's rewards, the graphics commands they call, and their replay |
| `ending/ending.cpp` | over an `EndingScenario`: `wrapUp()`, `win()`, `loss()` and `writeBack()`; over an `ItemsObtainedScenario`: the graphics program's two commands for the items obtained; `itemsObtainedTailAtPowerOn()`; `replayEnding()` and `readEndingScenario()` |
| `ai/ai_targets.cpp` | over an `AiScenario`: the battlers a monster's script names and the conditions it tests |
| `ai/ai_run.cpp` | a script readied, its turns and its counterattacks |
| `ai/ai_commands.cpp` | the three commands only a script queues |
| `ai/own_actions.cpp` | the battle's own actions, `ownAction()` |
| `ai/ai_replay.cpp` | `readAiScenario()`: a battler's AI rebuilt from a recorded call, and replayed |
| `battle/start.cpp` | the battle program called whole, `battleStart()`, and a start scenario written and read back |
| `battle/start_check.cpp` | a start checked whole, `checkStart()`, and the text and sprites it reads |
| `battle/battle_state.cpp` | the whole state compared: `writeBattleState()`, `compareBattleState()` and the two gates `uncoveredWrites()` and `carriedReads()` |
| `battle/state_actions.cpp` | the battle's one list of actions, laid as the cartridge holds it |
| `battle/state_menus.cpp` | the party's command menus, spell lists and skills, laid as the cartridge holds them |
| `battle/state_battlers.cpp` | each battler's clocks, counters and own cells, laid as the cartridge holds them |
| `battle/state_battle.cpp` | the battle's flags, settings, lines, records and Mimic's memory, laid as the cartridge holds them |
| `battle/whole_battle.cpp` | a battle run to its end a loop step at a time, `wholeBattle()`, or as one call, `wholeBattleInOneCall()`; the frames each graphics command takes, and the first step's laid cells |
| `battle/battle_check.cpp` | a whole battle checked step by step, `checkBattle()` |
| `battle/script_read.{h,cpp}`, `script_read_shared.h` | `ScriptBytes` and `readScript()`: a battle script's queue, params blocks and numeral buffers read into the port's commands, walked command by command |
| `battle/script_read_text.cpp` | the names in the window at the top and the battle messages |
| `battle/script_read_attack.cpp` | an animation, a bounce off Reflect, a Launcher's hits, and the damage numerals |
| `battle/script_read_party.cpp` | the ready stance, the step back and a pose reset |
| `battle/script_read_monsters.cpp` | a battle event, a monster's dialog, a formation change, monsters entering or leaving, and a monster animation |

**`src/scenarios/`, the port's side**

| File | What it holds |
|---|---|
| `common/bytes.{h,cpp}` | the port's values as the bytes the cartridge keeps them in |
| `common/report_text.{h,cpp}` | the text a report shows a value as |
| `common/draws.{h,cpp}` | the random draws every sweep makes its scenarios from |
| `common/graphics_commands.h` | the graphics commands the battle program asks for, by number |
| `damage/damage.{h,cpp}`, `damage_shared.h` | `DamageScenario` and the damage results; `portTargetDamage()`, `portApplyDamage()`, `portReflectedAttack()`; `drawScenario()`, `drawTargetScenario()`, `drawApplyScenario()`, `drawReflectScenario()` |
| `damage/cover.{h,cpp}` | `portInitParams()` and `portCoverTarget()`; `drawCoverScenario()` |
| `damage/attack.{h,cpp}`, `attack_shared.h` | `portAttack()`, `portSelfAttack()` and `attackResult()`; `drawAttackScenario()` and `drawSelfAttackScenario()` |
| `status/status.{h,cpp}`, `status_shared.h` | `portStatusChanges()` and `portStatusEffect()`; `drawStatusChangesScenario()` and `drawStatusEffectScenario()` |
| `status/attacker_effects.{h,cpp}`, `attacker_effects_shared.h` | `runAttackerEffect()`; `drawAttackerEffectScenario()` |
| `status/target_effects.{h,cpp}`, `target_effects_shared.h` | `runTargetEffect()`; `drawTargetEffectScenario()` |
| `ending/records.{h,cpp}`, `records_shared.h` | `RecordScenario`; `portLevelUp()` and `portLearnFromEsper()`; `drawLevelUpScenario()` and `drawLearningScenario()` |
| `ending/rewards.{h,cpp}`, `rewards_shared.h` | the graphics program's calls and the messages they show, `PartyCells`, `RewardScenario`; the port's reward steps; the reward sweeps' draws |
| `ending/ending.{h,cpp}` | `EndingScenario` and `ItemsObtainedScenario`; the port's endings, write-back and items obtained; `drawEndingScenario()`, `drawWinScenario()` and `drawItemsObtainedScenario()` |
| `ai/ai_targets.{h,cpp}`, `ai_targets_shared.h` | `AiScenario`; `portAiTargets()` and `portAiCondition()`; `drawAiScenario()`, `drawAiTargetsCase()` and `drawAiConditionCase()` |
| `ai/ai_run.{h,cpp}`, `ai_run_shared.h` | a script as the cartridge keeps it; `portAiTurn()` and `portAiCounter()`; `drawAiRunScenario()` |
| `ai/ai_commands.{h,cpp}` | `portAiCommand()`; `drawAiCommandCase()` |
| `ai/own_actions.{h,cpp}`, `own_actions_shared.h` | `portOwnAction()`; `drawOwnActionCase()` |
| `battle/start.{h,cpp}` | `StartScenario`, `StartResult` and `StartCheck`; `portStart()` |
| `battle/start_draw.{h,cpp}` | `drawStartScenario()`, `giveTheCastRecords()`, and the reach table: `startReachRows()` and `startReached()` |
| `battle/battle_draw.{h,cpp}` | `drawBattle()`, `drawBattleScenario()`, the aimed draws (`BattleAim`, `drawAimedBattleScenario()`), and the reach table: `battleReachRows()` and `battleReached()` |
| `battle/presentation_model.{h,cpp}` | `PresentationModel` and `Frame`: the opening's and each script's frames, and the party's running; `measuredPresentationModel()` and `drawPresentationModel()` |
| `battle/whole_battle.{h,cpp}` | `BattleScenario`, `FirstStepLaid`, `CartridgeStep`, `WholeBattleResult`, `PortBattle` and `BattleCheck`; `portBattle()`, `layFirstStep()`, `scriptStopsTime()`, `requestShown()` and `callShown()` |
| `battle/script_shown.{h,cpp}` | `ScriptCommandRead`; `scriptShown()`, a script one line per queue command as both sides show it, and the fourth byte's rule, `fourthByteIsTheAttacks()` and `fourthByte()` |
| `battle/battle_cells.{h,cpp}` | the whole-state comparison's tables: `cellMeanings()`, and the cells left as the machine has them |
| `battle/cells_not_compared.cpp` | `cellsNotCompared()`, every cell or bit the battle program writes that the comparison does not cover, with its reason; `onTheStack()` |

Every result has a `differences()` beside it, which compares two of them cell by cell, and every
drawn scenario a `describe()`.

**`src/cells/`**

| File | What it holds |
|---|---|
| `damage/per_target.h` | the cells of the per-target pass, the damage taken and shown, and the reflected attack |
| `damage/frame.h` | the attack frame's own cells |
| `status/status.h` | the statuses' cells and bits |
| `status/effects.h` | the special effects' cells |
| `ending/records.h` | the character records and the spells known |
| `ending/party.h` | the rewards' cells besides the records |
| `ending/ending.h` | a battle's end's cells |
| `ai/ai.h` | a battler's AI's cells |
| `battle/state.h` | the battle's state cells no routine's scenario writes: its flags, lines and records, each battler's clocks, the party's menus, spell lists and skills, Mimic's memory |

The tests that run the oracle, and how a run of the tests chooses how much of the cartridge to ask, are
in § Checking, § The tests that ask the cartridge.
