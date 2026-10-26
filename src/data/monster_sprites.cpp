#include "data/monster_sprites.h"

#include <algorithm>
#include <array>

namespace ostinato {

namespace {

constexpr std::array<MonsterSpriteBox, 48> kMonsterSpriteBoxes = {{
#include "data/generated/monster_sprite_boxes_data.inc"
}};

// The rows run layout by layout, each layout's slots counting from 0, so a box is found by its
// layout and slot alone.
constexpr bool boxesAreInOrder() {
    for (std::size_t i = 0; i < kMonsterSpriteBoxes.size(); ++i) {
        const MonsterSpriteBox& box = kMonsterSpriteBoxes[i];
        if (i == 0) {
            if (box.layout != 0 || box.slot != 0) {
                return false;
            }
            continue;
        }
        const MonsterSpriteBox& before = kMonsterSpriteBoxes[i - 1];
        const bool nextSlot = box.layout == before.layout && box.slot == before.slot + 1;
        const bool nextLayout = box.layout == before.layout + 1 && box.slot == 0;
        if (!nextSlot && !nextLayout) {
            return false;
        }
    }
    return true;
}

static_assert(boxesAreInOrder(), "the boxes must run layout by layout, slots counting from 0");

// One graphics-properties record: the graphics word, the palette word (its first byte's top bit
// marks a large stencil), and the stencil number.
constexpr std::size_t kPropertyBytes = 5;
constexpr std::size_t kLargeStencilFlagByte = 2;
constexpr std::uint8_t kLargeStencilFlag = 0x80;
constexpr std::size_t kStencilNumberByte = 4;

// A small stencil stores one byte per row, a large one two; the leftmost tile is the top bit of
// the row's first byte.
constexpr int kSmallSide = 8;
constexpr int kLargeSide = 16;
constexpr std::size_t kSmallStencilBytes = kSmallSide;
constexpr std::size_t kLargeStencilBytes = 2 * kLargeSide;

// The Ghost Train's sprite is drawn at this size, not the one its stencil and box give.
constexpr MonsterSpriteSize kGhostTrainSize{.width = 16, .height = 14};

}  // namespace

std::span<const MonsterSpriteBox> monsterSpriteBoxes() {
    return kMonsterSpriteBoxes;
}

std::optional<MonsterSpriteSize> monsterSpriteBox(std::uint8_t layout, std::size_t slot) {
    for (const MonsterSpriteBox& box : kMonsterSpriteBoxes) {
        if (box.layout == layout && box.slot == slot) {
            return MonsterSpriteSize{.width = box.width, .height = box.height};
        }
    }
    return std::nullopt;
}

// --- MonsterStencil -----------------------------------------------------------------------------

MonsterStencil::MonsterStencil(std::span<const std::uint8_t> bytes, bool large) : large_(large) {
    if (bytes.size() == (large ? kLargeStencilBytes : kSmallStencilBytes)) {
        bytes_ = bytes;
    }
}

int MonsterStencil::side() const {
    if (!valid()) {
        return 0;
    }
    return large_ ? kLargeSide : kSmallSide;
}

bool MonsterStencil::filled(int row, int column) const {
    if (row < 0 || column < 0 || row >= side() || column >= side()) {
        return false;
    }
    const std::size_t bytesPerRow = large_ ? 2 : 1;
    const std::uint8_t byte = bytes_[static_cast<std::size_t>(row) * bytesPerRow +
                                     static_cast<std::size_t>(column / 8)];
    return (byte & (0x80 >> (column % 8))) != 0;
}

MonsterSpriteSize MonsterStencil::extent() const {
    MonsterSpriteSize size;
    for (int row = 0; row < side(); ++row) {
        int rightmost = -1;
        for (int column = 0; column < side(); ++column) {
            if (filled(row, column)) {
                rightmost = column;
            }
        }
        if (rightmost < 0) {
            break;
        }
        size.height = row + 1;
        size.width = std::max(size.width, rightmost + 1);
    }
    return size;
}

// --- MonsterSprites -----------------------------------------------------------------------------

MonsterSprites::MonsterSprites(std::span<const std::uint8_t> properties,
                               std::span<const std::uint8_t> smallStencils,
                               std::span<const std::uint8_t> largeStencils)
    : properties_(properties), smallStencils_(smallStencils), largeStencils_(largeStencils) {}

bool MonsterSprites::valid() const {
    return !properties_.empty() && !smallStencils_.empty() && !largeStencils_.empty();
}

std::size_t MonsterSprites::graphicsCount() const {
    return properties_.size() / kPropertyBytes;
}

MonsterStencil MonsterSprites::stencil(MonsterId monster) const {
    const auto graphic = static_cast<std::size_t>(monster);
    if (graphic >= graphicsCount()) {
        return {};
    }
    const std::span<const std::uint8_t> record = properties_.subspan(graphic * kPropertyBytes, kPropertyBytes);
    const bool large = (record[kLargeStencilFlagByte] & kLargeStencilFlag) != 0;
    const std::span<const std::uint8_t> family = large ? largeStencils_ : smallStencils_;
    const std::size_t width = large ? kLargeStencilBytes : kSmallStencilBytes;
    const std::size_t at = record[kStencilNumberByte] * width;
    if (at + width > family.size()) {
        return {};
    }
    return MonsterStencil{family.subspan(at, width), large};
}

MonsterSpriteSize MonsterSprites::size(MonsterId monster, MonsterSpriteSize box) const {
    if (monster == MonsterId::GHOSTTRAIN) {
        return kGhostTrainSize;
    }
    const MonsterSpriteSize extent = stencil(monster).extent();
    return MonsterSpriteSize{
        .width = std::min(extent.width, box.width),
        .height = std::min(extent.height, box.height),
    };
}

}  // namespace ostinato
