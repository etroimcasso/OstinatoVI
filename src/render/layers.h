// The screen, the depth every layer draws at, and the names its sprites and regions go by.
//
// Depth is one table so two layers never collide: the battle's layers take the depths below the
// dialog box, the field's world and sprite layers will take the bands below kDialogTextZ too, and
// every later layer allocates its own depth here. A key is what the
// engine recognizes an object by from one frame to the next — an object keeping its key eases to
// its new place, a new key appears where it is put — so every key names the thing it is, never its
// position in a list.
#pragma once

#include <cstdint>
#include <string_view>

#include "retropp/geometry.h"
#include "retropp/viewport.h"

namespace ostinato::render {

// --- the screen ---------------------------------------------------------------------------------

// Every sprite and tile layer covers the whole screen.
inline constexpr retropp::PixelSize kScreenSize = retropp::ViewportResolution::Snes.size();

// --- depth --------------------------------------------------------------------------------------

// The dialog box and the map-title box, beneath the text they hold and above the field.
inline constexpr std::int32_t kDialogWindowZ = 25;

// The dialog text, its choice arrow, and the map title.
inline constexpr std::int32_t kDialogTextZ = 30;

// Within a sprite layer: every drop shadow beneath every piece of ink, so no shadow — a letter's own
// or a neighbor's — ever darkens a letter.
inline constexpr std::int32_t kShadowSpriteZ = 0;
inline constexpr std::int32_t kInkSpriteZ = 1;

// --- the battle ---------------------------------------------------------------------------------

// The backdrop beneath the battlefield: the color the background's see-through pixels show.
inline constexpr std::int32_t kBattleBackdropZ = 0;

// The battlefield's background.
inline constexpr std::int32_t kBattlefieldZ = 1;

// The battlers: one layer each, from this depth up by each one's place in the draw order, so the
// battler drawn last stands in front.
inline constexpr std::int32_t kBattlerBaseZ = 10;

// The message windows at the top of the screen and their text.
inline constexpr std::int32_t kMessageWindowZ = 20;
inline constexpr std::int32_t kMessageTextZ = 21;

// The HUD's windows and their text, and the damage numerals over everything in the battle.
inline constexpr std::int32_t kHudWindowZ = 22;
inline constexpr std::int32_t kHudTextZ = 23;
inline constexpr std::int32_t kDamageNumbersZ = 24;

// --- keys ---------------------------------------------------------------------------------------

// A dialog glyph: dlg:<page>:<line>:<n>, the n-th glyph of a line of a page. The page count runs
// on for the life of the dialog state, so nothing on a new page eases from the old page.
inline constexpr std::string_view kDialogGlyphKey = "dlg";

// A map title glyph: title:<n>.
inline constexpr std::string_view kTitleGlyphKey = "title";

// The choice arrow: dlgc:<page>:<choice>. Keyed by the choice it points at, so moving it puts it
// straight at the new choice rather than sliding it there.
inline constexpr std::string_view kDialogCursorKey = "dlgc";

// A wiped half-line: dlgw:<line>:<half>.
inline constexpr std::string_view kDialogWipeKey = "dlgw";

// The dialog box's layer.
inline constexpr std::string_view kDialogWindowKey = "dlgwin";

// A run of the box's shading: dlgsh:<first row of the run>.
inline constexpr std::string_view kWindowShadingKey = "dlgsh";

// What hides the parts of a layer outside the box's rows shown while it opens and closes.
inline constexpr std::string_view kWindowClipKey = "dlgclip";

// What a sprite's shadow adds to the key of the sprite it falls from.
inline constexpr std::string_view kShadowKeySuffix = ":shadow";

// The battle's backdrop layer and the battlefield's background layer.
inline constexpr std::string_view kBattleBackdropKey = "bfback";
inline constexpr std::string_view kBattlefieldKey = "bfbg";

// What hides the one line below the battlefield the background's tiles reach.
inline constexpr std::string_view kBattlefieldClipKey = "bfclip";

// A monster's layer and sprite: mon:<slot>. A party member's: chr:<slot>. Keyed by the battler, never
// by its place in the draw order, so a battler that steps in front of another stays itself.
inline constexpr std::string_view kMonsterKey = "mon";
inline constexpr std::string_view kCharacterKey = "chr";

// The message windows' layer and the HUD's windows' layer.
inline constexpr std::string_view kMessageWindowKey = "msgwin";
inline constexpr std::string_view kHudWindowKey = "hudwin";

}  // namespace ostinato::render
