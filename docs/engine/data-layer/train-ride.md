# Magitek train ride

## Public surface

```cpp
#include "data/train_ride.h"

// The script's three selectors pick one curve item each, per step.
ostinato::trainPitchCurve1(item);   // std::span<const TrainCurveEntry>, 32 steps
ostinato::trainPitchCurve2(item);   // the same item, read alongside the first
ostinato::trainYawCurve(ostinato::TrainYawType::LEFT_TURN);
ostinato::trainBackgroundCurve(ostinato::TrainBackgroundType::NO_PIPES);

// Whole curves, if you want the flattened run.
ostinato::trainPitchData1();        // 13 items x 32 steps
ostinato::trainYawData();           //  3 items x 32 steps
ostinato::trainBackgroundData();    //  5 items x 32 steps
ostinato::trainYawMultiplier();     //  1 item  x 32 steps

// How much of an offset a given row keeps.
ostinato::trainSweepFalloff(step);  // steps 1..24
ostinato::trainLayerFalloff(layer); // layers 1..22

// The tunnel's scenery.
ostinato::trainTileArrangement(variant);   // 12 TrainTileEntry
ostinato::trainRotationAngle(index);       // degrees, index 0..7

// The airship's takeoff and landing camera move.
ostinato::airshipLiftoffSteps();    // std::span<const AirshipLiftoffStep>, 32
```

## What it is

The Magitek train ride is a tunnel drawn in perspective, steered by a script.
Each step of that script names three curves — a pitch, a yaw, and a set of
scenery — and the ride then walks all three for 32 frames, one entry per frame,
before the script advances.

This page covers the data those curves hold. The script that selects them, and
the commands it carries, belong to the ride's behaviour and are not here.

The airship's takeoff and landing camera move sits on this page too. It is
unrelated to the train, but it is the same kind of thing: a curve of per-frame
steps for a camera, stored the same way.

## The steering curves

Four curves are stored the same way: a flat run of 32-byte items, with the
script's byte picking an item. `trainPitchCurve1()` and friends hand back one
item's 32 steps; the `...Data()` calls hand back the whole flattened run, indexed
`item * kTrainCurveItemSteps + step`.

| Curve | Items | Values |
|---|---|---|
| `trainPitchData1()` | 13 | Signed. How far the track tips over each frame. |
| `trainPitchData2()` | 13 | Signed. Read alongside the first, under the same selector. |
| `trainYawData()` | 3 | Signed. How hard the track bends. |
| `trainYawMultiplier()` | 1 | Signed. Scales the bend further down the screen. |
| `trainBackgroundData()` | 5 | Which scenery arrangement each frame wears. |

Two of the five have names, because the cartridge gives its items names and
those are carried into enums:

```cpp
enum class TrainYawType : std::uint8_t {
    STRAIGHT, LEFT_TURN, RIGHT_TURN,
};

enum class TrainBackgroundType : std::uint8_t {
    PIPES_MORE_BEAMS, PIPES_FEWER_BEAMS, NO_PIPES,
    SPLIT_TRACK_MORE_WALLS, SPLIT_TRACK_FEWER_WALLS,
};
```

The two pitch curves and the yaw multiplier have no names for their items, so
they take a plain number. Pitch items run 0 to 12; the yaw multiplier has one
item, because the script's selector for it is zero on every step of every course
and no second item exists to reach.

`trainPitchCurve1(item)` and `trainPitchCurve2(item)` deliberately take the same
argument: one selector byte drives both, and separating them would let a caller
pair curves the ride never pairs.

Every entry of `trainBackgroundData()` names a scenery arrangement that exists —
a compile-time assert in `src/data/train_ride.cpp` holds that, because the value
becomes an offset directly.

## Falloff

`trainPositionMultipliers()` is one 64-entry curve, and the ride reads it at two
different places for two different purposes. Both readings are kept:

| Function | Range | Reads |
|---|---|---|
| `trainSweepFalloff(step)` | 1..24 | 39 entries into the curve. |
| `trainLayerFalloff(layer)` | 1..22 | One entry back, so layer 1 is the first entry. |

They land in different parts of the curve and return different values for the
same argument. Collapsing them to one base would change what the tunnel looks
like, so do not.

## Tunnel scenery

Each background value names an arrangement of twelve tiles — five left wall,
five right wall, one ceiling, one rail:

```cpp
struct TrainTile {
    std::uint8_t x, y, tileIndexLow, tileIndexHigh;

    std::uint16_t tileIndex() const;   // the two stored bytes read together
    bool          unused() const;      // this slot draws nothing
};
```

The four bytes are stored exactly as the cartridge holds them, so an arrangement
survives a round trip unchanged. Most slots are empty — `unused()` is the same
test the tunnel builder makes, and an empty slot means *place no tile here*, not
*place tile zero*.

```cpp
for (const auto& entry : ostinato::trainTileArrangement(variant)) {
    if (entry.tile.unused()) continue;
    placeTile(entry.tile.x, entry.tile.y, entry.tile.tileIndex());
}
```

`trainTiles()` gives the flattened run of all twenty arrangements, indexed
`variant * kTrainTilesPerVariant + slot`.

## Roll angles

`trainRotationAngles()` holds eight angles in degrees. A script command carries a
three-bit index into them, which is why there are exactly eight.

## The airship camera

`airshipLiftoffSteps()` is 32 frames of a camera move, and both the takeoff and
the landing run off it — the takeoff walks the frames forward and subtracts each
step, the landing walks them backward and adds.

```cpp
struct AirshipLiftoffStep {
    std::uint8_t  index;
    std::uint16_t scaleDelta;
    std::uint16_t nearWeightDelta;
    std::uint8_t  farWeightDelta;
    std::uint8_t  horizonDelta;
    std::uint8_t  pivotYDelta;
};
```

The overworld's ground plane is a projective transform. `scaleDelta` moves the
transform's scale; `nearWeightDelta` and `farWeightDelta` move the projective
weight at the near and far edges of the drawn area, which is what sets how
steeply the ground recedes; `horizonDelta` moves where the plane starts on
screen; `pivotYDelta` moves the point it turns about. Applying a frame's five
deltas gives you the next frame's transform.

The original had no transform to apply, so it rebuilt the same foreshortening one
screen row at a time, dividing per row. A transform carries foreshortening in its
own coefficients, so one transform per frame does the whole job and no per-row
table is needed.

The move rises to its largest step at frame 15 and mirrors back down, with one
exception: frames 7 and 23 are each other's mirror and differ by one unit in
`nearWeightDelta` and `pivotYDelta`. Frame 31 is a tail below both ends, easing
the move to a stop. `tests/test_train_ride.cpp` pins the mirror, the one pair
that breaks it, and the peak.

## Where to change things

| To change | Edit |
|---|---|
| Any value in any table here | Nothing in this repository — the values are read out of the cartridge and emitted by `tools/asm_parser/parse_train_data.py`. |
| What a curve item is called | `YAW_TYPES` / `BACKGROUND_TYPES` in `tools/asm_parser/parse_train_data.py`, then regenerate. |
| An accessor's shape or bounds | `src/data/train_ride.h` and `src/data/train_ride.cpp`. |
| The tile or camera-step layout | `include/ostinato/train_tile.h`, `src/data/train_ride.h`. |

Regenerate the tables with:

```
python3 tools/asm_parser/parse_train_data.py --source-root original-src --repo-root .
```

The generated files carry the same command in their own headers. They are not
hand-edited; the parser checks every table against the cartridge before it writes
anything, and stops rather than emitting a value the cartridge disagrees with.

## See also

- [world-cutscenes.md](world-cutscenes.md) — the overworld's other set-piece tables.
- `src/data/world_tiles.h` — the train's graphics-layer geometry and the tile
  offsets derived from it.
