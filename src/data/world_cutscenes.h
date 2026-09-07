// The world map's set-piece data: the ending scene's expanding circles, Figaro
// Castle rising and sinking, the strafe bearings the control code reads, and
// one table the shipped cartridge never reaches. The row data is generated
// (src/data/generated/*.inc); this header owns the entry types and accessors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ostinato {

// --- the ending airship scene -------------------------------------------------

inline constexpr std::size_t kEndingSceneCircleCount = 9;

// One expanding circle of the ending airship scene.
//
// A circle waits out its start delay, then grows by `radiusStep` every frame
// until it reaches `radiusLimit` and holds there (world/cutscene.asm:948-990).
// Every circle opens at the same radius and grows at the same rate; only where
// each one sits, how far it gets, and when it begins differ.
struct EndingSceneCircle {
    std::uint8_t index;
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t radius;
    std::uint16_t radiusStep;
    std::uint16_t radiusLimit;
    std::uint16_t startDelay;
};

std::span<const EndingSceneCircle> endingSceneCircles();

// --- Figaro Castle ------------------------------------------------------------

inline constexpr std::size_t kFigaroCastlePieceCount = 6;

// One moving piece of Figaro Castle rising out of the desert or sinking back
// into it. Both directions run off this one table (world/event.asm:2153,
// :2275).
//
// A piece advances `phase` by `phaseStep` every frame; when that wraps it steps
// to its next animation frame and takes a fresh `xOffset` from the world's own
// wave, so the castle shudders as it moves (world/event.asm:2214-2227).
struct FigaroCastlePiece {
    std::uint8_t index;
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t animationStep;
    std::uint8_t phase;
    std::uint8_t phaseStep;
    std::uint8_t xOffset;
};

std::span<const FigaroCastlePiece> figaroCastlePieces();

// --- strafing -----------------------------------------------------------------

// Bearings the table carries, and the number of direction masks the control
// code can actually form.
inline constexpr std::size_t kStrafeAngleCount = 11;
inline constexpr std::size_t kStrafeDirectionMasks = 16;

// The bearing the world map strafes along while the Y button is held, for one
// combination of directions.
//
// The index is a bitmask — up, down, left, right in bit order — so index 6 is
// down and left, and a mask holding two opposed directions carries no bearing.
struct StrafeAngleEntry {
    std::uint16_t index;
    std::uint16_t degrees;
};

std::span<const StrafeAngleEntry> strafeAngles();

// The bearing for a direction mask, or nothing when the table does not reach
// that far.
//
// The control code masks four direction bits and indexes with all sixteen
// results, but the table stops after eleven, so five masks read past its end;
// see docs/Bugs.md "World-map strafing reads past the end of its bearing
// table" before changing anything that calls this. Callers decide what to do
// with an uncovered mask; the port does not invent a bearing for one.
std::optional<std::uint16_t> strafeAngle(std::uint8_t directionMask);

// True when a direction mask falls past the table's last entry.
bool strafeMaskReadsPastTheTable(std::uint8_t directionMask);

// --- unreached ----------------------------------------------------------------

inline constexpr std::size_t kUnusedCutsceneWordCount = 48;

// One word of the table the shipped cartridge reaches from nowhere.
struct UnusedCutsceneEntry {
    std::uint8_t index;
    std::uint16_t value;
};

// Forty-eight words sitting among the world cutscene routines with no consumer
// anywhere in the original. Nothing in the source says what they mean, so they
// are carried as raw values and named nothing.
std::span<const UnusedCutsceneEntry> unusedCutsceneWords();

}  // namespace ostinato
