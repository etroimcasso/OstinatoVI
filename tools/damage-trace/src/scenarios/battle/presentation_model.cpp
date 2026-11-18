// A whole battle's frames: the opening's and each script's, drawn from the model's seed.

#include "scenarios/battle/presentation_model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace ostinato::trace {

namespace {

// One battle in this many has a story countdown.
constexpr std::uint64_t kBattlesPerCountdown = 8;

// A generator for one draw: the seed and the place of what is drawn, mixed so neighboring places differ.
std::mt19937_64 generator(std::uint64_t seed, std::uint64_t place) {
    std::seed_seq mixed{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32),
                        static_cast<std::uint32_t>(place), static_cast<std::uint32_t>(place >> 32)};
    return std::mt19937_64{mixed};
}

int drawnUpTo(std::mt19937_64& random, int most) {
    return std::uniform_int_distribution<int>{0, most}(random);
}

}  // namespace

std::vector<Frame> PresentationModel::openingFrames() const {
    std::vector<Frame> frames(static_cast<std::size_t>(openingFramesBeforeClock), Frame{.clockCalled = false});
    // The waits that took two frames fall evenly among the clock calls: the frame with no clock comes
    // before the call it delayed, never before the first.
    const int calls = openingClockCalls;
    const int missed = std::min(openingWaitsMissed, std::max(0, calls - 1));
    int nextMissed = 1;
    for (int call = 0; call < calls; ++call) {
        if (nextMissed <= missed && call == nextMissed * calls / (missed + 1)) {
            frames.push_back(Frame{.clockCalled = false});
            ++nextMissed;
        }
        frames.push_back(Frame{.clockCalled = true});
    }
    return frames;
}

std::vector<Frame> PresentationModel::scriptFrames(std::size_t index, bool stopsTime) const {
    std::mt19937_64 random = generator(seed, index);
    const int length = 1 + drawnUpTo(random, kLongestScript - 1);
    return std::vector<Frame>(static_cast<std::size_t>(length), Frame{.clockCalled = !stopsTime});
}

PresentationModel measuredPresentationModel(std::uint64_t seed) {
    return PresentationModel{.seed = seed,
                             .openingFramesBeforeClock = kMeasuredFramesBeforeClock,
                             .openingClockCalls = kMeasuredClockCalls,
                             .openingWaitsMissed = kMeasuredWaitsMissed,
                             .timerRunsOutAt = std::nullopt};
}

PresentationModel drawPresentationModel(std::uint64_t seed, std::uint64_t framesAtMost) {
    // The opening's counts are drawn from a place no script takes.
    std::mt19937_64 random = generator(seed, ~std::uint64_t{0});
    PresentationModel model{.seed = seed,
                            .openingFramesBeforeClock = drawnUpTo(random, 2 * kMeasuredFramesBeforeClock),
                            .openingClockCalls = drawnUpTo(random, 2 * kMeasuredClockCalls),
                            .openingWaitsMissed = drawnUpTo(random, 2 * kMeasuredWaitsMissed),
                            .timerRunsOutAt = std::nullopt};
    if (random() % kBattlesPerCountdown == 0 && framesAtMost > 0) {
        model.timerRunsOutAt = random() % framesAtMost;
    }
    return model;
}

}  // namespace ostinato::trace
