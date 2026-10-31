// Where the battle's picture sits on the screen: the lines the battle leaves black, the
// battlefield's height, where the HUD begins, where the HUD's lines of text fall, where the windows
// at the top of the screen put their text, and the bands both shadings darken in.
//
// The battle shows nothing on lines 0-3 and 219-223, and nothing in the screen border's columns.
// Lines 0-150 show the battlefield — the background's tilemap from its second pixel row, its 19 tile
// rows ending on line 150 — and lines 151-214 the HUD: two windows of eight tile rows whose text
// lines sit twelve pixels apart, every letter shown whole.
//
//     frame.regions = ScreenBorder(kBattleTopBlankRows, kBattleBottomBlankRows);
//     TileLayer(kBattlefieldKey, kBattlefieldZ, BattleBackground(...), kBackgroundColumns,
//               kBackgroundRowsShown, {.scroll = {.y = kBattlefieldScrollPx}});
//     TileLayer(kHudWindowKey, kHudWindowZ, HudWindows(...), hudWindowsColumns(lang), kHudWindowRows,
//               {.scroll = hudWindowsScroll(lang), .regions = HudShading(lang)});
#pragma once

namespace ostinato::render {

// A tile layer's cell.
inline constexpr int kBattleCellPx = 8;

// The lines at the top and the bottom of the screen the battle leaves black.
inline constexpr int kBattleTopBlankRows = 4;
inline constexpr int kBattleBottomBlankRows = 5;

// The battlefield: lines 0 to 150, showing the background's tilemap from its pixel row 1.
inline constexpr int kBattlefieldHeightPx = 151;
inline constexpr int kBattlefieldScrollPx = 1;

// The HUD's windows: eight tile rows from line 151, ending at 214.
inline constexpr int kHudTopPx = 151;
inline constexpr int kHudWindowRows = 8;

// The HUD's text: four lines, the first at 163 and each next one 12 lower.
inline constexpr int kHudTextLines = 4;
inline constexpr int kHudTextFirstLinePx = 163;
inline constexpr int kHudTextLinePitchPx = 12;

// The HUD's windows are shaded darker toward the bottom: from line 151, every three lines take one
// more 31st off each channel, over 24 bands to the bottom of the screen. The windows end at line
// 214, in the 22nd band.
inline constexpr int kHudShadingTopPx = 151;
inline constexpr int kHudShadingBandRows = 3;
inline constexpr int kHudShadingBands = 24;

// --- the windows at the top of the screen ----------------------------------------------------

// The message windows' boxes sit at tile row 1, from line 8. Their text takes
// the 16-pixel line from line 15, a letter placed in it as the dialog's are (text::kGlyphRowOffset,
// text::kGlyphArtLeftOfPen): the wide window's pen from tile column 2 across 28 columns, the narrow
// window's from column 9 across 22.
inline constexpr int kMessageTextTopPx = 15;
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
