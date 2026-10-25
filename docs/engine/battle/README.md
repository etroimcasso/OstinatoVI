# Engine — Battle

Guide for working with battles: how turns come round, how actions wait and run, and how a battle
ends. Read this if you want to drive a battle from a host, plug in what a command does, or change
the battle's timing.

## What this area is

The battle core is logic only. It holds the state of a battle in progress — the ten battlers, their
ATB gauges and status timers, the lines they wait in, what each has chosen, the party's battle stats,
commands, spells and items — and advances it on two
clocks: battle time, once per displayed frame, and the battle loop, one step per frame while no
battle script is playing. It includes no rendering or audio type. What it needs shown it records as
requests (`BattleRequest`) for the presentation to act on, and what each command does, how actions
are aimed and how an AI chooses it hands to an `ActionExecutor`.

Everything lives in `src/battle/`, namespace `ostinato::battle`, except the party's records, which
the rest of the game shares: `src/game/`, namespace `ostinato::game`.

## Index

| File | Covers |
|---|---|
| [core.md](core.md) | The battle state and the two clocks; driving them from a host; battle slots and sets; the ATB gauge, the ATB rate, the advance wait and the status timers; the lines and adding actions; a battler's turn, counterattacks and interrupting actions; what follows every command; the battle's end; the executor seam and the requests; the random numbers. |
| [start.md](start.md) | The party side of a battle's start: the party's records; placing the party (the current party, a colosseum match, a final-battle stage, scripted setups); equipment into battle stats; HP, MP and statuses; battle commands; spell lists and MP costs; status immunity; the battle inventory and known skills; changing equipment during a battle. |

The rules, and the line of the original each is read from, are specified in
[`docs/contracts/battle_core.md`](../../contracts/battle_core.md).
