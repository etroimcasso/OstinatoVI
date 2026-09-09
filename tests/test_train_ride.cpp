// Full-corpus tests for the Magitek train ride's data and the airship's
// takeoff/landing camera move. Every curve, every tunnel arrangement and every
// camera frame is compared entry-by-entry against its generated fixture (the
// cartridge's own bytes), independent of the typed rows, so any decode or
// re-emit drift fails loudly. On top of that: the selector arithmetic each
// consumer performs is exercised at its real bounds, both readings of the
// falloff curve are traced to the values their consumers see, the tunnel
// builder's empty-slot test is checked on every slot, and the one pair of
// camera frames that breaks the curve's mirror is pinned by name.
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "data/train_ride.h"
#include "ostinato/train_background_type.h"
#include "ostinato/train_tile.h"
#include "ostinato/train_yaw_type.h"

#include "fixtures/train_ride_expected.h"

namespace {

using namespace ostinato;

std::int8_t signedByte(std::uint8_t raw) {
    return static_cast<std::int8_t>(raw);
}

// --- the steering curves ------------------------------------------------------

TEST(TrainRide, PitchCurvesMatchTheCartridge) {
    const auto first = trainPitchData1();
    const auto second = trainPitchData2();
    ASSERT_EQ(first.size(), test::kExpectedTrainPitchData1.size());
    ASSERT_EQ(second.size(), test::kExpectedTrainPitchData2.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].index, i);
        EXPECT_EQ(first[i].value, signedByte(test::kExpectedTrainPitchData1[i]))
            << "pitch 1 step " << i;
        EXPECT_EQ(second[i].index, i);
        EXPECT_EQ(second[i].value, signedByte(test::kExpectedTrainPitchData2[i]))
            << "pitch 2 step " << i;
    }
}

TEST(TrainRide, YawCurvesMatchTheCartridge) {
    const auto yaw = trainYawData();
    ASSERT_EQ(yaw.size(), test::kExpectedTrainYawData.size());
    for (std::size_t i = 0; i < yaw.size(); ++i) {
        EXPECT_EQ(yaw[i].index, i);
        EXPECT_EQ(yaw[i].value, signedByte(test::kExpectedTrainYawData[i]))
            << "yaw step " << i;
    }

    const auto multiplier = trainYawMultiplier();
    ASSERT_EQ(multiplier.size(), test::kExpectedTrainYawMultiplier.size());
    for (std::size_t i = 0; i < multiplier.size(); ++i) {
        EXPECT_EQ(multiplier[i].index, i);
        EXPECT_EQ(multiplier[i].value,
                  signedByte(test::kExpectedTrainYawMultiplier[i]))
            << "yaw multiplier step " << i;
    }
}

TEST(TrainRide, BackgroundCurveMatchesTheCartridge) {
    const auto background = trainBackgroundData();
    ASSERT_EQ(background.size(), test::kExpectedTrainBackgroundData.size());
    for (std::size_t i = 0; i < background.size(); ++i) {
        EXPECT_EQ(background[i].index, i);
        EXPECT_EQ(background[i].value, test::kExpectedTrainBackgroundData[i])
            << "background step " << i;
    }
}

