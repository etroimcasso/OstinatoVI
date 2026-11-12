# Battle rewards

What a won battle gives the party. The fallen monsters' experience is shared among the allies still
standing and their gold goes to the party; each living party member gains its share, reaches the levels
it has the experience for, and learns what those levels teach; equipment teaches its spells by the
battle's magic points; Strago keeps the lores he saw, Mog the dance of the ground he fought on, Terra
some Morph supply; and the monsters' rages are learned when Gau has leapt.

Each rule is a free function in `src/battle/rewards.h`, over the party (`game::Party`) and, where the
rule reads the battle, the `battle::BattleState` it was won in. A rule changes what it changes and
returns what the player is told about — a level reached, a technique learned, a dance mastered — and
shows nothing itself: the caller tells of each in the order the game does. The battle loop does not call
them, so a host that wins a battle applies them itself.

Every rule with a routine of its own in the cartridge is checked against that routine, run on the
engine's SNES machine over the same party.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_rewards.md`](../../contracts/battle_rewards.md).

## Files

| File | Holds |
|---|---|
| `src/battle/rewards.h` | `spoilsOf()`, `experienceShare()`, `addExperience()`, `earnedNextLevel()`, `learnAbilitiesAtLevel()`, `learnFromEquipment()`, `learnLores()`, `learnDance()`, `gainMorphSupply()`, `learnRages()`; `BattleSpoils`, `AbilitiesLearned`, `EquipmentLearning` |
| `src/battle/rewards.cpp` | the three-byte mask, the fallen statuses, the level scale, the Morph supply's limit and the rage bound as named constants; the console's divide |
| `src/battle/battle_end.h` | `wrapUpBattle()`, the close of a battle every ending but two runs; `kWrapUpWaitFrames` |
| `src/battle/items_obtained.h` | `ItemsObtained`, `obtainItem()`, `mergeItemsObtained()`, `itemsObtainedAtBattleStart()`, `itemsObtainedTail()`, `itemsObtainedTailAtPowerOn()`; `battleItemFromRecord()` and `recordOf()`, an item's five-byte form on the cartridge; `kMostOfOneItem` |
| `src/battle/battle_start.h` | `BattleStart::itemsObtainedTail` and `BattleResult::itemsObtainedTail`, the list's last three places carried from one battle to the next |
| `src/battle/battle_script.h` | the win's message numbers, `kMessageGotItem` to `kMessageMasteredATechnique`; `Wait`, a script command that holds the script for a number of frames |
| `src/assets/cartridge.h` | `IngestedContent::titleProgram`, the compressed title and cutscene program the power-on tail is read out of |
| `src/game/battle_switches.h` | `kMagicPointsEarnedSwitch` (67, the party has magicite); `BattleSwitches::cursedShieldBattles()` and `setCursedShieldBattles()` |
| `src/battle/spell_learning.h`, `src/battle/leveling.h` | `addSpellProgress()`, which the equipment's spells go through, and `levelUp()` ([espers.md](espers.md)) |
| `src/data/level_up.h` | `levelUpExp()`, the experience step to each level; `abilityLearnLevels()`, the levels the SwdTechs and Blitzes are learned at |
| `src/data/natural_magic.h` | `kNaturalMagicTerra`, `kNaturalMagicCeles`: each natural spell and its level |
| `src/data/item_properties.h` | `getItemProperties()`: each item's spell and learn rate |
| `src/data/battle_tables.h` | `backgroundDance()`: the dance each battle background teaches |
| `include/ostinato/game_limits.h` | `kMaxExperience` (15,000,000), `kMaxGil` (9,999,999) |
| `tools/damage-trace/damage_oracle.h` | `DamageOracle::addExperience()`, `learnFromEquipment()`, `levelUps()`, `learnAbilities()`, `learnRages()` over a `RewardScenario`; `wrapUp()` over an `EndingScenario`; `addItemObtained()` and `mergeItemsObtained()` over an `ItemsObtainedScenario`; `itemsObtainedTailAtPowerOn()`; the port's side of each and `differences()`; the draws; `graphicsCommand()` and `GraphicsCall`, the graphics program's calls as the oracle answers them |

## Experience and gold

