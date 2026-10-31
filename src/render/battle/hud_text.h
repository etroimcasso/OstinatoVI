// HudText — a run of the HUD's small-font letters.
//
//     SpriteLayer(kHudTextKey, kHudTextZ, {HudText("hud:hp:1", codes, 168, hudLineY(1), palettes[0], font)});
//
// Each code is one 8 x 8 glyph of the small font, drawn left to right from (x, y) through `palette`,
// one sprite per glyph keyed <key>:<n>. kHudBlank draws nothing.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "render/battle/layout.h"
#include "render/types.h"

namespace ostinato::render {

// The blank code: it holds a place in a run and draws nothing.
inline constexpr std::uint8_t kHudBlank = 0xFF;

// The y of HUD text line `line` (0 to 3): where its glyphs' top row sits.
constexpr int hudLineY(int line) { return kHudTextFirstLinePx + line * kHudTextLinePitchPx; }

// `smallFont` is the uploaded small-font sheet (text::buildSmallFontSheet), one 8 x 8 cell per code.
[[nodiscard]] Sprites HudText(std::string_view key, std::span<const std::uint8_t> codes, int x, int y,
                              retropp::PaletteId palette, const retropp::AtlasManifest& smallFont);

}  // namespace ostinato::render
