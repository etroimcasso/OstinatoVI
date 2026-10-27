# Starting a battle, and what the game keeps

Before a battle's first frame it is built from a formation and the game's records: which formation
is fought, who stands in each of the four party slots, each member's battle stats with their
equipment, their HP, MP and statuses, their battle command menu and spell list, the battle
inventory, what the party knows, every monster from its record, the battle type, where each gauge
starts and the opening message. When the battle is over, `writeBack()` returns what the game keeps.
Everything works on `BattleState::party`, the battle's own copy of the game's party, and
`BattleState::switches`, its copy of the battle switches.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_core.md`](../../contracts/battle_core.md) § "The party at the battle's start",
"A battle's start", "What the game keeps" and "The final battle's stages".

## Files

| File | Holds |
|---|---|
| `src/battle/battle_start.h` | `startBattle()`, `writeBack()`, `startNextFinalBattleStage()`, the start's steps (`chooseFormation()`, `loadMonster()`, `loadMonsterTraits()`, `chooseBattleType()`, `applyBattleType()`, `fillStartingGauges()`), `BattleStart`, `ColosseumMatch`, `BattleResult` |
| `src/game/battle_switches.h` | `game::BattleSwitches` and the switch numbers the battle uses |
| `src/game/character_record.h` | `game::CharacterRecord`, `game::Party`, `PartyMembership`, `SpellProgress`, `InventorySlot`, `PlayTime` |
| `src/battle/party_setup.h` | `placeParty()`, `loadParty()`, `loadPartyMember()`, `buildCommandList()`, `deriveEquipment()`, `applyDerivedStats()`, `hitCheckFactor()`, `applyDeathAndPoisonImmunity()`, `refreshStatusImmunity()`, `applyEquipmentChanges()`, `DerivedStats`, `MaxBoost`, `PartyLineup` |
| `src/battle/spell_lists.h` | `SpellList`, `SpellListEntry`, `buildSpellLists()`, `loadSpellList()`, `refreshCastableSpells()` |
| `src/battle/battle_inventory.h` | `BattleInventory`, `BattleItem`, `ToolSet`, `battleItem()`, `loadBattleInventory()`, `loadKnownSkills()` |
| `src/battle/battle_state.h` | the battler's party-member fields: `Hand`, `MenuCommand`, `BlockGraphicSet` |

## Starting a battle

```cpp
#include "battle/battle_start.h"

using namespace ostinato;
using namespace ostinato::battle;

BattleStart start;
start.formation = FormationRef::of(FormationId::LOBO);
start.background = BattleBackgroundId::FIELD_WOB;
start.config = playerSettings;
start.playTimeFrames = playClock.frames;   // 0-59

BattleState state = startBattle(start, party, switches, cartridge.text, cartridge.monsterSprites,
                                executor);