`spoilsOf()` totals what the fallen give. A monster counts when it is dead, petrified or a zombie at
the win; one that fled gives nothing. On the Veldt the monsters give their gold and no experience:

```cpp
#include "battle/rewards.h"

const battle::BattleSpoils spoils = battle::spoilsOf(state);
const auto allies = static_cast<std::uint8_t>(state.allyCount);
const std::uint32_t share = battle::experienceShare(spoils.experience, allies);
// 301 experience between 4 allies: 75 each, the remainder dropped.
```

`addExperience()` adds a share to a record in three bytes and holds the sum to `kMaxExperience`:

```cpp
game::CharacterRecord& record = state.party.characters[*state.at(battle::BattlerSlot::character(0)).record];
battle::addExperience(record, share);
```

A character at 99 still gains experience; only the levels stop.

## Levels and what they teach

`earnedNextLevel()` says whether a record has the experience for its next level: 8 times the sum of the
steps up to it. A win levels a record up for as long as it does, and each level teaches what it
teaches:

```cpp
#include "battle/leveling.h"

bool toldOfALevel = false;
bool toldOfATechnique = false;
while (battle::earnedNextLevel(record)) {
    if (!toldOfALevel) {
        toldOfALevel = true;   // "Terra gained a level", once however many levels
    }
    battle::levelUp(record, state.version);
    const battle::AbilitiesLearned learned =
        battle::learnAbilitiesAtLevel(state.party, record.actor, record.level);
    if (learned.swdtech && !toldOfATechnique) {
        toldOfATechnique = true;   // "Mastered a new technique!", once in the win
    }
}
```

| Actor | At a level |
|---|---|
| Terra, Celes | each natural spell whose level is exactly the new level, marked `SpellProgress::kSpellJustLearned` unless already learned |
| Cyan | the SwdTech taught at the new level, if not known; `AbilitiesLearned::swdtech` |
| Sabin | the Blitz taught at the new level, if not known; `AbilitiesLearned::blitz` |
| anyone else | nothing |

A natural spell is not told of here: the win tells of every spell at `kSpellJustLearned` once the
levels are done, and that makes it `kSpellLearned`.

## Spells from equipment

`learnFromEquipment()` raises the spells the record's six pieces of equipment teach, in the order
weapon, shield, helmet, armor, relic 1, relic 2, each by its learn rate times the battle's magic points:

```cpp
game::SpellProgress& spells = state.party.spells[static_cast<std::size_t>(record.actor)];
const battle::EquipmentLearning learning =
    battle::learnFromEquipment(record, spells, state.switches, magicPoints);
const bool magicPointsEarned =
    learning.progressed && state.switches.isSet(game::kMagicPointsEarnedSwitch);
```

The Cursed Shield counts the battles won with it in `BattleSwitches::cursedShieldBattles()`, one per
slot holding it. When the count comes round to 0 — the 256th win — `curseDispelled` is true, the slot
holds the Paladin Shield, and the Paladin Shield teaches its spell in that same win.

Only the first twelve characters have spell lists: call it for Terra to Gau.

## Lores, the dance, the Morph supply and the rages

```cpp
battle::learnLores(state.party, state.loresLearned);   // told of one by one, the last lore first

if (const std::optional<DanceId> dance = battle::learnDance(state.party, state)) {
    // "Mastered a new dance!"
}

battle::gainMorphSupply(state.party, magicPoints);   // Terra in the party, with magicite

battle::learnRages(state.party, state);   // after a Leap, or a battle that ends by teaching rages
```

`learnDance()` reads the background as it stands at the end: a dance changes it. It needs Mog in one of
the party slots, fallen or not, and returns the dance only when the party did not know it.
`learnRages()` takes every monster in the six slots, alive, fallen or gone, numbered below 256.

## The wrap-up

`wrapUpBattle()` closes a battle once it has an outcome, before the party is handed back. A win, a loss
and every special ending but two run it; a Leap and a battle that ends by teaching its rages do not.

```cpp
#include "battle/battle_end.h"

battle::wrapUpBattle(state, executor);
```

