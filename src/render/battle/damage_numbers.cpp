#include "render/battle/damage_numbers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "data/battle_gfx_tables.h"
#include "data/damage_numerals.h"
#include "render/battle/layout.h"
#include "render/layers.h"

namespace ostinato::render {

namespace {

// A party member's numerals bounce from the middle of its 16-pixel frame, 16 below its point
// (btlgfx _c10659 :1088-1100: w7e8033 = x + 8, w7e8043 = y + 16).
constexpr int kPartyAnchorDx = 8;
constexpr int kPartyAnchorDy = 16;

// A numeral tile is a cell of the numeral sheet.
constexpr int kNumeralCellPx = 8;

std::vector<int> digitsOf(std::uint16_t amount) {
    std::vector<int> digits;
    do {
        digits.insert(digits.begin(), amount % 10);
        amount /= 10;
    } while (amount != 0);
    return digits;
}

void addNumeral(Sprites& sprites, std::string_view key, const battle::ShownNumeral& numeral,
                const NumeralAnchor& anchor, const NumeralSheet& sheet) {
    const int left = anchor.x - numeral.offset;
    const int y = anchor.y - damageNumberLift(numeral.frame);
    const retropp::PaletteId palette = numeral.heals ? sheet.green : sheet.white;
    std::vector<std::size_t> cells;
    int x = left;
    if (numeral.miss) {
        cells = {kMissCell, kMissCell + 1};
        x += kNumeralCellPx;
    } else {
        for (const int digit : digitsOf(numeral.amount)) {
            cells.push_back(static_cast<std::size_t>(digit));
        }
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        sprites.push_back(retropp::Sprite{
            .key = std::string{key} + ":" + std::to_string(i),
            .x = x + static_cast<int>(i) * kNumeralCellPx,
            .y = y,
            .size = {kNumeralCellPx, kNumeralCellPx},
            .atlas = sheet.tiles.atlasId,
            .tile = sheet.tiles[cells[i]].tile,
            .palette = palette,
        });
    }
}

}  // namespace

NumeralAnchor numeralAnchor(const MonsterStanding& standing) {
    return {.x = standing.x + standing.size.width * kBattleCellPx / 2,
            .y = standing.y + (standing.size.height - 1) * kBattleCellPx};
}

NumeralAnchor numeralAnchor(const PartyStanding& member) {
    return {.x = member.x + kPartyAnchorDx, .y = member.y + kPartyAnchorDy};
}

Sprites DamageNumbers(const battle::BattlePresentationState& presentation, const NumeralAnchors& anchors,
                      const NumeralSheet& numerals) {
    Sprites sprites;
    for (std::size_t index = 0; index < presentation.numeralsTogether.size(); ++index) {
        const std::optional<battle::ShownNumeral>& numeral = presentation.numeralsTogether[index];
        if (numeral && anchors[index]) {
            addNumeral(sprites, std::string{kDamageNumeralKey} + ":" + std::to_string(index), *numeral, *anchors[index],
                       numerals);
        }
    }
    for (std::size_t thread = 0; thread < presentation.singleNumerals.size(); ++thread) {
        const std::optional<battle::ShownNumeral>& numeral = presentation.singleNumerals[thread];
        if (!numeral) {
            continue;
        }
        const std::optional<NumeralAnchor>& anchor = anchors[static_cast<std::size_t>(numeral->target.index())];
        if (anchor) {
            addNumeral(sprites, std::string{kSingleNumeralKey} + ":" + std::to_string(thread), *numeral, *anchor,
                       numerals);
        }
    }
    return sprites;
}

}  // namespace ostinato::render
