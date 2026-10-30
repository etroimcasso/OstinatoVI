// PartyStatus — the party's names, HP and gauges in the HUD's right window.
//
//     SpriteLayer(kHudTextKey, kHudTextZ, {PartyStatus(battle, palettes, font)}, {.regions = HudTextClip()});
//
// One line per party slot with a member on the field, slot 0 on the first HUD text line, each part
// at the column the battle's text placements give it:
//
//   - the member's name, six letters;
//   - its HP, four digits with the leading zeros left blank and the last digit always shown;
//   - its ATB gauge — a left cap, four cells each filled a pixel column at a time, a right cap — in
//     palette 2 when full and palette 5 while filling; a member in Morph shows its Morph gauge the
//     same way in palette 6. With the gauge turned off in the battle settings the gauge's place
//     shows "~" and the member's max HP instead.
//
// Names and HP draw through palette 0.
#pragma once

#include <array>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "battle/battle_state.h"
#include "render/types.h"
#include "text/small_font.h"

namespace ostinato::render {

// The palettes a HUD part draws through, by the battle's own numbering.
inline constexpr std::size_t kHudTextPalette = 0;
inline constexpr std::size_t kFullGaugePalette = 2;
inline constexpr std::size_t kFillingGaugePalette = 5;
inline constexpr std::size_t kMorphGaugePalette = 6;

using SmallFontPaletteIds = std::array<retropp::PaletteId, text::kSmallFontPaletteCount>;

[[nodiscard]] Sprites PartyStatus(const battle::BattleState& battle, const SmallFontPaletteIds& palettes,
                                  const retropp::AtlasManifest& smallFont);

}  // namespace ostinato::render