In order it keeps the Morph supply as the meter stands, closes the party's battle menus from slot 3 to
slot 0 (a `CloseBattleMenu` request each), sets `BattleState::menusDisabled`, waits 32 frames (a
`RunBattleScript` holding one `Wait`), puts away every party member's hand (`putAwayItemsObtained()`
with all four marked: an item chosen for Item or Throw and never used comes back), merges the items
obtained into the inventory, and applies equipment changed in the Item menu.

## Items obtained

An item the party comes away with does not go straight into the inventory. It waits in
`BattleState::itemsObtained`, sixteen places used as a ring, and reaches `state.inventory` when the list is
merged:

```cpp
#include "battle/items_obtained.h"

battle::obtainItem(state, ItemId::POTION, 2);   // in place `added % 16`
battle::mergeItemsObtained(state);              // held: added to, at most 99; not held: the first empty slot
```

A battle starts with both counts at 0 and the first thirteen places empty; the last three are what the
host hands it. The host carries them from one battle's result to the next battle's start, and starts a
play session with what a console just switched on holds there, read out of the cartridge's own title
program:

```cpp
#include "assets/cartridge.h"
#include "battle/battle_start.h"

battle::BattleStart start{.formation = FormationRef::of(formation),
                          .itemsObtainedTail = battle::itemsObtainedTailAtPowerOn(
                              content.routines->decompress(assets::compressedStream(content.titleProgram)))};
// ... the battle ...
start.itemsObtainedTail = result.itemsObtainedTail;   // the next battle
```

A later stage of the final battle keeps the whole list and both counts.

`battleItemFromRecord()` and `recordOf()` turn a `BattleItem` into the five bytes the cartridge keeps an
inventory slot and a place of the list in, and back.

## Checking against the cartridge

The oracle runs the cartridge's own routine for each rule over a `RewardScenario` — the whole party in
its cells, the battle switches, the record whose turn it is, the magic points and the win's flags — and
`differences()` names every cell where the port's answer and the cartridge's part:

```cpp
#include "damage_oracle.h"

trace::runOnLoop([&] {
    trace::DamageOracle oracle{image};
    oracle.start();

    trace::RewardScenario s;
    s.recordIndex = 0;
    s.cells.party.characters[0].actor = CharacterPropId::TERRA;
    s.cells.party.characters[0].level = 21;
    s.cells.party.characters[0].experience = 20'000;
    s.partySlot = 2;

    const std::string apart = trace::differences(trace::portLevelUps(s), oracle.levelUps(s));   // ""
});
```

A routine that tells the player something calls the graphics program. While one runs, the oracle
answers that call at the graphics program's entry and records it as a `GraphicsCall`, with the cells the
graphics program reads; a message is compared on the numbers its text shows (`messageShown()`). The
commands that wait on the screen or the sound return at once; the two that keep the items obtained run
as the cartridge holds them. `ostinato-damage-trace --check` runs the draws as `add-exp`,
`item-learning`, `level-ups`, `abilities` and `rages`.

`wrapUp()` runs the cartridge's wrap-up over an `EndingScenario` — the party in its cells and the battle at
its end — and compares the party, the inventory, the list with both counts, the hands, the meters shown,
the menus and what was shown, in order (`EndingCall`: a menu closing, a wait, an item put away). After it,
`unwrittenReads()` names any cell the cartridge read that the scenario did not write.
`addItemObtained()` and `mergeItemsObtained()` run the graphics program's own two commands over an
`ItemsObtainedScenario`. `--check` runs these as `items-obtained`, `items-merged` and `wrap-up`.

## Gotchas

- **The share drops its remainder.** 301 experience between 4 allies is 75 each.
- **On the Veldt there is no experience**, but the gold still comes.
- **A sum past three bytes wraps before the cap.** 15,000,000 plus 3,145,728 is 1,368,512 — no share in
  play gets near that.
- **One "gained a level" per member**, however many levels it gains.
- **One technique message, and one blitz message, per win.** Cyan learning two SwdTechs in one win is
  told of once; `AbilitiesLearned` says each was learned, and the caller keeps the once.
- **A natural spell is learned at exactly its level.** A character set past that level by the story
  does not learn it at the next level-up.
- **A technique already known is not learned again**, and `learnAbilitiesAtLevel()` returns false for
  it.
