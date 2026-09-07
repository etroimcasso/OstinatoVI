// Full-corpus tests for the world map's set-piece tables: the ending scene's
// expanding circles, Figaro Castle rising and sinking, the strafe bearings, and
// the table the shipped cartridge never reaches. Every row is compared against
// its generated fixture (the cartridge's own words), independent of the typed
// rows. On top of that: the two columns that read as settings are proved
// constant across every circle, the Figaro rows are re-derived from the raw
// words the way the consumer indexes them, and the strafe table's shortfall
// against the mask space the control code forms is pinned both ways.
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "data/world_cutscenes.h"

#include "fixtures/world_cutscenes_expected.h"

namespace {

using namespace ostinato;

// --- the ending airship scene -------------------------------------------------

TEST(WorldCutscenes, EndingSceneCirclesMatchTheCartridge) {
    const auto circles = endingSceneCircles();
    ASSERT_EQ(circles.size(), test::kExpectedEndingSceneCircleCount);
    ASSERT_EQ(test::kExpectedEndingSceneCircles.size(), circles.size() * 6);
    for (std::size_t i = 0; i < circles.size(); ++i) {
        const auto* raw = &test::kExpectedEndingSceneCircles[i * 6];
        EXPECT_EQ(circles[i].index, i);
        EXPECT_EQ(circles[i].x, raw[0]) << "circle " << i;
        EXPECT_EQ(circles[i].y, raw[1]) << "circle " << i;
        EXPECT_EQ(circles[i].radius, raw[2]) << "circle " << i;
        EXPECT_EQ(circles[i].radiusStep, raw[3]) << "circle " << i;
        EXPECT_EQ(circles[i].radiusLimit, raw[4]) << "circle " << i;
        EXPECT_EQ(circles[i].startDelay, raw[5]) << "circle " << i;
    }
}

// Two of the six columns hold the same value on every circle. That is what
// makes them settings the scene shares rather than per-circle data, and a run
// where one disagreed would mean the decode had slipped a column.
TEST(WorldCutscenes, EveryCircleOpensAndGrowsTheSameWay) {
    for (const auto& circle : endingSceneCircles()) {
        EXPECT_EQ(circle.radius, test::kExpectedCircleOpeningRadius)
            << "circle " << static_cast<int>(circle.index);
        EXPECT_EQ(circle.radiusStep, test::kExpectedCircleRadiusStep)
            << "circle " << static_cast<int>(circle.index);
    }
}

// The circles are laid out from the outside in and start in that order, so the
// scene opens wide and closes down.
TEST(WorldCutscenes, TheCirclesCloseInwardAsTheSceneRuns) {
    const auto circles = endingSceneCircles();
    for (std::size_t i = 1; i < circles.size(); ++i) {
        EXPECT_LE(circles[i].x, circles[i - 1].x) << "circle " << i;
        EXPECT_LE(circles[i].y, circles[i - 1].y) << "circle " << i;
        EXPECT_GE(circles[i].startDelay, circles[i - 1].startDelay)
            << "circle " << i;
    }
    // Every circle grows past where it starts, or it would never appear.
    for (const auto& circle : circles) {
        EXPECT_GT(circle.radiusLimit, circle.radius)
            << "circle " << static_cast<int>(circle.index);
    }
}

// How far each circle gets does not follow the same order. The limits fall from
// the first circle to the last, but circle 2 reaches further than circle 1 —
// the one place the sequence steps back up, pinned so it stays visible.
TEST(WorldCutscenes, OneCircleReachesFurtherThanThePrecedingOne) {
    const auto circles = endingSceneCircles();
    EXPECT_GT(circles.front().radiusLimit, circles.back().radiusLimit);

    std::size_t stepsBackUp = 0;
    for (std::size_t i = 1; i < circles.size(); ++i) {
        if (circles[i].radiusLimit > circles[i - 1].radiusLimit) {
            ++stepsBackUp;
            EXPECT_EQ(i, 2u) << "circle " << i << " reaches further than "
                             << (i - 1);
        }
    }
    EXPECT_EQ(stepsBackUp, 1u);
}

// --- Figaro Castle ------------------------------------------------------------

// The block is copied a word at a time but read a byte at a time, at a
// six-byte stride. Re-deriving the rows from the raw words the way the consumer
// walks them proves the port splits them where the original does.
TEST(WorldCutscenes, FigaroCastlePiecesMatchTheCartridge) {
    const auto pieces = figaroCastlePieces();
    ASSERT_EQ(pieces.size(), test::kExpectedFigaroPieceCount);

    std::uint8_t bytes[6 * 6] = {};
    ASSERT_EQ(test::kExpectedFigaroCastle.size() * 2, sizeof(bytes));
    for (std::size_t w = 0; w < test::kExpectedFigaroCastle.size(); ++w) {
        bytes[w * 2] =
            static_cast<std::uint8_t>(test::kExpectedFigaroCastle[w] & 0xFF);
        bytes[w * 2 + 1] =
            static_cast<std::uint8_t>(test::kExpectedFigaroCastle[w] >> 8);
    }

    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const std::uint8_t* raw = &bytes[i * 6];
        EXPECT_EQ(pieces[i].index, i);
        EXPECT_EQ(pieces[i].x, raw[0]) << "piece " << i;
        EXPECT_EQ(pieces[i].y, raw[1]) << "piece " << i;
        EXPECT_EQ(pieces[i].animationStep, raw[2]) << "piece " << i;
        EXPECT_EQ(pieces[i].phase, raw[3]) << "piece " << i;
        EXPECT_EQ(pieces[i].phaseStep, raw[4]) << "piece " << i;
        EXPECT_EQ(pieces[i].xOffset, raw[5]) << "piece " << i;
    }
}

