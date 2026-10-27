# Engine — Battle

Guide for working with battles: how turns come round, how actions wait and run, and how a battle
ends. Read this if you want to drive a battle from a host, plug in what a command does, or change
the battle's timing.

## What this area is

The battle core is logic only. It holds the state of a battle in progress — the ten battlers, their
ATB gauges and status timers, the lines they wait in, what each has chosen, the party's battle stats,
commands, spells and items, the monsters' — builds it from a formation and the party, and advances
it on two
clocks: battle time, once per displayed frame, and the battle loop, one step per frame while no
battle script is playing. It includes no rendering or audio type. What it needs shown it records as
requests (`BattleRequest`) for the presentation to act on, and what each command does, how actions
are aimed and how an AI chooses it hands to an `ActionExecutor`. `BattleSim` runs a whole battle,
start to finish, from a host's frame tick.

Everything lives in `src/battle/`, namespace `ostinato::battle`, except the party's records, which
the rest of the game shares: `src/game/`, namespace `ostinato::game`.

## Index

| File | Covers |
|---|---|
| [core.md](core.md) | The battle state and the two clocks; driving a battle with `BattleSim` or by hand; battle slots and sets; the ATB gauge, the ATB rate, the advance wait and the status timers; the lines and adding actions; the player's commands; a battler's turn, counterattacks and interrupting actions; what follows every command; the battle's end; the executor seam and the requests; the random numbers. |
| [commands.md](commands.md) | Carrying commands out: `CommandExecutor`; the attack in flight and its special effect; the attack loop — fizzling, MP, Runic, Air Anchor, the critical hit, a weapon's spell, lores; the hit check; what a hit target records; `AttackHooks`, where damage, statuses and effects plug in; the battle script and its builder; Fight. |
| [targeting.md](targeting.md) | Aiming: the targeting byte and a command's aiming byte; loading a command's attack; who an attack reaches — the reach check, re-aiming at a side, the pincer and side rules; targets off the field; cover. |
| [layout.md](layout.md) | Where battlers stand and which way they face: the parts of the field by battle type; the pincer's two sides; laying a battle out; turning to face a target, and the animations that don't. |
| [start.md](start.md) | A battle's start and what the game keeps: `startBattle()` and `BattleStart`; the party's records; placing the party (the current party, a colosseum match, a final-battle stage, scripted setups); equipment into battle stats; HP, MP and statuses; battle commands; spell lists and MP costs; status immunity; the battle inventory and known skills; the formation; the monsters; the battle type; the starting gauges; the opening; the final battle's stages; `writeBack()` and `BattleResult`; the battle switches; changing equipment during a battle. |

The rules, and the line of the original each is read from, are specified in
[`docs/contracts/battle_core.md`](../../contracts/battle_core.md) and, for carrying commands out,
[`docs/contracts/battle_commands.md`](../../contracts/battle_commands.md).
