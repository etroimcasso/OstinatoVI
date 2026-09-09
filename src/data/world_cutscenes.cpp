#include "data/world_cutscenes.h"

#include <array>
#include <cstddef>

namespace ostinato {

namespace {

constexpr std::array<EndingSceneCircle, kEndingSceneCircleCount>
    kEndingSceneCircles = {{
#include "data/generated/ending_scene_circle_data.inc"
}};

constexpr std::array<FigaroCastlePiece, kFigaroCastlePieceCount>
    kFigaroCastlePieces = {{
#include "data/generated/figaro_castle_data.inc"
}};

constexpr std::array<StrafeAngleEntry, kStrafeAngleCount> kStrafeAngles = {{
#include "data/generated/strafe_angle_data.inc"
}};

constexpr std::array<UnusedCutsceneEntry, kUnusedCutsceneWordCount>
    kUnusedCutsceneWords = {{
#include "data/generated/unused_cutscene_data.inc"
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

static_assert(indexMatchesPosition(kEndingSceneCircles));
static_assert(indexMatchesPosition(kFigaroCastlePieces));
static_assert(indexMatchesPosition(kStrafeAngles));
static_assert(indexMatchesPosition(kUnusedCutsceneWords));

// Every circle opens at the same radius and grows at the same rate. That is
// what lets those two columns read as settings rather than per-circle data; if
// a circle ever disagreed, the scene would need them separated.
constexpr bool circlesShareTheirOpeningGrowth() {
    for (const auto& circle : kEndingSceneCircles) {
        if (circle.radius != kEndingSceneCircles.front().radius ||
            circle.radiusStep != kEndingSceneCircles.front().radiusStep) {
            return false;
        }
    }
    return true;
}

static_assert(circlesShareTheirOpeningGrowth(),
              "every ending-scene circle opens and grows the same way");

// Every entry is a bearing.
constexpr bool strafeAnglesAreBearings() {
    for (const auto& entry : kStrafeAngles) {
        if (entry.degrees >= 360) {
            return false;
        }
    }
    return true;
}

static_assert(strafeAnglesAreBearings(),
              "every strafe angle must be a bearing under 360 degrees");

// The table is shorter than the set of masks the control code forms. This is
// the original's own gap, kept rather than closed; see docs/Bugs.md.
static_assert(kStrafeAngleCount < kStrafeDirectionMasks,
              "the strafe table is expected to fall short of the mask space");

}  // namespace

std::span<const EndingSceneCircle> endingSceneCircles() {
    return kEndingSceneCircles;
}

std::span<const FigaroCastlePiece> figaroCastlePieces() {
    return kFigaroCastlePieces;
}

std::span<const StrafeAngleEntry> strafeAngles() { return kStrafeAngles; }

bool strafeMaskReadsPastTheTable(std::uint8_t directionMask) {
    return directionMask >= kStrafeAngleCount;
}

std::optional<std::uint16_t> strafeAngle(std::uint8_t directionMask) {
    if (strafeMaskReadsPastTheTable(directionMask)) {
        return std::nullopt;
    }
    return kStrafeAngles[directionMask].degrees;
}

std::span<const UnusedCutsceneEntry> unusedCutsceneWords() {
    return kUnusedCutsceneWords;
}

}  // namespace ostinato