// A hand-traced piece: the first row of world/event.asm:2264 is $876d, $0000,
// $0040, which the consumer reads as six separate bytes.
TEST(WorldCutscenes, TheFirstCastlePieceDecodesAsTraced) {
    const auto piece = figaroCastlePieces()[0];
    EXPECT_EQ(piece.x, 0x6D);
    EXPECT_EQ(piece.y, 0x87);
    EXPECT_EQ(piece.animationStep, 0x00);
    EXPECT_EQ(piece.phase, 0x00);
    EXPECT_EQ(piece.phaseStep, 0x40);
    EXPECT_EQ(piece.xOffset, 0x00);
}

// Every piece steps within the four-frame cycle the consumer wraps into, every
// piece actually moves, and none starts with a jitter of its own — the wave
// supplies that once the castle is under way.
TEST(WorldCutscenes, EveryCastlePieceStartsWithinItsAnimationCycle) {
    for (const auto& piece : figaroCastlePieces()) {
        EXPECT_LT(piece.animationStep, 4)
            << "piece " << static_cast<int>(piece.index);
        EXPECT_GT(piece.phaseStep, 0)
            << "piece " << static_cast<int>(piece.index);
        EXPECT_EQ(piece.xOffset, 0)
            << "piece " << static_cast<int>(piece.index);
    }
}

// --- strafing -----------------------------------------------------------------

TEST(WorldCutscenes, StrafeAnglesMatchTheCartridge) {
    const auto angles = strafeAngles();
    ASSERT_EQ(angles.size(), test::kExpectedStrafeAngles.size());
    for (std::size_t i = 0; i < angles.size(); ++i) {
        EXPECT_EQ(angles[i].index, i);
        EXPECT_EQ(angles[i].degrees, test::kExpectedStrafeAngles[i])
            << "mask " << i;
        EXPECT_LT(angles[i].degrees, 360) << "mask " << i;
    }
}

// The index is up, down, left, right in bit order. Tracing the single-direction
// masks and the diagonals proves the bearings are laid out the way the mask is
// read, rather than in some other order that happens to fit.
TEST(WorldCutscenes, TheBearingsFollowTheDirectionBits) {
    constexpr std::uint8_t kUp = 8;
    constexpr std::uint8_t kDown = 4;
    constexpr std::uint8_t kLeft = 2;
    constexpr std::uint8_t kRight = 1;

    EXPECT_EQ(strafeAngle(kUp), 0);
    EXPECT_EQ(strafeAngle(kRight), 270);
    EXPECT_EQ(strafeAngle(kDown), 180);
    EXPECT_EQ(strafeAngle(kLeft), 90);

    EXPECT_EQ(strafeAngle(kUp | kRight), 315);
    EXPECT_EQ(strafeAngle(kDown | kRight), 225);
    EXPECT_EQ(strafeAngle(kDown | kLeft), 135);
    EXPECT_EQ(strafeAngle(kUp | kLeft), 45);

    // Each diagonal sits halfway between the two directions that make it.
    EXPECT_EQ(strafeAngle(kDown | kRight),
              (strafeAngle(kDown).value() + strafeAngle(kRight).value()) / 2);
    EXPECT_EQ(strafeAngle(kDown | kLeft),
              (strafeAngle(kDown).value() + strafeAngle(kLeft).value()) / 2);

    // Masks holding two opposed directions carry no bearing.
    EXPECT_EQ(strafeAngle(0), 0);
    EXPECT_EQ(strafeAngle(kLeft | kRight), 0);
    EXPECT_EQ(strafeAngle(kDown | kLeft | kRight), 0);
}

// The control code masks four direction bits and indexes with all sixteen
// results, but the table stops after eleven. The five masks it cannot reach are
// pinned so the shortfall stays visible; see docs/Bugs.md.
TEST(WorldCutscenes, TheStrafeTableStopsShortOfTheMasksTheCodeCanForm) {
    ASSERT_EQ(strafeAngles().size(), kStrafeAngleCount);
    ASSERT_LT(kStrafeAngleCount, kStrafeDirectionMasks);

    for (std::uint8_t mask = 0; mask < kStrafeDirectionMasks; ++mask) {
        const bool covered = mask < kStrafeAngleCount;
        EXPECT_EQ(strafeAngle(mask).has_value(), covered)
            << "mask " << static_cast<int>(mask);
        EXPECT_EQ(strafeMaskReadsPastTheTable(mask), !covered)
            << "mask " << static_cast<int>(mask);
    }

    for (const auto mask : test::kExpectedStrafeMasksWithoutAnEntry) {
        EXPECT_FALSE(strafeAngle(mask).has_value())
            << "mask " << static_cast<int>(mask);
    }
    EXPECT_EQ(test::kExpectedStrafeMasksWithoutAnEntry.size(),
              kStrafeDirectionMasks - kStrafeAngleCount);
}

// --- unreached ----------------------------------------------------------------

TEST(WorldCutscenes, TheUnreachedWordsMatchTheCartridge) {
    const auto words = unusedCutsceneWords();
    ASSERT_EQ(words.size(), test::kExpectedUnusedCutsceneWords.size());
    for (std::size_t i = 0; i < words.size(); ++i) {
        EXPECT_EQ(words[i].index, i);
        EXPECT_EQ(words[i].value, test::kExpectedUnusedCutsceneWords[i])
            << "word " << i;
    }
}

}  // namespace
