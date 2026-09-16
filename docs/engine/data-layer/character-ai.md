# Scripted battle setups

How the game stages a battle that isn't simply "the current party fights".
Shadow in the colosseum, Terra's flashback, Vargas, Gau on the Veldt, the blitz
tutorial — twenty-four setups, each placing its own cast in the battle's
character slots and optionally overriding the background and the music.

## The surface

```cpp
#include "data/char_ai.h"

const CharAiSetup& setup = ostinato::charAiSetup(CharAiId::SHADOW_COLOSSEUM);

setup.flags.hidesNames();            // hide names and gauges in the battle menu
setup.flags.hidesPartyCharacters();  // show only this setup's cast
setup.background;                    // BattleBackgroundId, DEFAULT to keep the current one
setup.song;                          // SongId, NONE to keep the current music
setup.validMonsterTargets.has(0);    // which monster slots may be targeted

for (const CharAiSlot& slot : setup.slots) {
    if (slot.isEmpty()) continue;    // the party character stays in this slot
    slot.character.characterProperties();  // CharacterPropId
    slot.character.isEnemy();              // fights against the party
    slot.character.isNotInParty();         // excluded from entry/victory animations
    slot.graphics.usesCharacterDefault();  // or slot.graphics.id()
    slot.aiScript;                         // which monster A.I. script drives them
    slot.x; slot.y;                        // position, doubled by the consumer
}
```

`charAiSetups()` returns all twenty-four entries in id order for iteration; each
carries its `CharAiId` as `.id` and its data as `.record`.

| Type | Header | What it is |
|---|---|---|
| `CharAiId` | `ostinato/char_ai_id.h` | Which setup (24, `NONE` being "no override") |
| `CharAiSetup` | `data/char_ai.h` | One setup's 24 bytes |
| `CharAiSlot` | `data/char_ai.h` | One of the four character slots (5 bytes) |
| `CharAiFlags` | `ostinato/char_ai_flags.h` | The setup's two display flags |
| `CharAiSlotCharacter` | `ostinato/char_ai_slot_character.h` | A slot's character plus its two flags |
| `CharAiSlotGraphics` | `ostinato/char_ai_slot_graphics.h` | A slot's sprite sheet, or "the character's own" |

The background, music, character and sprite-sheet ids are the same types the
rest of the data layer uses — see [battle-backgrounds.md](battle-backgrounds.md)
and [characters.md](characters.md).

## What a setup does

Four slots, matching the battle's four character positions. A slot either names
a character or is left empty, in which case whoever the player has in that
position fights as normal. A named character can be marked two ways,
independently:

- **an enemy** — faces the other way and fights the party;
- **not in the party** — skipped by the entry and victory animations, with
  their name and gauge hidden.

Both at once is how a one-off opponent appears. The slot also names the monster
A.I. script that drives the character, which is what makes Shadow in the
colosseum fight on his own rather than take orders.

The setup's own flags are about presentation: whether names and gauges show at
all, and whether the player's party is hidden so only the cast is on screen.

## Adding or changing a setup

Edit the row in `src/data/generated/char_ai_data.inc`. Rows are in id order and
every field is named:

```cpp
.slots = {
    CharAiSlot{
        .character = CharAiSlotCharacter::enemy(CharacterPropId::SHADOW),
        .graphics = CharAiSlotGraphics::of(CharacterGfxId::SHADOW),
        .aiScript = 126,
        .x = 40, .y = 48,
    },
    CharAiSlot::unusedZeroed(),
    ...
},
```

Pick the builder that matches what you want: `ally`, `enemy`, `notInParty` or
`absentEnemy` for the character; `CharAiSlotGraphics::fromCharacter()` to let
the character's own sprite sheet stand.

Then update the matching row in `tests/fixtures/char_ai_expected.h`.
`ostinato-vi-background-tests` compares every record against that fixture, so a
row changed in one file and not the other is reported rather than accepted.

## Gotchas

- **The record's flags and a slot's flags are different things that share bit
  values.** Bit 7 on the record hides the party; bit 7 on a slot means that
  character is not in the party. They are separate types on purpose — do not
  pass one where the other is expected, and do not read a raw byte and decide
  for yourself which meaning applies.
- **An empty slot is the whole byte `$FF`, not a bit pattern.** The low six bits
  of `$FF` look like a perfectly valid character index, so ask `isEmpty()`
  first. `isEnemy()` and `isNotInParty()` report false on an empty slot rather
  than reading flags out of the sentinel.
- **Unused slots come in two spellings.** `CharAiSlot::unused()` is all `$FF`;
  `CharAiSlot::unusedZeroed()` zeroes the graphics and script bytes. Both mean
  unused — the loader reads neither form's trailing bytes — and both appear in
  the table. Keep whichever a row already has unless you have reason to change
  it.
- **`aiScript` is the low byte only.** The consumer adds `$0100` to reach the
  script. The stored byte is what the table holds.
- **`x` and `y` are halved.** The consumer doubles both when it places the
  character.
- **`NONE` is a real setup.** Id 0 is the ordinary battle: no overrides, no
  cast. Six further ids are named `UNUSED_nn` and nothing selects them.

## Where to change things

| Change | File |
|---|---|
| A setup's cast, background or music | `src/data/generated/char_ai_data.inc` (+ the fixture) |
| What a flag means | `include/ostinato/char_ai_flags.h`, `include/ostinato/char_ai_slot_character.h` |
| The slot or record layout | `src/data/char_ai.h` |
| The lookup itself | `src/data/char_ai.cpp` |

## See also

- [battle-backgrounds.md](battle-backgrounds.md) — the backgrounds a setup can select
- [characters.md](characters.md) — the character properties a slot names
- [monster-tables.md](monster-tables.md) — the A.I. scripts that drive a slot
