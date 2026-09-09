// The Magitek train ride's data: the curves that steer the tunnel, the tile
// arrangements that dress it, and the camera move the airship takes off and
// lands with. The row data is generated (src/data/generated/*.inc); this header
// owns the entry types and accessors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/train_background_type.h"
#include "ostinato/train_tile.h"
#include "ostinato/train_yaw_type.h"

namespace ostinato {

// Steps in one curve item. The ride's script names an item per curve and the
// ride then walks its 32 steps, one per frame (world/train_script.asm:370-420).
inline constexpr std::size_t kTrainCurveItemSteps = 32;

// Items in each of the selector-indexed curves.
inline constexpr std::size_t kTrainPitchItems = 13;
inline constexpr std::size_t kTrainYawItems = 3;
inline constexpr std::size_t kTrainBackgroundItems = 5;

// The yaw multiplier is a single curve. The script's selector for it is zero on
// every step of every course, so no second item exists to reach.
inline constexpr std::size_t kTrainYawMultiplierItems = 1;

// Tunnel scenery: how many arrangements exist and how many tiles each places.
inline constexpr std::size_t kTrainBackgroundVariants = 20;
inline constexpr std::size_t kTrainTilesPerVariant = 12;

// Steps in the two curves that are read whole rather than by item.
inline constexpr std::size_t kTrainPositionMultiplierSteps = 64;
inline constexpr std::size_t kTrainRotationAngleCount = 8;

// Frames in the airship's takeoff and landing camera move.
inline constexpr std::size_t kAirshipLiftoffStepCount = 32;

// --- entry types --------------------------------------------------------------

// One step of a train curve whose values are signed offsets.
struct TrainCurveEntry {
    std::uint16_t index;
    std::int8_t value;
};

// One step of a train curve whose values are magnitudes or selectors.
struct TrainStepEntry {
    std::uint16_t index;
    std::uint8_t value;
};

// One tile slot of a tunnel arrangement: the flattened slot, and the tile.
struct TrainTileEntry {
    std::uint16_t index;
    TrainTile tile;
};

// One frame of the airship's takeoff and landing camera move.
//
// The world map's ground plane is a projective transform, and these are the
// per-frame steps that animate it: `scaleDelta` moves the transform's scale,
// `nearWeightDelta` and `farWeightDelta` move the projective weight at the near
// and far edges of the drawn area, `horizonDelta` moves where the plane starts,
// and `pivotYDelta` moves the point it turns about. Taking off subtracts each
// one and lands walks the same rows backward adding them
// (world/liftoff.asm:129-169, :260-300).
//
// The original rebuilt this as a per-scanline divide because its hardware had
// no other way to foreshorten. A transform carries the foreshortening in its
// own coefficients, so the port applies one transform per frame rather than a
// table of one value per line.
struct AirshipLiftoffStep {
    std::uint8_t index;
    std::uint16_t scaleDelta;
    std::uint16_t nearWeightDelta;
    std::uint8_t farWeightDelta;
    std::uint8_t horizonDelta;
    std::uint8_t pivotYDelta;
};

// --- steering curves ----------------------------------------------------------

// Whole curves, flattened and indexed item * kTrainCurveItemSteps + step.
std::span<const TrainCurveEntry> trainPitchData1();
std::span<const TrainCurveEntry> trainPitchData2();
std::span<const TrainCurveEntry> trainYawData();
std::span<const TrainCurveEntry> trainYawMultiplier();
std::span<const TrainStepEntry> trainBackgroundData();

// One item's 32 steps. The ride reads the two pitch curves together under one
// selector, so they take the same item (world/train_script.asm:358-374).
std::span<const TrainCurveEntry> trainPitchCurve1(std::uint8_t item);
std::span<const TrainCurveEntry> trainPitchCurve2(std::uint8_t item);
std::span<const TrainCurveEntry> trainYawCurve(TrainYawType type);
std::span<const TrainStepEntry> trainBackgroundCurve(TrainBackgroundType type);

// --- falloff ------------------------------------------------------------------

// How much of the tunnel's pitch and yaw offset reaches a given row — the
// falloff that makes the track recede. One curve, read at two different offsets
// into itself, and both readings are the original's.

// The falloff for one step of the vertical sweep, steps 1 through 24. The sweep
// reads 39 entries into the curve (world/train_script.asm:76).
inline constexpr std::size_t kTrainSweepFalloffOffset = 39;
inline constexpr std::size_t kTrainSweepSteps = 24;
std::uint8_t trainSweepFalloff(std::uint8_t step);

// The falloff for one graphics layer, layers 1 through 22. The layer walk reads
// one entry earlier, so layer 1 lands on the curve's first step
// (world/train_script.asm:161).
inline constexpr std::size_t kTrainFalloffLayers = 22;
std::uint8_t trainLayerFalloff(std::uint8_t layer);

std::span<const TrainStepEntry> trainPositionMultipliers();

// --- tunnel scenery -----------------------------------------------------------

// Every arrangement's tiles, flattened and indexed
// variant * kTrainTilesPerVariant + slot.
std::span<const TrainTileEntry> trainTiles();

// One arrangement's twelve tiles: five left wall, five right wall, one ceiling,
// one rail (world/train_script.asm:549).
std::span<const TrainTileEntry> trainTileArrangement(std::uint8_t variant);

// --- roll ---------------------------------------------------------------------

// The angles a script command can roll the train through, in degrees. The
// command carries a three-bit index into this curve
// (world/train_script.asm:815-820).
std::span<const TrainStepEntry> trainRotationAngles();
std::uint8_t trainRotationAngle(std::uint8_t index);

// --- airship camera -----------------------------------------------------------

// The takeoff and landing camera move, one entry per frame.
std::span<const AirshipLiftoffStep> airshipLiftoffSteps();

}  // namespace ostinato
