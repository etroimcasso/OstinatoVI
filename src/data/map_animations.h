// Map animation tables: palette animation, bg1/bg2 tile animation, and bg3 tile
// animation. Palette animation is a fixed 12-byte record per index (two 6-byte
// slots); the two bg-animation families are pointer-table streams of tile
// sub-records. The row data is generated (src/data/generated/*.inc); this header
// owns the record types and accessors. The frame values index pack-side graphics
// (MapAnimGfx, Phase F1); this layer carries the indices only.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/palette_animation_control.h"

namespace ostinato {

// --- palette animation -------------------------------------------------------

// One 6-byte palette-animation slot. InitPalAnim copies the six bytes into the
// slot work area (anim.asm:37-48): the control byte, the frame duration, the
// CGRAM-mirror color start offset, the color byte count, and a u16 offset into
// the MapPalAnimColors rom table (used by the ROM_COLORS type).
struct PaletteAnimationSlot {
    PaletteAnimationControl control;  // +0
    std::uint8_t frameDuration;       // +1  -> $10e8
    std::uint8_t colorOffset;         // +2  -> $10eb (CGRAM-mirror byte offset)
    std::uint8_t colorByteCount;      // +3  -> $10ec
    std::uint16_t romColorOffset;     // +4..+5 -> $10ed/e (into MapPalAnimColors)
};

static_assert(sizeof(PaletteAnimationSlot) == 6,
              "PaletteAnimationSlot must be byte-identical to a 6-byte slot");
static_assert(std::endian::native == std::endian::little,
              "PaletteAnimationSlot::romColorOffset assumes a little-endian "
              "platform");
static_assert(offsetof(PaletteAnimationSlot, control) == 0);
static_assert(offsetof(PaletteAnimationSlot, frameDuration) == 1);
static_assert(offsetof(PaletteAnimationSlot, colorOffset) == 2);
static_assert(offsetof(PaletteAnimationSlot, colorByteCount) == 3);
static_assert(offsetof(PaletteAnimationSlot, romColorOffset) == 4);

// One palette-animation entry: two slots (12 bytes total), selected by
// MapProperties +26 (index 0 = none, else record n-1). InitPalAnim copies 12
// bytes = two slots per entry (anim.asm:30-62).
struct MapPaletteAnimation {
    std::array<PaletteAnimationSlot, 2> slots;
};

static_assert(sizeof(MapPaletteAnimation) == 12,
              "MapPaletteAnimation must be byte-identical to a 12-byte entry");

struct MapPaletteAnimationEntry {
    std::uint16_t index;
    MapPaletteAnimation record;
};

// The number of palette-animation entries (map_pal_anim_prop.dat is 10 x 12).
inline constexpr std::size_t kPaletteAnimationCount = 10;

// --- bg1/bg2 animation -------------------------------------------------------

// A decoded bg1/bg2 tile sub-record (MapBGAnimProp scope, anim.asm:277-285): a
// tile animation speed and four frame graphics offsets. This is a reader-side
// view over the contiguous byte stream.
struct BgAnimationFrameSet {
    std::uint16_t animSpeed;              // +0
    std::array<std::uint16_t, 4> frames;  // +2..+9
};

static_assert(sizeof(BgAnimationFrameSet) == 10,
              "BgAnimationFrameSet must be a 10-byte bg-anim sub-record view");
static_assert(std::endian::native == std::endian::little,
              "BgAnimationFrameSet assumes a little-endian platform");

// The number of bg1/bg2 animation indexes (the MapBGAnimProp pointer table has
// 20 entries).
inline constexpr std::size_t kBgAnimationCount = 20;

// The number of sub-records the consumer reads per animation index, regardless
// of the body's actual length (InitBG12Anim's cpy #13*8 loop, anim.asm:319).
inline constexpr std::size_t kBgAnimationSubRecordsRead = 8;

// One entry of the bg1/bg2 offset table: the animation index as a typed
// identity field alongside the byte offset of its body into the stream. Indexes
// 11/12/17/19 alias one body, so they carry distinct indexes with the same
// offset; the final entry (index 20) is the end offset (== stream length). A
// compile-time assert verifies index == array position.
struct BgAnimationOffsetEntry {
    std::uint16_t index;
    std::uint32_t offset;
};

// --- bg3 animation -----------------------------------------------------------

// One 20-byte bg3 tile-animation record (MapBG3AnimProp scope, anim.asm:382-395):
// a tile animation speed, a graphics size, and eight frame graphics offsets.
struct Bg3AnimationRecord {
    std::uint16_t animSpeed;              // +0
    std::uint16_t gfxSize;                // +2
    std::array<std::uint16_t, 8> frames;  // +4..+19
};

static_assert(sizeof(Bg3AnimationRecord) == 20,
              "Bg3AnimationRecord must be byte-identical to a 20-byte record");
static_assert(std::endian::native == std::endian::little,
              "Bg3AnimationRecord assumes a little-endian platform");

struct Bg3AnimationRecordEntry {
    std::uint16_t index;
    Bg3AnimationRecord record;
};

// The number of bg3 animation records (the MapBG3AnimProp pointer table has 6
// entries).
inline constexpr std::size_t kBg3AnimationCount = 6;

// --- accessors ---------------------------------------------------------------

// The palette-animation entry for an index (0-9). index must be in range.
const MapPaletteAnimation& mapPaletteAnimation(std::uint8_t index);
std::span<const MapPaletteAnimationEntry> mapPaletteAnimations();

// The contiguous bg1/bg2 animation byte stream. Animation bodies are stored
// back-to-back in pointer-table order; four indexes (11/12/17/19) alias one
// body, and the consumer's fixed 8-sub-record read spills a short body into the
// next one — both are reproducible because storage stays contiguous.
std::span<const std::uint8_t> bgAnimationStream();

// The 20 animation-index byte offsets into the stream plus a final end offset
// (21 entries), each carrying its animation index as a typed identity field.
// Aliased indexes (11/12/17/19) share the same offset; entry 20 is the end.
std::span<const BgAnimationOffsetEntry> bgAnimationOffsets();

// Decode the bg1/bg2 sub-record at a byte offset into the stream. The consumer
// reads kBgAnimationSubRecordsRead sub-records starting at
// bgAnimationOffsets()[index]; callers advance the offset by sizeof themselves.
BgAnimationFrameSet bgAnimationFrameSet(std::size_t byteOffset);

// The bg3 animation record for an index (0-5). index must be in range.
const Bg3AnimationRecord& bg3Animation(std::uint8_t index);
std::span<const Bg3AnimationRecordEntry> bg3Animations();

}  // namespace ostinato