// ... run it (core.md) until stepBattle() returns an outcome ...
const BattleResult result = writeBack(state);
party = result.party;
switches = result.switches;
if (result.veldtFormation) { /* Gau can find this formation on the Veldt now */ }
```

`BattleStart` holds what the field sets for a battle:

| Member | Is |
|---|---|
| `formation` | the formation asked for, with its randomize flag (`FormationRef`) |
| `background` | the battle background |
| `gauMayAppear`, `onTheVeldt`, `keepFieldMusic` | Gau may come back after this Veldt battle; Leap works; the field's music plays on |
| `gauRecruited`, `shadowAvailable` | whether Gau and Shadow are among the characters the party can field now |
| `colosseum` | a `ColosseumMatch` — wager, prize, opponent, fighter — for the colosseum's formations |
| `finalBattleOrder` | the final battle: the character records in the order the player lined them up |
| `config` | the player's battle settings (`BattleConfig`) |
| `version` | the cartridge's `GameVersion` |
| `playTimeFrames` | the frames part of the play clock; the battle's random numbers start at four times it |

The text is the game's (`TextCorpus`, from the cartridge): the monsters' names decide which share a
line in the list of names. The monsters' sprites (`MonsterSprites`, from the cartridge) decide where
each monster stands ([layout.md](layout.md)). The story's rules — only magic, a permanent morph,
Morph available, Gau having joined before, Shadow kept — come from the battle switches.

`startBattle()` runs, in order: the formation; the party placed and loaded (below), the battle
inventory and known skills; the monsters; the statuses waiting to be set
(`executor.applyStatusChanges`); the after-command checks once; the battle type; status immunity; the
starting gauges; the opening message; whether the party can run; everyone placed on the field and
facing the way the battle type starts them. It leaves `OpenBattleScene` on `state.requests`.

The run check comes before the battlers are placed, so at that moment a pincer reads every monster
as standing on both sides: a pincer always opens unrunnable ([`docs/Bugs.md`](../../Bugs.md)).

## The party's records

`game::Party` is the party as the game keeps it between battles:

| Member | Holds |
|---|---|
| `characters` | the sixteen `CharacterRecord`s |
| `membership` | per record: its party (0 for none, 1-3), its slot in that party's line-up, its row |
| `currentParty` | the party the player controls |
| `spells` | per character of the first twelve (indexed by actor): `SpellProgress`, a percentage per spell or `kSpellLearned` |
| `loresKnown`, `dancesKnown`, `ragesKnown` | `std::bitset`s, bit *n* for lore *n*, `DanceId` *n*, the rage of `MonsterId` *n* |
| `swdtechsKnown`, `blitzesKnown` | `AbilityLearnedSet`s |
| `morphSupply` | the Morph meter |
| `playTimeAtLastSave` | the play time the game was last saved at |
| `inventory` | 256 `InventorySlot`s |

A `CharacterRecord` holds the character's actor (whose battle properties they use), sprite, name,
level, HP, MP, experience, lasting statuses, four commands, vigor, speed, stamina, magic power,
esper and six equipment slots. `maxHp` and `maxMp` are the character's own: a Muscle Belt or a
Crystal Orb raises them in battle, not in the record.

## The party, step by step

`startBattle()` builds the party side with these, which can also be called on their own:

```cpp
#include "battle/battle_inventory.h"
#include "battle/party_setup.h"

using namespace ostinato;
using namespace ostinato::battle;

BattleState state;
state.party = party;                  // the game's party
state.formation = formation;          // read by the Gau rule
state.gauMayAppear = veldtLetsGauAppear;
state.gauRecruited = gauHasJoined;
state.morphAvailable = terraCanMorph;
state.onTheVeldt = onTheVeldt;
state.config.magicOrder = magicOrderSetting;

