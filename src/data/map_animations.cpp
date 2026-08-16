#include "data/map_animations.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>

namespace ostinato {

namespace {

// The 10 palette-animation entries in index order.
constexpr std::array<MapPaletteAnimationEntry, kPaletteAnimationCount>
    kMapPaletteAnimations = {{
#include "data/generated/map_pal_anim_data.inc"
    }};

// The contiguous bg1/bg2 animation byte stream, a raw positional byte blob
// (constexpr std::uint8_t kBgAnimationStream[N]).
#include "data/generated/map_bg_anim_stream_data.inc"

// The 20 animation-index offsets plus the end offset, each carrying its index.
constexpr std::array<BgAnimationOffsetEntry, kBgAnimationCount + 1>
    kBgAnimationOffsets = {{
#include "data/generated/map_bg_anim_offsets_data.inc"
    }};

// The 6 fixed 20-byte bg3 animation records in index order.
constexpr std::array<Bg3AnimationRecordEntry, kBg3AnimationCount>
    kBg3Animations = {{
#include "data/generated/map_bg3_anim_data.inc"
    }};

static_assert(
    kBgAnimationOffsets[kBgAnimationCount].offset == sizeof(kBgAnimationStream),
    "bg-anim end offset must equal the contiguous stream length");

template <typename Table>
constexpr bool indexMatchesPosition(const Table& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].index != i) {
            return false;
        }
    }
    return true;
}

static_assert(indexMatchesPosition(kMapPaletteAnimations),
              "kMapPaletteAnimations index fields must match array positions");
static_assert(indexMatchesPosition(kBgAnimationOffsets),
              "kBgAnimationOffsets index fields must match array positions");
static_assert(indexMatchesPosition(kBg3Animations),
              "kBg3Animations index fields must match array positions");

}  // namespace

const MapPaletteAnimation& mapPaletteAnimation(std::uint8_t index) {
    assert(index < kMapPaletteAnimations.size() &&
           "palette animation index out of range");
    return kMapPaletteAnimations[index].record;
}

std::span<const MapPaletteAnimationEntry> mapPaletteAnimations() {
    return kMapPaletteAnimations;
}

std::span<const std::uint8_t> bgAnimationStream() { return kBgAnimationStream; }

std::span<const BgAnimationOffsetEntry> bgAnimationOffsets() {
    return kBgAnimationOffsets;
}

BgAnimationFrameSet bgAnimationFrameSet(std::size_t byteOffset) {
    assert(byteOffset + sizeof(BgAnimationFrameSet) <= sizeof(kBgAnimationStream) &&
           "bg-anim sub-record read out of range");
    BgAnimationFrameSet frameSet{};
    std::memcpy(&frameSet, kBgAnimationStream + byteOffset, sizeof(frameSet));
    return frameSet;
}

const Bg3AnimationRecord& bg3Animation(std::uint8_t index) {
    assert(index < kBg3Animations.size() && "bg3 animation index out of range");
    return kBg3Animations[index].record;
}

std::span<const Bg3AnimationRecordEntry> bg3Animations() {
    return kBg3Animations;
}

}  // namespace ostinato
