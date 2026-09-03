#include "data/world_anim.h"

#include <array>

namespace ostinato {

namespace {

// The pointer table's own size: one 16-bit offset per frame.
constexpr std::size_t kPointerTableBytes =
    kWorldAnimFrameCount * kWorldAnimPointerBytes;

// The three sequence tables. Every row carries its own step, so a row is
// readable without counting positions.
constexpr std::array<WorldAnimFrameStep, 5> kWorldAnimDismountChocoboFrames = {{
#include "data/generated/world_anim_dismount_chocobo_frames_data.inc"
}};

constexpr std::array<WorldAnimFrameStep, 43> kWorldAnimSmokingAirshipFrames = {{
#include "data/generated/world_anim_smoking_airship_frames_data.inc"
}};

constexpr std::array<WorldAnimFrameStep, 4> kWorldAnimBirdFrames = {{
#include "data/generated/world_anim_bird_frames_data.inc"
}};

// Each table is indexed by its step, so a row has to sit at the step it names.
template <std::size_t N>
constexpr bool stepsAreInOrder(const std::array<WorldAnimFrameStep, N>& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].index != i) {
            return false;
        }
    }
    return true;
}

static_assert(stepsAreInOrder(kWorldAnimDismountChocoboFrames),
              "every dismount-chocobo row must sit at the step it names");
static_assert(stepsAreInOrder(kWorldAnimSmokingAirshipFrames),
              "every smoking-airship row must sit at the step it names");
static_assert(stepsAreInOrder(kWorldAnimBirdFrames),
              "every bird row must sit at the step it names");

// The airship table is whole altitude rows plus the one entry the last row's
// overrun reads.
static_assert(kWorldAnimSmokingAirshipFrames.size() %
                      kSmokingAirshipFrameRowStride == 1,
              "the smoking-airship table is whole rows of six plus the trailing "
              "entry the last row reads past its end");
static_assert(kSmokingAirshipSecondLabelStep %
                      kSmokingAirshipFrameRowStride == 0,
              "the table's second cartridge label must fall on a row boundary");

// One 16-bit offset out of the pointer table, little-endian as the cartridge
// stores it.
std::uint16_t offsetAt(std::span<const std::uint8_t> table, std::size_t frame) {
    const std::size_t at = frame * kWorldAnimPointerBytes;
    if (at + 1 >= table.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(table[at] | (table[at + 1] << 8));
}

}  // namespace

// --- WorldAnimFrame ------------------------------------------------------------

WorldAnimFrame::WorldAnimFrame(std::span<const std::uint8_t> bytes) {
    if (!bytes.empty()) {
        bytes_ = bytes;
    }
}

std::uint8_t WorldAnimFrame::spriteCount() const {
    return valid() ? bytes_[0] : 0;
}

std::size_t WorldAnimFrame::storedRows() const {
    if (!valid()) {
        return 0;
    }
    return (bytes_.size() - 1) / sizeof(WorldAnimSprite);
}

WorldAnimSprite WorldAnimFrame::sprite(std::size_t row) const {
    if (row >= storedRows()) {
        return {};
    }
    const std::size_t at = 1 + row * sizeof(WorldAnimSprite);
    return WorldAnimSprite{
        .x = bytes_[at],
        .y = bytes_[at + 1],
        .tileLow = bytes_[at + 2],
        .attributes = bytes_[at + 3],
    };
}

// --- WorldAnimFrames -----------------------------------------------------------

WorldAnimFrames::WorldAnimFrames(std::span<const std::uint8_t> region) {
    if (region.size() > kPointerTableBytes) {
        region_ = region;
    }
}

std::span<const std::uint8_t> WorldAnimFrames::pointerTable() const {
    if (!valid()) {
        return {};
    }
    return region_.first(kPointerTableBytes);
}

std::span<const std::uint8_t> WorldAnimFrames::records() const {
    if (!valid()) {
        return {};
    }
    return region_.subspan(kPointerTableBytes);
}

std::uint16_t WorldAnimFrames::offsetOf(WorldAnimFrameId frame) const {
    return offsetAt(pointerTable(), static_cast<std::size_t>(frame));
}

WorldAnimFrame WorldAnimFrames::frameAt(WorldAnimFrameId frame) const {
    const auto body = records();
    const std::uint16_t start = offsetOf(frame);
    if (start >= body.size()) {
        return {};
    }

    // A record runs to whichever record starts next. The cartridge stores no
    // length, and the frame's own count byte cannot supply one — eight records
    // hold more rows than they declare — so the bound comes from the offsets
    // themselves. Frames sharing an offset are skipped by the strict >.
    const auto table = pointerTable();
    std::size_t end = body.size();
    for (std::size_t other = 0; other < kWorldAnimFrameCount; ++other) {
        const std::size_t offset = offsetAt(table, other);
        if (offset > start && offset < end) {
            end = offset;
        }
    }
    return WorldAnimFrame{body.subspan(start, end - start)};
}

// --- frame sequences -----------------------------------------------------------

std::span<const WorldAnimFrameStep> dismountChocoboFrames() {
    return kWorldAnimDismountChocoboFrames;
}

std::span<const WorldAnimFrameStep> smokingAirshipFrames() {
    return kWorldAnimSmokingAirshipFrames;
}

std::span<const WorldAnimFrameStep> birdFrames() {
    return kWorldAnimBirdFrames;
}

}  // namespace ostinato
