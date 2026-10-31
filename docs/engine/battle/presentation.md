# The battle's presentation

The presentation decides **what the player sees, and for how long**: which name or message is up at
the top of the screen and how much of its text has appeared, which damage numerals are bouncing and
how far, which monsters are fading out, which monsters stand on the field, what the list of monster
names reads, and which pose each party member stands in. It plays the battle's requests frame by
frame and tells the battle when it may take its next step. It draws nothing — the rendering
components read it ([../rendering/battle.md](../rendering/battle.md)).

The behavior it reproduces is specified in
[`docs/contracts/battle_presentation.md`](../../contracts/battle_presentation.md); this page is about
using and changing the code.

## Files

| File | Holds |
|---|---|
| `src/battle/presentation.h` | `BattlePresentationState`, `BattlePresentationContext`, `tickBattlePresentation()`, `battleInput()`, `awaitingKey()`, `monsterNameList()`, `partyMemberPose()`, `poseStep()`, `battleMessageSteps()`, `monsterDialogSteps()`, and what they hold: `MessagePlayback`, `ShownMessage`, `ShownGlyph`, `MessageStep`, `ShownNumeral`, `DeathFade` |
| `src/battle/battle_script.h` | `DamageNumbersShown`, `DamageNumber`, `DamageNumberWait` — the numerals an attack shows |

## Running it

Each frame, give the battle what the presentation reports, tick the battle, and hand its requests
to the presentation:

```cpp
#include "battle/battle_sim.h"
#include "battle/presentation.h"

battle::BattlePresentationState shown;
const battle::BattlePresentationContext context{
    .text = content.text,
    .metrics = content.dialogFont.metrics,
    .messageSpeed = battle::kNewGameMessageSpeed,
};

while (!shown.sceneClosed) {
    battle::BattleInput input = battle::battleInput(shown);  // time runs; idle or not
    input.playerActions = menu.finishedActions();
    sim.tick(input);
    const std::vector<battle::BattleRequest> requests = sim.takeRequests();
    battle::tickBattlePresentation(shown, sim.state(), requests, context, confirmPressed);
    // draw from sim.state() and shown
}
```

`tickBattlePresentation` takes in the requests first, then advances one frame:

- `OpenBattleScene` shows the monsters standing and builds the list of names; `RefreshMonsterNames`
  builds it again from the battle as it stands; `CloseBattleScene` sets `sceneClosed`;
  `RunBattleScript` queues the script. The menu requests and the rest show nothing here.
- Each party member's pose counter counts one, and every numeral on the screen moves one frame on.
- If a script is playing or queued, it plays: commands that take no frames run straight through in
  this frame, and the frame ends on the first command that takes frames.
- Otherwise the monsters that have fallen fade out, and a monster back on the field is shown again.

`battleInput(shown).presentationIdle` is false while anything is playing — a command, a queued
script, a fade — so the battle loop waits for it; battle time runs throughout.

`confirmPressed` is the player confirming a message that waits for a key; `awaitingKey(shown)` says
whether one is waiting.

## A script's commands

| Command | Shown as | Frames |
|---|---|---|
| `AttackNameShown`, `ItemNameShown`, `EsperNameShown`, `BushidoNameShown`, `CommandNameShown` | the name in the narrow window, whole at once | the frame it appears and the message time after |
| `MonsterSpecialAttackNameShown` | the name in the narrow window | the frame it appears and 32 after |
| `BattleMessage` | the battle message in the wide window, whole at once | the frame it appears and the message time after |
| `MonsterDialogShown` | the monster's words in the wide window, one letter a frame | the letters, then the message time |
| `DamageNumbersShown` with `UntilGone` | every numeral at once | until every numeral on the screen has gone |
| `DamageNumbersShown` with `CarryOn` | one numeral at a time | two frames each, ten when one is already over that battler |
| `ReadyStance` | the actor's waiting pose (`commandWaitingPose`) | none |
| `StepBack` | every waiting pose cleared | none |
| `PoseReset` | that battler's waiting pose cleared | none |
| `FormationChanges` | the new formation's monsters shown | none |
| `MonstersEnterOrLeave` | the revived shown, or the hidden no longer shown | none |
| `AttackAnimation`, `BattleEventPlays`, `MonsterAnimationPlays` | nothing | none |

