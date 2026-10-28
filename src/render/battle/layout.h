// Where the battle's picture sits on the screen: the battlefield's height, where the HUD begins, and
// where the HUD's lines of text fall.
//
// The screen is split at scanline 151. Lines 0-150 show the battlefield — the background's first 19
// tile rows, cut off one line short of the nineteenth — and lines 152-223 the HUD: two windows of
// eight tile rows whose text lines sit twelve pixels apart, each showing seven of its letters' eight
// pixel rows. Line 151 shows neither; the backdrop color shows there.
//
//     TileLayer(kBattlefieldKey, kBattlefieldZ, BattleBackground(...), kBackgroundColumns,
//               kBackgroundRowsShown, {.regions = BattlefieldClip()});
//     TileLayer(kHudWindowKey, kHudWindowZ, cells, 32, kHudWindowRows, {.scroll = {0, -kHudTopPx}});
#pragma once

namespace ostinato::render {

// A tile layer's cell.
inline constexpr int kBattleCellPx = 8;

// The battlefield: lines 0 to 150.
inline constexpr int kBattlefieldHeightPx = 151;

// The HUD's windows: eight tile rows from line 152, ending at 215.
inline constexpr int kHudTopPx = 152;
inline constexpr int kHudWindowRows = 8;

// The HUD's text: four lines, the first at 164 and each next one 12 lower, each showing seven pixel
// rows of its letters.
inline constexpr int kHudTextLines = 4;
inline constexpr int kHudTextFirstLinePx = 164;
inline constexpr int kHudTextLinePitchPx = 12;
inline constexpr int kHudGlyphRowsShown = 7;

// The message windows sit on the battlefield's own tile grid, their text two tile rows below their
// top border.
inline constexpr int kMessageTextRowOffset = 2;

}  // namespace ostinato::render