const BattlerSet fromSetup = placeParty(state, {.fighters = CurrentParty{}, .scriptedSetup = setup});
loadParty(state, fromSetup, party.playTimeAtLastSave);
loadBattleInventory(state);
loadKnownSkills(state);
```

When calling them yourself, set the story facts (`magicOnly`, `morphAvailable`, `permanentMorph`,
`onTheVeldt`, `gauMayAppear`, `gauRecruited`) and the configuration before `placeParty()`: placing and
loading read them. `startBattle()` sets them from the `BattleStart` and the battle switches.

## Placing the party

`placeParty(state, lineup)` fills the four party slots. `PartyLineup::fighters` is one of:

| Alternative | Who stands |
|---|---|
| `CurrentParty{}` | every record in `currentParty`, in its slot; when two claim a slot the lower record wins |
| `ColosseumFighter{.fighter = actor}` | the fighter alone in the first slot; the battle is a colosseum battle |
| `FinalBattleStage{.standing = {...}}` | the fighters still standing keep their slots; each empty slot takes the next record from `state.finalBattleReserve` |

`PartyLineup::scriptedSetup` stages a scripted battle (`CharAI`, see
[character-ai.md](../data-layer/character-ai.md)): each cast member takes the slot of the party
member they match, or the first empty slot, or sits out when none is empty. A cast member brings
their flags, their sprite and their AI script (`Battler::aiScript`, script 256 + *n*). With
`CurrentParty` and `gauMayAppear` set, the Veldt setup is staged instead when Gau can come back now;
when he cannot, `gauMayAppear` is cleared.

For each filled slot it sets `actor`, `record`, `present`, `actsAsEnemy`, `backRow`, `graphics`
and `equipsAs`. It returns the slots a scripted setup or a colosseum match filled: pass them to
`loadParty()`, which heals those characters to full.

## Equipment into battle stats

`deriveEquipment(record)` works out a character's stats with their equipment, as a `DerivedStats`:
vigor, speed, stamina, magic power, evade and magic-block ratings, defense and magic defense, the two
hands, elements, the statuses the equipment protects against and keeps on, the five relic-effect
sets, block graphics, the run factor and the HP and MP boosts. It reads the record and the shipped
character and item properties and nothing else.

```cpp
game::CharacterRecord sabin = party.characters[5];
sabin.weapon = ItemId::EMPTY;           // a bare hand: the empty item's power 10, hit rate 200
const DerivedStats stats = deriveEquipment(sabin);
stats.weaponHand.battlePower;           // Sabin's battle power + 10
stats.hpBoost;                          // MaxBoost::None unless something raises his HP
```

`applyDerivedStats(state, slot, stats)` puts them on the battler: vigor doubled (at most 255),
evade and magic block as the hit check reads them (`evadeFactor`, `magicBlockFactor`), the hands
(`weaponHand`, `shieldHand`), relic effects, which statuses can be put on it, and its
equippable-by bits.

Which hand attacks depends on what each holds; the table is in the contract. In short: a weapon
beside a shield or nothing attacks alone, a weapon in each hand attack together, and the two-handed
grip survives only beside a bare hand and only with a Gauntlet.

## HP, MP and statuses

`loadPartyMember(state, slot, stats, joinedFromSetup)` sets HP and MP from the record, the maximums
raised by `stats`' boosts (`boostedMaximum()`) and capped at 9999 and 999, the level, and
`innateStatuses` — the statuses the battle starts the battler with. `joinedFromSetup` heals to full
and leaves the record only Blind, Poison, Magitek and Imp of its first eight statuses.

## Battle commands

`buildCommandList(state, slot)` fills `Battler::commands`, four `MenuCommand`s (`command`, `usable`,
`targeting`) from the record. Magitek armor and `magicOnly` rewrite the list; relics swap commands;
Morph, Leap, Dance, Magic and X-Magic go when their conditions fail; a member left with no Magic,
X-Magic or Lore has no MP. A removed row holds `BattleCommandId::NONE`.

## Spell lists

`Battler::spells` is a `SpellList`: `esper` and its `summon`, then `magic` (54 places) and `lores`
(24 places), each an optional `SpellListEntry` (`spell`, `targeting`, `mpCost`, `castable`). Every
party member's list has the same places, so a spell sits in the same place in everyone's menu;
`find(spell)` looks one up.

| Function | Does |
|---|---|
| `buildSpellLists(state, savedPlayTime)` | lays out every spell the party knows by `config.magicOrder` and gives every party member the list; counts `state.loresKnown` |
| `loadSpellList(state, slot)` | keeps the member's own spells (Gogo all, the first twelve their own, anyone else none) and lores, prices them with their equipment, adds their esper; sets `spellCount` |
| `refreshCastableSpells(state, slot)` | marks what they can cast now; the battle calls it whenever an action leaves `pendingUpdates.enabledMagic` set |

A party member's MP cost for an action (`commandMpCost()`) is the entry's `mpCost`; a summon costs the
list's summon.

## Status immunity

`Battler::inflictableStatuses` is the set of statuses that can be put on the battler; it starts
with all of them. `applyDerivedStats()` decides the first sixteen from the equipment's protection;
`refreshStatusImmunity()` takes away Float, Morph and the statuses the equipment keeps on, and pairs
Regen with Sap and Slow with Haste. Neither function ever gives a status back, so the set only
shrinks during a battle.

## The battle inventory and known skills

`loadBattleInventory(state)` fills `state.inventory`: `items` (the party's 256 slots, in order),
`weaponHands` and `shieldHands` (what each party slot holds, quantity 1), and `tools`. A
`BattleItem` carries `item`, `usableInBattle`, `throwable`, `isTool`, `isWeapon`, `isShield`,
`targeting`, `quantity` and `equippableBy` — the party slots whose member can hold it in a hand.
`battleItem(state, item, quantity)` builds one for the party as it stands.

`loadKnownSkills(state)` sets `swdtechsKnown`, `blitzesKnown`, `dancesKnown` (indexed by `DanceId`)
and `ragesKnown` (in order).

## Changing equipment during a battle

When the battle's Item menu puts an item in a party member's hand, write it into
`state.inventory.weaponHands` or `shieldHands` and set `Battler::equipmentChanged`. The next
`stepBattle()` writes the hands and the member's first eight statuses into their record, works out
their stats again, calls `ActionExecutor::refreshCommandList()`, and runs
`refreshStatusImmunity()` (`applyEquipmentChanges()`).

```cpp
state.inventory.weaponHands[0] = battleItem(state, ItemId::DIRK, 1);
state.at(BattlerSlot::character(0)).equipmentChanged = true;
stepBattle(state, executor);
```

## The formation

`chooseFormation(state, asked)` gives the formation fought: a conditional battle whose battle switch
is on replaces the one asked for (switch 48 + *n* for row *n* of the conditional-battle table), and a
randomized formation becomes itself or one of the three after it, by one draw.
`state.startingFormation` keeps the formation the battle began with — in the final battle the first
stage's.

## The monsters

Each monster slot is present or not by the formation, and every slot with a monster is loaded,
the last slot first; in a colosseum match the opponent fills every slot numbered below 256.
`loadMonster(state, executor, slot, id, text)` gives the battler, from the monster's record: its
stats (stamina 16 plus one per 512 of maximum HP, at most 40; magic power half again; vigor 56 to
63), its steal items, its elements, which statuses can be put on it, the statuses it starts with,
its traits and battle flags (`monsterFlags`), its special attack and animation, its AI script
(`aiScript`, then `executor.initAi`) and its name group (`nameGroup`). A present monster with first
strike starts ready to act, with the battle menus shut until someone has acted. Doom Gaze, in his
own formation, takes the HP kept in the battle switches.

```cpp
loadMonster(state, executor, BattlerSlot::monster(0), MonsterId::LOBO, cartridge.text);
state.at(BattlerSlot::monster(0)).stamina;   // 16 + max HP / 512, at most 40
```

The part of that a party member raging as the monster takes on too — its special attack, metamorph,
weak, absorbed and nullified elements, the statuses it cannot be given, the statuses its record keeps
on it and its True Knight cover — is `loadMonsterTraits(battler, id)`. It returns the statuses the
monster starts with rather than setting them: `loadMonster()` makes them the monster's
`innateStatuses`, and a Rage puts them on when its action's statuses take effect
([commands.md](commands.md#sketch-rage-and-control)).

## The battle type

`chooseBattleType(state, possibleTypes)` picks front, back, pincer or side from the formation's
possible types (`getFormationAux(id).possibleBattleTypes()`, bits `$10` front, `$20` back, `$40`
pincer, `$80` side), weighted by the battle-type rates — a front attack about four times in five
when all four are possible. A Back Guard rules out back and pincer attacks, and fewer than three
allies a side attack, unless nothing else is possible. `applyBattleType(state, aux)` then rolls for a
preemptive strike in a front or side attack (32 or 44 in 256, twice that with a Gale Hairpin; never
with the menus shut or where the formation forbids it) and moves the rows for a back or pincer
attack.

## The starting gauges

`fillStartingGauges(state)` gives each slot a random place 0-9 and sets each unset gauge: the party's
from speed plus a random amount plus its place in a front attack, from its place alone in a back or
pincer attack, and not at all in a side attack or after a preemptive strike — so it fills on the
first update and the party acts first. Monsters start from speed plus a random amount plus their
place, or nearly empty in a side attack or after a preemptive strike. Every empty slot adds 16 to a
random start. A first strike's gauge is kept, and in a final-battle stage after the first no gauge
is set.

## The opening

A back attack, a pincer, a side attack or a preemptive strike opens with its battle message, a
`BattleMessageAction` that interrupts the line; a formation that forbids preemptive strikes opens
with none.

## The final battle's stages

The final battle is recognized when its first stage is asked for; its party is
`BattleStart::finalBattleOrder`, four at a time. A won stage ends with
`BattleEnded{FinalBattleNextStage, nextFormation}`; hand the battle back and start the next:

```cpp
const BattleResult handedBack = writeBack(finished);
BattleState next = startNextFinalBattleStage(finished, handedBack, start, cartridge.text,
                                             cartridge.monsterSprites, executor);