The message time is `messageHoldFrames(context.messageSpeed)`: 32 frames at speed 0 up to 112 at
speed 5, 64 at a new game's speed 2. A message that waited for a key closes the frame after the
player confirms, without the message time.

### Messages

The text of a message is played as `MessageStep`s: a letter (`MessageGlyph`, a dialog-font glyph),
a line break, a pause, or a key wait. `battleMessageSteps` and `monsterDialogSteps` build them from
the cartridge's text with every name and number filled in — a character's name by their record,
an item's, attack's or command's by its index, the message's numbers without leading zeros.

While a message plays, `shown.message->shown` holds what is on screen: the window, each letter with
the pen's x for it, and in the narrow window the `centering` that moves the text to sit in the
middle. A line break raises every letter on the line one pixel a frame (`ShownGlyph::rise`) for 16
frames, then the line is gone and the next starts at the left. A pause holds the text for its
frames.

### Damage numerals

`DamageNumbersShown` carries one `DamageNumber` per battler: the amount (up to 9999), or `miss`,
and `heals` for a green numeral. With `UntilGone` every numeral starts at once over its battler and
the script waits until all are gone; with `CarryOn` they start one at a time and the script goes on
while they bounce, up to four of them on the screen with a fifth replacing the oldest. Each lives
64 frames (`ShownNumeral::frame` 0 to 63). A numeral over a battler not on the field is not shown.

`ShownNumeral::offset` is where the numeral starts left of its battler: four pixels a digit, so a
number sits centered; "Miss" takes 8 shown on its own and 0 shown with the others, and starts one
cell further right still.

### A monster's death

Between scripts, every monster shown that the battle no longer has standing starts a `DeathFade`
together: one frame as it was (`frame` 0), then 32 in the death palette (`inDeathPalette()`), its
colors `darkening()` 31sts darker each frame; then it is no longer shown.

## What the presentation works out

```cpp
const battle::MonsterNameList names = battle::monsterNameList(sim.state());
const std::uint8_t pose = battle::partyMemberPose(sim.state().at(slot), shown.waitingPoses[slot.position()]);
const int step = battle::poseStep(shown.poseCounters[slot.position()]);
```

- **The list of names** has a line per name group, in the order the monsters stand in their slots,
  for every monster present, not fallen, counted in the battle, not petrified, dead or a zombie and
  not hidden from the list; at most four lines.
- **A party member's pose**: fallen when dead, the wounded pose when asleep, poisoned or near death,
  the confused pose, the chanting pose; otherwise the pose it waits in before acting, or the idle
  pose (6).
- **The pose step**: each party member's counter starts at 0, 2, 4 or 6 by slot and counts every
  frame; a pose takes its next step every eight counts.

## Gotchas

- **Tick the presentation after the battle, every frame.** The battle's next step waits on
  `presentationIdle`, and a fade or a message only advances when the presentation is ticked.
- **The context outlives nothing.** `BattlePresentationContext` holds references to the text and the
  font's widths; keep the cartridge content alive while the battle runs.
- **The monsters drawn are `shown.monstersShown`.** A monster that has fallen stays in it until its
  fade ends, so draw from it rather than from the battle's `monstersStanding`.
- **Names and numbers come from the message.** A `BattleMessage`'s numbers are its text's first two
  variables: they fill in its numbers and name the item, attack, command or party slot its text
  reads from them.

## Where to change things

| To do this | Edit |
|---|---|
| Change how long a message stays up | `messageHoldFrames` ([../data-layer/battle-gfx-tables.md](../data-layer/battle-gfx-tables.md)); the special attack name's `kSpecialAttackNameFrames` in `presentation.h` |
| Change what a script command shows | `startCommand()` in `src/battle/presentation.cpp` |
| Change how a message's text is read | `textSteps()` in `src/battle/presentation.cpp` |
| Change a numeral's life or wait | `kDamageNumberLife`, `kSameTargetWaitFrames` in `presentation.h` |
| Change the fade | `kDeathFadeFrames` in `presentation.h`; how it looks is `MonsterDeathFade` ([../rendering/battle.md](../rendering/battle.md)) |

## See also

- [core.md](core.md) — the battle, its requests and `BattleSim`
- [commands.md](commands.md) — the battle script and what writes it
- [../rendering/battle.md](../rendering/battle.md) — drawing what the presentation shows
- [../../contracts/battle_presentation.md](../../contracts/battle_presentation.md) — the rules and where each comes from
