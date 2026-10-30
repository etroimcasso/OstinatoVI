// DamageNumbers — the damage numerals bouncing over the battlers.
//
//     SpriteLayer(kDamageNumbersKey, kDamageNumbersZ, {DamageNumbers(presentation, anchors, numerals)});
//
// Every numeral the presentation shows, its digits — or "Miss" — drawn as 8 x 8 tiles from the
// numeral sheet left to right from the battler's anchor less the numeral's offset, lifted by the
// bounce for its frame: up sixteen pixels and back, a smaller second bounce, then still. "Miss"
// starts one tile further right than a number with the same offset. White numerals draw through
// the white palette, healing ones through the green.
//
// A battler's anchor is the point its numerals bounce from: a monster's center at the top of its
// bottom tile row, a party member's center at its feet (numeralAnchor). A battler with no anchor
// shows no numeral.
#pragma once

#include <array>
#include <optional>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "battle/battler_set.h"
#include "battle/presentation.h"
#include "data/monster_sprites.h"
#include "render/battle/battle_placement.h"
#include "render/types.h"

namespace ostinato::render {

// The numeral sheet as uploaded: the ten digits' tiles then the two of "Miss", and the two
// palettes.
struct NumeralSheet {
    retropp::AtlasManifest tiles;  // cells 0-9 the digits, 10-11 "Miss"
    retropp::PaletteId white{};
    retropp::PaletteId green{};
};

// The first of "Miss"'s two cells in the numeral sheet.
inline constexpr std::size_t kMissCell = 10;

// The point a battler's numerals bounce from, in screen pixels.
struct NumeralAnchor {
    int x = 0;
    int y = 0;

    constexpr bool operator==(const NumeralAnchor&) const = default;
};

using NumeralAnchors = std::array<std::optional<NumeralAnchor>, battle::kBattlerSlots>;

// Where a monster standing as `standing`, or a party member standing as `member`, has its numerals.
NumeralAnchor numeralAnchor(const MonsterStanding& standing);
NumeralAnchor numeralAnchor(const PartyStanding& member);

[[nodiscard]] Sprites DamageNumbers(const battle::BattlePresentationState& presentation, const NumeralAnchors& anchors,
                                    const NumeralSheet& numerals);

}  // namespace ostinato::render