// The curves hold whole items back to back, and each consumer reaches an item
// by multiplying its script byte by the item size. Walking every item of every
// curve proves the port slices where the original's arithmetic lands.
TEST(TrainRide, EveryCurveItemSlicesWhereTheSelectorLands) {
    for (std::size_t item = 0; item < kTrainPitchItems; ++item) {
        const auto slice = trainPitchCurve1(static_cast<std::uint8_t>(item));
        ASSERT_EQ(slice.size(), kTrainCurveItemSteps);
        for (std::size_t step = 0; step < slice.size(); ++step) {
            const std::size_t flat = item * kTrainCurveItemSteps + step;
            EXPECT_EQ(slice[step].index, flat);
            EXPECT_EQ(slice[step].value,
                      signedByte(test::kExpectedTrainPitchData1[flat]));
        }
        const auto other = trainPitchCurve2(static_cast<std::uint8_t>(item));
        ASSERT_EQ(other.size(), kTrainCurveItemSteps);
        EXPECT_EQ(other.front().index, item * kTrainCurveItemSteps);
    }

    const TrainYawType yawTypes[] = {TrainYawType::STRAIGHT,
                                     TrainYawType::LEFT_TURN,
                                     TrainYawType::RIGHT_TURN};
    for (std::size_t i = 0; i < kTrainYawItems; ++i) {
        const auto slice = trainYawCurve(yawTypes[i]);
        ASSERT_EQ(slice.size(), kTrainCurveItemSteps);
        EXPECT_EQ(slice.front().index, i * kTrainCurveItemSteps);
    }

    const TrainBackgroundType backgrounds[] = {
        TrainBackgroundType::PIPES_MORE_BEAMS,
        TrainBackgroundType::PIPES_FEWER_BEAMS,
        TrainBackgroundType::NO_PIPES,
        TrainBackgroundType::SPLIT_TRACK_MORE_WALLS,
        TrainBackgroundType::SPLIT_TRACK_FEWER_WALLS};
    for (std::size_t i = 0; i < kTrainBackgroundItems; ++i) {
        const auto slice = trainBackgroundCurve(backgrounds[i]);
        ASSERT_EQ(slice.size(), kTrainCurveItemSteps);
        EXPECT_EQ(slice.front().index, i * kTrainCurveItemSteps);
    }
}

// The straight-track curve is flat and the two turns mirror each other. Traced
// from the corpus's own item names (world/train_script.asm:513-515).
TEST(TrainRide, TheYawCurvesTurnTheWayTheyAreNamed) {
    for (const auto& step : trainYawCurve(TrainYawType::STRAIGHT)) {
        EXPECT_EQ(step.value, 0) << "straight track step " << step.index;
    }

    const auto left = trainYawCurve(TrainYawType::LEFT_TURN);
    const auto right = trainYawCurve(TrainYawType::RIGHT_TURN);
    ASSERT_EQ(left.size(), right.size());
    EXPECT_GT(left[15].value, 0);
    EXPECT_LT(right[15].value, 0);
}

// Every background step names a tunnel arrangement that exists. The builder
// turns the value straight into an offset, so one past the end would dress the
// tunnel with another table's bytes.
TEST(TrainRide, EveryBackgroundStepNamesAnArrangement) {
    for (const auto& step : trainBackgroundData()) {
        EXPECT_LT(step.value, kTrainBackgroundVariants)
            << "background step " << step.index;
    }
}

// --- falloff ------------------------------------------------------------------

TEST(TrainRide, PositionMultipliersMatchTheCartridge) {
    const auto rows = trainPositionMultipliers();
    ASSERT_EQ(rows.size(), test::kExpectedTrainPositionMultipliers.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].index, i);
        EXPECT_EQ(rows[i].value, test::kExpectedTrainPositionMultipliers[i])
            << "falloff step " << i;
    }
}

// The one curve is read at two different offsets into itself and both readings
// are the original's. Walking each at its real bounds pins that the port keeps
// them apart rather than normalising them to one base.
TEST(TrainRide, BothFalloffReadingsLandWhereTheOriginalsDo) {
    // The offsets are written out rather than taken from the header, so this
    // pins where the reads land instead of restating whatever the header says.
    // The sweep reads 39 entries in; the layer walk reads one entry back.
    for (std::uint8_t step = 1; step <= kTrainSweepSteps; ++step) {
        EXPECT_EQ(trainSweepFalloff(step),
                  test::kExpectedTrainPositionMultipliers[39 + step])
            << "sweep step " << static_cast<int>(step);
    }
    for (std::uint8_t layer = 1; layer <= kTrainFalloffLayers; ++layer) {
        EXPECT_EQ(trainLayerFalloff(layer),
                  test::kExpectedTrainPositionMultipliers[layer - 1U])
            << "layer " << static_cast<int>(layer);
    }
    EXPECT_EQ(kTrainSweepFalloffOffset, 39u);

    // Both walks end inside the curve rather than off it.
    EXPECT_EQ(39 + kTrainSweepSteps, test::kExpectedTrainPositionMultipliers
                                         .size() - 1);
    EXPECT_LT(kTrainFalloffLayers, test::kExpectedTrainPositionMultipliers
                                       .size());

    // The two readings genuinely differ: the sweep starts deep in the curve
    // where it has already fallen off, the layer walk at the very top.
    EXPECT_NE(trainSweepFalloff(1), trainLayerFalloff(1));
}

