# World set pieces

## Public surface

```cpp
#include "data/world_cutscenes.h"

// The ending airship scene.
ostinato::endingSceneCircles();     // std::span<const EndingSceneCircle>, 9

// Figaro Castle rising out of the desert, and sinking back into it.
ostinato::figaroCastlePieces();     // std::span<const FigaroCastlePiece>, 6

// Strafing on the overworld.
ostinato::strafeAngle(mask);        // std::optional<std::uint16_t>, degrees
ostinato::strafeMaskReadsPastTheTable(mask);
ostinato::strafeAngles();           // std::span<const StrafeAngleEntry>, 11

// Words with no reader anywhere in the original.
ostinato::unusedCutsceneWords();    // std::span<const UnusedCutsceneEntry>, 48
```

## What it is

Four small tables that live among the overworld's set-piece routines. They have
little to do with each other beyond sitting in the same part of the game: two
drive scenes, one drives a control, and one is never read at all.

## The ending airship scene

Nine circles expand across the screen. Each waits out a start delay, then grows
every frame until it reaches its limit and holds there.

```cpp
struct EndingSceneCircle {
    std::uint8_t  index;
    std::uint16_t x, y;
    std::uint16_t radius;        // where it opens
    std::uint16_t radiusStep;    // how much it gains per frame
    std::uint16_t radiusLimit;   // where it stops
    std::uint16_t startDelay;    // frames before it begins
};
```

Every circle opens at the same radius and grows at the same rate — only position,
reach and timing differ. A compile-time assert in `src/data/world_cutscenes.cpp`
holds that, so if a circle ever disagreed it would be a decode error rather than
a design choice.

The circles are laid out from the outside in and start in that order, so the
scene opens wide and closes down. How far each one reaches does not follow the
same order: circle 2 reaches further than circle 1, and it is the only place the
sequence steps back up.

## Figaro Castle

Six pieces, one table, both directions. The same rows drive the castle rising out
of the desert and sinking back into it.

```cpp
struct FigaroCastlePiece {
    std::uint8_t index;
    std::uint8_t x, y;
    std::uint8_t animationStep;  // 0..3, where in its cycle the piece starts
    std::uint8_t phase;          // sub-position accumulator
    std::uint8_t phaseStep;      // how fast that accumulates
    std::uint8_t xOffset;        // sideways shudder, filled in as it moves
};
```

A piece advances `phase` by `phaseStep` every frame. When that wraps, it steps to
its next animation frame and takes a fresh `xOffset` from the overworld's own
wave, which is what makes the castle shudder while it moves. Every row starts
with `xOffset` at zero — the shudder is not authored per piece, it arrives once
the castle is under way.

## Strafing

Holding Y strafes along a bearing chosen by the directions being held. The index
is a bitmask — up, down, left, right in bit order — so index 6 is down and left.

```cpp
if (auto bearing = ostinato::strafeAngle(mask)) {
    strafeAlong(*bearing);
}
```

`strafeAngle()` returns nothing for five of the sixteen masks. That is not a
port decision: the table has eleven entries and the original indexes it with all
sixteen, so those five read past its end. See
[Bugs.md](../../Bugs.md#world-map-strafing-reads-past-the-end-of-its-bearing-table)
before changing anything here. `strafeMaskReadsPastTheTable()` answers the same
question without asking for a bearing.

Masks that hold two opposed directions and *are* in the table carry a bearing of
zero, which is also what the empty mask carries — so a caller cannot tell
"no direction" from "contradictory direction" by the bearing alone.

## Words nothing reads

`unusedCutsceneWords()` is 48 words sitting among the cutscene routines with no
reader anywhere in the original. Nothing in the game says what they mean, so
they are carried as raw values and named nothing. They are here so the table is
accounted for rather than quietly dropped; there is no reason to call this
unless you are investigating what it was for.

## Where to change things

| To change | Edit |
|---|---|
| Any value in any table here | Nothing in this repository — the values are read out of the cartridge and emitted by `tools/asm_parser/parse_train_data.py`. |
| An accessor's shape or bounds | `src/data/world_cutscenes.h` and `src/data/world_cutscenes.cpp`. |
| What an uncovered strafe mask does | The caller. `strafeAngle()` reports the absence; it does not choose a bearing. |

Regenerate the tables with:

```
python3 tools/asm_parser/parse_train_data.py --source-root original-src --repo-root .
```

## See also

- [train-ride.md](train-ride.md) — the other tables the same tool emits.
- [Bugs.md](../../Bugs.md) — the strafe table's shortfall, and the rest of what
  this port reproduces on purpose.
