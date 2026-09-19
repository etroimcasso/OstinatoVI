# Blitz inputs

The button sequences Sabin's eight blitzes are performed with, and the
controller inputs that count toward each step of a sequence.

## The surface

```cpp
#include "data/blitz_codes.h"

const BlitzCode& pummel = ostinato::blitzCode(AttackId::PUMMEL);

pummel.inputCount();   // 4
pummel.sequence();     // LEFT, RIGHT, LEFT, A_BUTTON
pummel.inputs;         // all eleven slots, NONE after the sequence

BlitzButtonMask mask = ostinato::blitzButtonMask(BlitzCodeInput::DOWN_LEFT);
mask.has(PadInput::DOWN);                                  // true
mask.acceptsPress(BlitzButtonMask::of(PadInput::LEFT));    // true
mask.acceptsPress(BlitzButtonMask::of(PadInput::UP));      // false
```

`blitzCodes()` returns all eight entries in learning order, each carrying the
blitz's `AttackId` as `.id` and its sequence as `.record`. `blitzButtonMasks()`
returns the fifteen `{ input, mask }` pairs in `BlitzCodeInput` order.

| Type | Header | What it is |
|---|---|---|
| `BlitzCodeInput` | `ostinato/blitz_code_input.h` | One step of a sequence: a button, a direction, or `NONE` |
| `PadInput` | `ostinato/pad_input.h` | The controller's twelve inputs, each valued as its bit in a button mask |
| `BlitzButtonMask` | `ostinato/blitz_button_mask.h` | A set of `PadInput`s (2 bytes) |
| `BlitzCode` | `data/blitz_codes.h` | One blitz's sequence (12 bytes) |

The blitz ids are the same `AttackId`s the learn-level table uses — see
[level-up.md](level-up.md).

## What a record holds

Twelve bytes: eleven input slots, then the number of inputs in the sequence
multiplied by two. The sequence fills the slots from the front and `NONE` fills
the rest. Every sequence ends with `A_BUTTON`, the press that confirms it.

The battle reads the stored count to know how far to compare; the menu's blitz
list draws the first ten slots. The longest sequence, Bum Rush, is ten inputs
long, so the list shows every sequence in full.

## Masks

Each `BlitzCodeInput` has a mask naming the controller inputs it stands for. A
press counts toward a step when it shares **at least one** bit with that step's
mask. A diagonal's mask holds both directions, so `DOWN_LEFT` also accepts a
press of `DOWN` or `LEFT` alone.

`NONE`'s mask is every bit (`BlitzButtonMask::allButtons()`). It only ever sits
in the padding past a sequence's end, which the battle does not compare.

## Adding or changing a blitz

Edit the row in `src/data/generated/blitz_code_data.inc`:

```cpp
BlitzCodeEntry{
    .id = AttackId::PUMMEL,
    .record = BlitzCode::of({
        BlitzCodeInput::LEFT,
        BlitzCodeInput::RIGHT,
        BlitzCodeInput::LEFT,
        BlitzCodeInput::A_BUTTON,
    }),
},
```

`BlitzCode::of()` pads the sequence and stores the doubled count for you; a
sequence longer than eleven inputs does not compile. Keep the sequence at ten
inputs or fewer if the menu should show all of it, and end it with
`A_BUTTON`.

Then update the matching row in `tests/fixtures/blitz_code_expected.h`, which
holds the raw bytes. `ostinato-vi-misc-tests` compares every record and every
mask against it.

To change what an input accepts, edit its pair in
`src/data/generated/blitz_button_mask_data.inc`:

```cpp
{ BlitzCodeInput::DOWN_LEFT, BlitzButtonMask::of(PadInput::DOWN, PadInput::LEFT) },
```

## Gotchas

- **The count is stored doubled.** Read it through `inputCount()`, and never
  write `doubledInputCount` by hand — `BlitzCode::of()` sets it.
- **Masks match on any shared bit, not all of them.** Use `acceptsPress()` rather
  than comparing masks for equality when deciding whether a press counts.
- **`PadInput` values are bits, not indices.** Combine them with
  `BlitzButtonMask::of(...)`; don't use them to index an array.
- **`PadInput` names the game's own controller.** Mapping a player's actual
  device onto these inputs is the job of the game's input bindings, not of this
  table.

## Where to change things

| Change | File |
|---|---|
| A blitz's sequence | `src/data/generated/blitz_code_data.inc` (+ the fixture) |
| What an input accepts | `src/data/generated/blitz_button_mask_data.inc` (+ the fixture) |
| The record layout or the builder | `src/data/blitz_codes.h` |
| The mask type | `include/ostinato/blitz_button_mask.h` |
| The lookups | `src/data/blitz_codes.cpp` |

## See also

- [level-up.md](level-up.md) — the level each blitz is learned at
- [battle-commands.md](battle-commands.md) — the Blitz command itself