// --- tunnel scenery -----------------------------------------------------------

TEST(TrainRide, TunnelTilesMatchTheCartridge) {
    const auto tiles = trainTiles();
    ASSERT_EQ(tiles.size(), kTrainBackgroundVariants * kTrainTilesPerVariant);
    ASSERT_EQ(test::kExpectedTrainTiles.size(), tiles.size() * 4);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        EXPECT_EQ(tiles[i].index, i);
        EXPECT_EQ(tiles[i].tile.x, test::kExpectedTrainTiles[i * 4 + 0])
            << "tile " << i;
        EXPECT_EQ(tiles[i].tile.y, test::kExpectedTrainTiles[i * 4 + 1])
            << "tile " << i;
        EXPECT_EQ(tiles[i].tile.tileIndexLow,
                  test::kExpectedTrainTiles[i * 4 + 2]) << "tile " << i;
        EXPECT_EQ(tiles[i].tile.tileIndexHigh,
                  test::kExpectedTrainTiles[i * 4 + 3]) << "tile " << i;
    }
}

// The builder tests the leading two bytes and draws nothing when they are zero.
// Checking the predicate against that test on every one of the 240 slots proves
// the port empties exactly the slots the original empties.
TEST(TrainRide, TheEmptySlotTestMatchesTheBuildersOwn) {
    const auto tiles = trainTiles();
    std::size_t empty = 0;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const bool builderSkips = test::kExpectedTrainTiles[i * 4 + 0] == 0 &&
                                  test::kExpectedTrainTiles[i * 4 + 1] == 0;
        EXPECT_EQ(tiles[i].tile.unused(), builderSkips) << "tile " << i;
        if (builderSkips) {
            ++empty;
        }
    }
    // The tunnel is mostly empty slots; a run that found none would mean the
    // predicate had stopped reading the right bytes.
    EXPECT_GT(empty, 0u);
    EXPECT_LT(empty, tiles.size());
}

TEST(TrainRide, EveryArrangementSlicesTwelveTiles) {
    for (std::size_t variant = 0; variant < kTrainBackgroundVariants;
         ++variant) {
        const auto slice =
            trainTileArrangement(static_cast<std::uint8_t>(variant));
        ASSERT_EQ(slice.size(), kTrainTilesPerVariant);
        EXPECT_EQ(slice.front().index, variant * kTrainTilesPerVariant);
        EXPECT_EQ(slice.back().index,
                  variant * kTrainTilesPerVariant + kTrainTilesPerVariant - 1);
    }
}

// A hand-traced slot: arrangement 1's first wall tile is the four bytes at
// world/train_script.asm:555, and its graphics tile is the two of them read
// together.
TEST(TrainRide, TheFirstWallTileOfArrangementOneDecodesAsTraced) {
    const auto tile = trainTileArrangement(1)[0].tile;
    EXPECT_EQ(tile.x, 0x1C);
    EXPECT_EQ(tile.y, 0x20);
    EXPECT_EQ(tile.tileIndex(), 0x0000);
    EXPECT_FALSE(tile.unused());
}

TEST(TrainRide, TheTileIndexReadsBothStoredBytes) {
    constexpr TrainTile tile{
        .x = 0x64, .y = 0x20, .tileIndexLow = 0x98, .tileIndexHigh = 0x01};
    static_assert(tile.tileIndex() == 0x0198);
    static_assert(!tile.unused());
    static_assert(TrainTile{}.unused());
    EXPECT_EQ(tile.tileIndex(), 0x0198);
}

// --- roll ---------------------------------------------------------------------

TEST(TrainRide, RollAnglesMatchTheCartridge) {
    const auto angles = trainRotationAngles();
    ASSERT_EQ(angles.size(), test::kExpectedTrainRotationAngles.size());
    for (std::size_t i = 0; i < angles.size(); ++i) {
        EXPECT_EQ(angles[i].index, i);
        EXPECT_EQ(angles[i].value, test::kExpectedTrainRotationAngles[i]);
        EXPECT_EQ(trainRotationAngle(static_cast<std::uint8_t>(i)),
                  test::kExpectedTrainRotationAngles[i]);
    }
    // The command carries a three-bit index, so the curve holds exactly the
    // eight values that index can reach.
    EXPECT_EQ(angles.size(), 8u);
}