```

The fighters standing go on in their slots with their statuses, less Dance, Rage and Chant; a
fallen, absent or air-anchored fighter's slot takes the next character in line. Mimic's memory, the
Reflect and Freeze timers, who is in the air and who has summoned carry on, the party keeps the way
each fighter faces while the new monsters are placed, and a fighter still in the air comes down as
the stage opens. The stage opens with `FinalBattleStageArrives` — the
background scrolls to it and its monsters appear — and the loop waits for that script. `BattleSim`
does all of this itself.

## What the game keeps

`writeBack(state)` returns a `BattleResult`:

| Member | Holds |
|---|---|
| `outcome` | the `BattleOutcome` |
| `party` | each party member's HP, MP (unless they have none) and lasting statuses — the first eight, Float and Interceptor; the inventory as the battle left it; a fallen ghost and anyone Possess took out of the party; Shadow out of it, without his esper, if he walked off; a colosseum wager given up unless the match ran out of time |
| `switches` | the battle switches, with how the battle ended (party defeated 64, ran out of time 68, timer expired 69, Zone Eater 71) and Doom Gaze's HP |
| `veldtFormation` | the formation the battle began with, when Gau can now find it on the Veldt |
| `shadowLeftParty` | Shadow walked off |

## The battle switches

`game::BattleSwitches` is the set of numbered switches and small counters event scripts, monster AI
and battles share: `isSet(n)`, `set(n)`, `clear(n)`, and `doomGazeHp()` / `setDoomGazeHp()`. Switch *n*
is bit *n* mod 8 of variable *n* / 8. The numbers the battle reads and writes are named in
`battle_switches.h` (`kMagicOnlySwitch`, `kPartyDefeatedSwitch`, and the rest).

## Gotchas

- **Order matters.** `placeParty()` before `loadParty()`; `loadParty()` before
  `loadBattleInventory()`, which reads each member's equippable-by bits. `startBattle()` keeps the
  original's order, which also keeps its random numbers in the original's sequence.
- **Empty slots draw too.** The starting gauges give every slot a place and a random start, present
  or not; tests that pin a draw count count all ten.
- **English monster names group by eight-byte windows.** Two monsters with the same English name
  usually get a line each in the list of names (`docs/Bugs.md`).
- **`startingFormation` is not `formation`** in the final battle: Doom Gaze, the colosseum and the
  Veldt look at the first, the stage chain at the second.
- **The battle changes its copy of the party.** Healing a setup's character, a permanent morph
  filling the Morph supply, and an equipment change during the battle all write
  `state.party`, not the caller's party.
- **`evadeFactor` is not the rating.** It is 255 for a rating of 0, 256 − 2 × the rating below 128,
  and 1 from 128.
- **Guests' equippable-by bits come from their name.** Anyone past Terra to Umaro gets the fifth and
  sixth bytes of their name in place of a bit of their own (`docs/Bugs.md`).
- **Step Mine prices from the last save.** `buildSpellLists()` takes `playTimeAtLastSave`, not the
  running clock.
- **`spellCount` leaves out lores.** It is what decides whether Magic stays in the menu.
- **`relicEffects4`'s Genji Glove bit** stays only while the wearer does not hold two weapons;
  `mayHoldTwoWeapons` is the glove itself.

## Where to change

| To change | Edit |
|---|---|
| The order of a battle's start | `assembleBattle()` in `battle_start.cpp` |
| Conditional battles and randomized formations | `chooseFormation()` in `battle_start.cpp` |
| What a monster takes from its record | `loadMonster()` and `loadMonsterTraits()` in `battle_start.cpp` |
| The battle type, preemptive strikes, rows | `chooseBattleType()` and `applyBattleType()` in `battle_start.cpp`; the weighted draw is `pickWeighted()` in `battle_rng.cpp` |
| Where gauges start | `fillStartingGauges()` in `battle_start.cpp` |
| What the game keeps | `writeBack()` in `battle_start.cpp` |
| What carries into a final-battle stage | `startNextFinalBattleStage()` in `battle_start.cpp` |
| Who stands where, scripted setups, the Gau rule | `currentParty()`, `stageScriptedSetup()`, `seat()` in `party_setup.cpp` |
| How equipment adds up | `addItem()` and `settleHands()` in `party_setup.cpp` |
| What reaches the battler | `applyDerivedStats()` in `party_setup.cpp` |
| HP and MP caps, a joining character's heal | `loadPartyMember()` in `party_setup.cpp` |
| The command rewrites and conditions | `buildCommandList()` in `party_setup.cpp` |
| The spell layout, costs, castability | `spell_lists.cpp` |
| An item's battle flags, the battle inventory | `battle_inventory.cpp` |
| An item's, character's, monster's or formation's numbers | the data tables ([item-properties.md](../data-layer/item-properties.md), [characters.md](../data-layer/characters.md), [monster-properties.md](../data-layer/monster-properties.md), [formations.md](../data-layer/formations.md)) |
