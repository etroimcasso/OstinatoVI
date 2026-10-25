# Starting a battle: the party

Before a battle's first frame, its party side is built from the game's records: who stands in each
of the four party slots, each member's battle stats with their equipment, their HP, MP and
statuses, their battle command menu and spell list, the battle inventory, and what the party knows
of its other abilities. Everything here works on `BattleState::party`, the battle's own copy of the
game's party; the battle reads and changes that copy as it goes.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_core.md`](../../contracts/battle_core.md) § "The party at the battle's start".

## Files

| File | Holds |
|---|---|
| `src/game/character_record.h` | `game::CharacterRecord`, `game::Party`, `PartyMembership`, `SpellProgress`, `InventorySlot`, `PlayTime` |
| `src/battle/party_setup.h` | `placeParty()`, `loadParty()`, `loadPartyMember()`, `buildCommandList()`, `deriveEquipment()`, `applyDerivedStats()`, `refreshStatusImmunity()`, `applyEquipmentChanges()`, `DerivedStats`, `MaxBoost`, `PartyLineup` |
| `src/battle/spell_lists.h` | `SpellList`, `SpellListEntry`, `buildSpellLists()`, `loadSpellList()`, `refreshCastableSpells()` |
| `src/battle/battle_inventory.h` | `BattleInventory`, `BattleItem`, `ToolSet`, `battleItem()`, `loadBattleInventory()`, `loadKnownSkills()` |
| `src/battle/battle_state.h` | the battler's party-member fields: `Hand`, `MenuCommand`, `BlockGraphicSet` |

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

## Building the party side

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

Set the story facts (`magicOnly`, `morphAvailable`, `permanentMorph`, `onTheVeldt`, `gauMayAppear`,
`gauRecruited`) and the configuration before `placeParty()`: placing and loading read them.

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

## Gotchas

- **Order matters.** `placeParty()` before `loadParty()`; `loadParty()` before
  `loadBattleInventory()`, which reads each member's equippable-by bits.
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
| Who stands where, scripted setups, the Gau rule | `currentParty()`, `stageScriptedSetup()`, `seat()` in `party_setup.cpp` |
| How equipment adds up | `addItem()` and `settleHands()` in `party_setup.cpp` |
| What reaches the battler | `applyDerivedStats()` in `party_setup.cpp` |
| HP and MP caps, a joining character's heal | `loadPartyMember()` in `party_setup.cpp` |
| The command rewrites and conditions | `buildCommandList()` in `party_setup.cpp` |
| The spell layout, costs, castability | `spell_lists.cpp` |
| An item's battle flags, the battle inventory | `battle_inventory.cpp` |
| An item's or character's numbers | the data tables ([item-properties.md](../data-layer/item-properties.md), [characters.md](../data-layer/characters.md)) |
