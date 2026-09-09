#include "data/train_ride.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The two pitch curves, read together under one selector.
constexpr std::array<TrainCurveEntry,
                     kTrainPitchItems * kTrainCurveItemSteps>
    kTrainPitchData1 = {{
#include "data/generated/train_pitch_data_1_data.inc"
}};

constexpr std::array<TrainCurveEntry,
                     kTrainPitchItems * kTrainCurveItemSteps>
    kTrainPitchData2 = {{
#include "data/generated/train_pitch_data_2_data.inc"
}};

constexpr std::array<TrainCurveEntry, kTrainYawItems * kTrainCurveItemSteps>
    kTrainYawData = {{
#include "data/generated/train_yaw_data.inc"
}};

constexpr std::array<TrainCurveEntry,
                     kTrainYawMultiplierItems * kTrainCurveItemSteps>
    kTrainYawMultiplier = {{
#include "data/generated/train_yaw_multiplier_data.inc"
}};

constexpr std::array<TrainStepEntry,
                     kTrainBackgroundItems * kTrainCurveItemSteps>
    kTrainBackgroundData = {{
#include "data/generated/train_background_data.inc"
}};

constexpr std::array<TrainStepEntry, kTrainPositionMultiplierSteps>
    kTrainPositionMultipliers = {{
#include "data/generated/train_position_multiplier_data.inc"
}};

constexpr std::array<TrainStepEntry, kTrainRotationAngleCount>
    kTrainRotationAngles = {{
#include "data/generated/train_rotation_angle_data.inc"
}};

constexpr std::array<TrainTileEntry,
                     kTrainBackgroundVariants * kTrainTilesPerVariant>
    kTrainTiles = {{
#include "data/generated/train_tile_data.inc"
}};

constexpr std::array<AirshipLiftoffStep, kAirshipLiftoffStepCount>
    kAirshipLiftoffSteps = {{
#include "data/generated/airship_liftoff_data.inc"
}};

template <typename Table>
constexpr bool indexMatchesPosition(const Table& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].index != i) {
            return false;
        }
    }
    return true;
}

static_assert(indexMatchesPosition(kTrainPitchData1));
static_assert(indexMatchesPosition(kTrainPitchData2));
static_assert(indexMatchesPosition(kTrainYawData));
static_assert(indexMatchesPosition(kTrainYawMultiplier));
static_assert(indexMatchesPosition(kTrainBackgroundData));
static_assert(indexMatchesPosition(kTrainPositionMultipliers));
static_assert(indexMatchesPosition(kTrainRotationAngles));
static_assert(indexMatchesPosition(kTrainTiles));
static_assert(indexMatchesPosition(kAirshipLiftoffSteps));

// Every step of the background curves names a tunnel arrangement that exists.
// The tunnel builder turns the value straight into an offset, so a value past
// the end would dress the tunnel with another table's bytes.
constexpr bool backgroundStepsNameAnArrangement() {
    for (const auto& step : kTrainBackgroundData) {
        if (step.value >= kTrainBackgroundVariants) {
            return false;
        }
    }
    return true;
}

static_assert(backgroundStepsNameAnArrangement(),
              "every background step must name a tunnel arrangement");

// One item's slice of a flattened curve.
template <typename Entry, typename Table>
std::span<const Entry> curveItem(const Table& table, std::size_t item,
                                 std::size_t items) {
    assert(item < items && "curve item out of range");
    (void)items;
    return std::span<const Entry>(table).subspan(item * kTrainCurveItemSteps,
                                                 kTrainCurveItemSteps);
}

}  // namespace

std::span<const TrainCurveEntry> trainPitchData1() { return kTrainPitchData1; }
std::span<const TrainCurveEntry> trainPitchData2() { return kTrainPitchData2; }
std::span<const TrainCurveEntry> trainYawData() { return kTrainYawData; }

std::span<const TrainCurveEntry> trainYawMultiplier() {
    return kTrainYawMultiplier;
}

std::span<const TrainStepEntry> trainBackgroundData() {
    return kTrainBackgroundData;
}

std::span<const TrainCurveEntry> trainPitchCurve1(std::uint8_t item) {
    return curveItem<TrainCurveEntry>(kTrainPitchData1, item, kTrainPitchItems);
}

std::span<const TrainCurveEntry> trainPitchCurve2(std::uint8_t item) {
    return curveItem<TrainCurveEntry>(kTrainPitchData2, item, kTrainPitchItems);
}

std::span<const TrainCurveEntry> trainYawCurve(TrainYawType type) {
    return curveItem<TrainCurveEntry>(kTrainYawData,
                                      static_cast<std::size_t>(type),
                                      kTrainYawItems);
}

std::span<const TrainStepEntry> trainBackgroundCurve(TrainBackgroundType type) {
    return curveItem<TrainStepEntry>(kTrainBackgroundData,
                                     static_cast<std::size_t>(type),
                                     kTrainBackgroundItems);
}

std::span<const TrainStepEntry> trainPositionMultipliers() {
    return kTrainPositionMultipliers;
}

std::uint8_t trainSweepFalloff(std::uint8_t step) {
    assert(step >= 1 && step <= kTrainSweepSteps &&
           "sweep step out of range");
    return kTrainPositionMultipliers[kTrainSweepFalloffOffset + step].value;
}

std::uint8_t trainLayerFalloff(std::uint8_t layer) {
    assert(layer >= 1 && layer <= kTrainFalloffLayers &&
           "falloff layer out of range");
    return kTrainPositionMultipliers[layer - 1U].value;
}

std::span<const TrainTileEntry> trainTiles() { return kTrainTiles; }

std::span<const TrainTileEntry> trainTileArrangement(std::uint8_t variant) {
    assert(variant < kTrainBackgroundVariants &&
           "tunnel arrangement out of range");
    return std::span<const TrainTileEntry>(kTrainTiles)
        .subspan(static_cast<std::size_t>(variant) * kTrainTilesPerVariant,
                 kTrainTilesPerVariant);
}

std::span<const TrainStepEntry> trainRotationAngles() {
    return kTrainRotationAngles;
}

std::uint8_t trainRotationAngle(std::uint8_t index) {
    assert(index < kTrainRotationAngleCount && "roll angle out of range");
    return kTrainRotationAngles[index].value;
}

std::span<const AirshipLiftoffStep> airshipLiftoffSteps() {
    return kAirshipLiftoffSteps;
}

}  // namespace ostinato