// --- the airship camera -------------------------------------------------------

TEST(TrainRide, AirshipCameraStepsMatchTheCartridge) {
    const auto steps = airshipLiftoffSteps();
    ASSERT_EQ(steps.size(), test::kExpectedAirshipLiftoffSteps);
    ASSERT_EQ(test::kExpectedAirshipLiftoff.size(), steps.size() * 7);
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto* raw = &test::kExpectedAirshipLiftoff[i * 7];
        EXPECT_EQ(steps[i].index, i);
        EXPECT_EQ(steps[i].scaleDelta,
                  static_cast<std::uint16_t>(raw[0] | (raw[1] << 8)))
            << "camera frame " << i;
        EXPECT_EQ(steps[i].nearWeightDelta,
                  static_cast<std::uint16_t>(raw[2] | (raw[3] << 8)))
            << "camera frame " << i;
        EXPECT_EQ(steps[i].farWeightDelta, raw[4]) << "camera frame " << i;
        EXPECT_EQ(steps[i].horizonDelta, raw[5]) << "camera frame " << i;
        EXPECT_EQ(steps[i].pivotYDelta, raw[6]) << "camera frame " << i;
    }
}

// The move rises to a peak and comes back down the way it went up — except for
// one pair of frames, which differ by a single unit in two fields. Both halves
// are pinned: the mirror everywhere else, and the one place it breaks.
TEST(TrainRide, TheCameraMoveMirrorsAboutItsPeakExceptForOnePair) {
    const auto steps = airshipLiftoffSteps();
    const std::size_t low = test::kExpectedLiftoffAsymmetricLow;
    const std::size_t high = test::kExpectedLiftoffAsymmetricHigh;

    std::size_t differingPairs = 0;
    for (std::size_t i = 0; i < 15; ++i) {
        const std::size_t j = 30 - i;
        const bool same = steps[i].scaleDelta == steps[j].scaleDelta &&
                          steps[i].nearWeightDelta == steps[j].nearWeightDelta &&
                          steps[i].farWeightDelta == steps[j].farWeightDelta &&
                          steps[i].horizonDelta == steps[j].horizonDelta &&
                          steps[i].pivotYDelta == steps[j].pivotYDelta;
        if (!same) {
            ++differingPairs;
            EXPECT_EQ(i, low) << "frame " << i << " breaks the mirror";
            EXPECT_EQ(j, high) << "frame " << j << " breaks the mirror";
        }
    }
    EXPECT_EQ(differingPairs, 1u);

    // The pair differs only in the near weight and the pivot, by one each.
    EXPECT_EQ(steps[high].nearWeightDelta - steps[low].nearWeightDelta, 1);
    EXPECT_EQ(steps[high].pivotYDelta - steps[low].pivotYDelta, 1);
    EXPECT_EQ(steps[low].scaleDelta, steps[high].scaleDelta);
    EXPECT_EQ(steps[low].farWeightDelta, steps[high].farWeightDelta);
    EXPECT_EQ(steps[low].horizonDelta, steps[high].horizonDelta);
}

// The move accelerates away from rest and holds its largest step at the peak.
TEST(TrainRide, TheCameraMoveGrowsToItsPeakAndBack) {
    const auto steps = airshipLiftoffSteps();
    for (std::size_t i = 1; i <= 15; ++i) {
        EXPECT_GE(steps[i].scaleDelta, steps[i - 1].scaleDelta)
            << "camera frame " << i;
    }
    EXPECT_EQ(steps[15].scaleDelta, 0x0600);
    EXPECT_EQ(steps[15].nearWeightDelta, 0x0780);
    // The last frame is a tail the mirror does not reach; it is the smallest
    // step of all, easing the move to a stop.
    EXPECT_LT(steps[31].scaleDelta, steps[0].scaleDelta);
}

}  // namespace
