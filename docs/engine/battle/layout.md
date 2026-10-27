# The battle's layout

Where each battler stands and which way it faces. The layout decides which side of a pincer each
monster is on and, in a side or pincer attack, whether a blow lands in the target's back. Battlers
turn to face whoever they attack, so a facing set at the start of a battle can change with every
action.

The rules, and the line of the original each is read from, are in
[`docs/contracts/battle_commands.md`](../../contracts/battle_commands.md) § The battle layout.

## Files

| File | Holds |
|---|---|
| `src/battle/battle_layout.h` | `Facing`, `PartyLayoutRow`/`kPartyLayout`, `placeMonster()`, `layOutMonsters()`, `layOutParty()`, `facedTarget()`, `turnToFace()`, `turnToFaceTargets()` |
| `src/battle/battle_state.h` | `FieldSide`, `Battler::side`, `BattleState::facingRight`, `pincerSides`, `animationTargets` |
| `src/data/generated/party_layout_data.inc` | the party's facing and place for each battle type, generated from the cartridge's tables |

## Parts of the field

Every battler stands in one part of the field: `FieldSide::Left`, `Center` or `Right`.

| Battle type | Monsters | Party |
|---|---|---|
| Front | left | right |
| Back | right | left |
| Pincer | left or right, by where the formation puts each one | center |
| Side | center | slots 0 and 1 right, slots 2 and 3 left |

A monster and a party member never stand in the same part, so "is the target to my right?" is
always a comparison of two parts: `state.at(target).side > state.at(attacker).side`.

`BattleState::facingRight` holds who faces right; anyone not in it faces left. The monsters on each
side of a pincer are `pincerSides[0]` (left) and `pincerSides[1]` (right).

## Laying out a battle

`startBattle()` lays the battle out after the battle type is chosen; the next stage of the final
battle lays out its new monsters and keeps the party as it was. To lay out a battle built by hand:

```cpp
BattleState state = ...;                      // formation, battle type and battlers loaded
layOutMonsters(state, content.monsterSprites);
layOutParty(state);
state.at(BattlerSlot::monster(0)).side;       // FieldSide::Left in a front attack
state.facingRight.has(BattlerSlot::character(0));
```

`layOutMonsters()` needs the monsters' sprite sizes (`MonsterSprites`, read from the cartridge):
whether a monster in a pincer joins the right group depends on how far its sprite reaches, and in
a side attack which half of the field its middle is in decides its facing. With an empty
`MonsterSprites` every monster but the Ghost Train, whose size is fixed, measures 0 tiles across
and is placed by its formation position alone.

`placeMonster()` is the placement on its own, for one monster:

```cpp
const MonsterPlacement placed = placeMonster(BattleType::Pincer, formation.positionX(slot), widthTiles);
placed.side;      // FieldSide::Left or Right
placed.facing;    // Facing::Right on the left, Facing::Left on the right
placed.center();  // the middle of its sprite, in pixels across
```

## Turning to face a target

An attacker turns to face the target of each animation it is shown in: the first monster aimed at,
or with none the first party member (`facedTarget()`). A party member turns only toward a monster,
a monster only toward a party member.

```cpp
turnToFace(state, BattlerSlot::character(1), BattlerSet::of(BattlerSlot::monster(1)));
```

The battle loop does this for you: `turnToFaceTargets()` runs over each action's script as it is
handed to the presentation, in the order the script plays. It also remembers whom the last
animation was aimed at (`BattleState::animationTargets`).

```cpp
turnToFaceTargets(state, script);   // every ReadyStance and AttackAnimation in it
```

## Gotchas

- **Some animations turn no one.** Mimic, Row, Def and the roulette cursor neither turn their
  attacker nor change `animationTargets`.
- **After the party runs, no party member turns** for the rest of that script. Monsters still do.
- **A Jump lands facing the last animation's targets**, not its own: whatever `animationTargets`
  holds when its landing plays.
- **Facings matter only in side and pincer attacks.** In a front or back attack they are what the
  player sees and nothing more; `hitInTheBack()` reads them only in the other two.
- **A pincer opens unrunnable.** The run check at a battle's start sees every monster on both
  sides, so even a pincer with all its monsters on one side cannot be run from until the check runs
  again ([`docs/Bugs.md`](../../Bugs.md)).
- **A party member that has stepped forward is not followed.** The part of the field is where a
  battler stands, not where an attack animation has walked it to. In two side-attack formations
  (Cephaler ×3, Orog + Power Demon ×2) that step would carry party member 0 past a monster; the
  port does not turn it around there.

## Where to change

| To change | Edit |
|---|---|
| The party's facings or places by battle type | regenerate `party_layout_data.inc` (`tools/asm_parser/parse_battle_layout.py`) |
| Where the monsters stand in each battle type | `placeMonster()` and its constants in `src/battle/battle_layout.h` |
| Which animations turn their attacker | `turnToFaceTargets()` in `src/battle/battle_layout.cpp` |
| Who an attacker faces | `facedTarget()` and `turnToFace()` in `src/battle/battle_layout.cpp` |
| When a blow lands in the back | `hitInTheBack()` in `src/battle/attack.cpp` |