- **The Paladin Shield teaches in the win that lifts the curse.**
- **A second Cursed Shield counts twice.** The shield's own slot and any other slot holding one each add
  a battle.
- **Gogo, Umaro and the guests gain levels but learn nothing.** Their actors are past the twelve with
  spell lists, and only Terra, Celes, Cyan and Sabin learn from a level.
- **Mog learns a dance fallen.**
- **The Morph supply's doubling keeps its low byte.** 128 magic points add 1, and 200 add 145.
- **A rage is learned from a monster that fled.** Every monster in the slots counts.
- **Switch 67 is the party having magicite, not Morph.** The Morph command reads switch 66.
- **An item obtained waits until a merge.** `obtainItem()` alone leaves the inventory as it was; the
  wrap-up, each drop, the colosseum's prize and a battle menu opening merge.
- **A merge stops for good at a full inventory.** An item the party does not hold, with no empty slot,
  stays in its place, and every item obtained after it waits behind it — held items too.
- **A held item's sum keeps its low byte before it is held to 99.** 200 plus 100 is 44.
- **An item not held arrives in the number it was obtained in**, past 99 too.
- **The thirteenth item obtained in one battle brings the last three places with it.** On a console just
  switched on they hold 108 Thief Knives, 112 Silk Robes and 136 Excaliburs. The fourteenth to sixteenth
  then wait until a twenty-ninth.
- **The last three places outlast the battle.** Pass `BattleResult::itemsObtainedTail` into the next
  `BattleStart`; a `BattleStart` left as it is starts them empty.
- **The wrap-up puts away every hand**, not only the ones marked as holding an item.

## Where to change

| To change | Edit |
|---|---|
| Which monsters count as fallen | `kFallen` in `src/battle/rewards.cpp` |
| The Veldt's rule | `spoilsOf()` in `src/battle/rewards.cpp` |
| How the share is divided | `experienceShare()` and `divide()` in `src/battle/rewards.cpp` |
| The experience cap | `kMaxExperience` in `include/ostinato/game_limits.h` (generated from `include/const.inc` by `tools/asm_parser/parse_const_enums.py`) |
| The experience a level needs | `kLevelExperienceScale` in `src/battle/rewards.cpp`; the steps in `src/data/generated/level_up_data.inc` |
| What a level teaches | `learnAbilitiesAtLevel()` in `src/battle/rewards.cpp`; the tables in `src/data/generated/natural_magic_data.inc` and `level_up_data.inc` |
| The Cursed Shield | `learnFromEquipment()` in `src/battle/rewards.cpp` |
| The Morph supply's limit | `kMostMorphSupply` in `src/battle/rewards.cpp` |
| Which monsters have rages | `kMonstersWithRages` in `src/battle/rewards.cpp` |
| The win's message numbers | `src/battle/battle_script.h` |
| The wrap-up's steps and its wait | `wrapUpBattle()` and `kWrapUpWaitFrames` in `src/battle/battle_end.h`/`.cpp` |
| How the list fills and merges | `obtainItem()` and `mergeItemsObtained()` in `src/battle/items_obtained.cpp`; `kMostOfOneItem` in `items_obtained.h` |
| Which places a battle's start empties | `itemsObtainedAtBattleStart()` and `kItemsObtainedTailPlaces` in `src/battle/items_obtained.h`/`.cpp` |
| Where the power-on tail is read from | `kItemsObtainedTailInTitleProgram` in `src/battle/items_obtained.cpp` |
| How long a `Wait` holds the script | `playCommandFrame()` in `src/battle/presentation.cpp` |
| The ending's cells the oracle writes and reads | `writeEndingScenario()`, `readEndingResult()`, `writeItemsObtained()` and `writeInventory()` in `tools/damage-trace/damage_oracle.cpp` |
| The party's cells the oracle writes and reads | `partyImage()`, `writeParty()` and `readParty()` in `tools/damage-trace/damage_oracle.cpp` |
| How the oracle answers the graphics program | `answerGraphics()` in `tools/damage-trace/damage_oracle.cpp` |
| What text reads which message variable | `kMessageVariableReads` in `tools/damage-trace/damage_oracle.cpp` |
