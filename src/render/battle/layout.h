// Where the battle's picture sits on the screen: the battlefield's height, where the HUD begins,
// where the HUD's lines of text fall, where the windows at the top of the screen put their text,
// and the bands both shadings darken in.
//
// The screen is split at scanline 151. Lines 0-150 show the battlefield — the background's first 19
// tile rows, cut off one line short of the nineteenth — and lines 152-223 the HUD: two windows of
// eight tile rows whose text lines sit twelve pixels apart, each showing seven of its letters' eight
// pixel rows. Line 151 shows neither; the backdrop color shows there.
//
//     TileLayer(kBattlefieldKey, kBattlefieldZ, BattleBackground(...), kBackgroundColumns,
//               kBackgroundRowsShown, {.regions = BattlefieldClip()});
//     TileLayer(kHudWindowKey, kHudWindowZ, HudWindows(...), hudWindowsColumns(lang), kHudWindowRows,
//               {.scroll = hudWindowsScroll(lang), .regions = HudShading(lang)});
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

// The HUD's windows are shaded darker toward the bottom: from line 151, every three lines take one
// more 31st off each channel, over 24 bands to the bottom of the screen. The windows end at line
// 215, partway into the 22nd band.
inline constexpr int kHudShadingTopPx = 151;
inline constexpr int kHudShadingBandRows = 3;
inline constexpr int kHudShadingBands = 24;

// --- the windows at the top of the screen ----------------------------------------------------

// The message windows sit on the battlefield's own tile grid at tile row 1. Their text takes the
// 16-pixel line on tile rows 2 and 3, a letter placed in it as the dialog's are
// (text::kGlyphRowOffset, text::kGlyphArtLeftOfPen): the wide window's pen from tile column 2 across
// 28 columns, the narrow window's from column 9 across 22.
inline constexpr int kMessageTextTopPx = 16;
inline constexpr int kMessageLineHeightPx = 16;
inline constexpr int kWideMessageTextColumn = 2;
inline constexpr int kWideMessageTextColumns = 28;
inline constexpr int kNarrowMessageTextColumn = 9;
inline constexpr int kNarrowMessageTextColumns = 22;

// The message windows are shaded darker toward the bottom too: from line 8, every two lines take
// one more 31st off each channel.
inline constexpr int kMessageShadingTopPx = 8;
inline constexpr int kMessageShadingBandRows = 2;
inline constexpr int kMessageShadingBands = 16;

}  // namespace ostinato::render
