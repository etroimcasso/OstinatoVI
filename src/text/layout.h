// Every number the field dialog text engine works in: the text area's geometry, the timings
// its control codes count in, the limits of its splices, and the glyph codes the interpreter
// treats specially. The logic (dialog.cpp) and the rendering components read these and nothing
// else, so changing one here changes it everywhere it is used.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ostinato::text {

// --- the text area ----------------------------------------------------------------------------

// The text area is kTextRight pixels wide. The pen starts each line kTextLeft pixels in from its
// left edge, and a glyph must end before the right edge (field/text.asm:54-58).
inline constexpr int kTextLeft = 4;
inline constexpr int kTextRight = 224;

// Lines per page.
inline constexpr int kLinesPerPage = 4;

// How far apart the lines are, top to top: kLinePitch while the dialog window is open or closing,
// kOpeningLinePitch while it opens (docs/contracts/dialog_window.md § Where the text sits). The
// window reports which one applies (linePitch() in text/dialog_window.h).
inline constexpr int kLinePitch = 15;
inline constexpr int kOpeningLinePitch = 16;

// Below a line's top: the blank rows above the glyph, and the glyph's own height.
inline constexpr int kGlyphRowOffset = 4;
inline constexpr int kGlyphRows = 11;

// A glyph's artwork starts this many pixels left of the pen, and its ink starts this far into the
// artwork, so the pen is the ink's left edge.
inline constexpr int kGlyphArtLeftOfPen = 4;

// The drop shadow's offset from its glyph.
inline constexpr int kShadowDx = 1;
inline constexpr int kShadowDy = 1;

// The choice arrow sits at the same height in the line as a glyph, and its shadow falls one pixel
// to the right only.
inline constexpr int kCursorShadowDx = 1;
inline constexpr int kCursorShadowDy = 0;

// The rows of a line that show anything: the glyph's rows and its shadow's row below them. The
// choice arrow fits within them too.
inline constexpr int kLineInkRows = kGlyphRows + kShadowDy;

// The width of one column of the text area. A choice's cursor arrow sits at the left edge of the
// column the choice marker was in.
inline constexpr int kCellPx = 16;

// --- the dialog window ------------------------------------------------------------------------

// The window is drawn in square cells this many pixels on a side, and a box is kDialogWindowRows
// cells tall. Its top is cell row kDialogWindowTopRow for a message at the top of the screen and
// kDialogWindowBottomRow for one at the bottom; the map-title box is always at the top and
// kMapTitleWindowRows cells tall.
inline constexpr int kWindowCellPx = 8;
inline constexpr int kDialogWindowRows = 9;
inline constexpr int kDialogWindowTopRow = 1;
inline constexpr int kDialogWindowBottomRow = 18;
inline constexpr int kMapTitleWindowRows = 4;

// The frames a window takes to open: each shows two more of its rows, out from the middle one.
inline constexpr int kWindowOpenFrames = (kDialogWindowRows + 1) / 2;

// Where the text area sits: its left edge on screen, and its top below the top of the box it is in.
// The map title's text area sits lower in its own box.
inline constexpr int kTextAreaLeft = 16;
inline constexpr int kTextAreaTopInBox = 4;
inline constexpr int kTitleTextAreaTopInBox = 6;

// --- the page wipe ----------------------------------------------------------------------------

// Frames from a page being dismissed to the next page typing. On the way the page's text leaves
// half a line per frame, left half then right half, top line first: kWipeSteps of them.
inline constexpr int kPageClearFrames = 9;
inline constexpr int kWipeSteps = 2 * kLinesPerPage;
inline constexpr int kHalfLinePx = kTextRight / 2;

// --- timings ----------------------------------------------------------------------------------

// The pause WAIT holds, and the unit WAIT_FRAMES and KEY_TIMED multiply their operand by.
inline constexpr int kSecondFrames = 60;
inline constexpr int kQuarterSecondFrames = 15;

// The frames KEY arms the key timer for.
inline constexpr int kKeyTimerFrames = 1;

// How long the map title stays up.
inline constexpr int kTitleFrames = 100;

// --- limits -----------------------------------------------------------------------------------

// Characters whose names a message can splice, and how many glyphs each splice takes at most.
inline constexpr std::size_t kCharacterCount = 16;
inline constexpr std::size_t kCharacterNameGlyphs = 6;
inline constexpr std::size_t kItemNameGlyphs = 12;
inline constexpr std::size_t kSpellNameGlyphs = 4;
inline constexpr std::size_t kGoldDigits = 7;

// The spell splice reads the spell-name table at this stride (field/text.asm:783). The table's
// records are wider; see docs/contracts/text_engine.md § Splices.
inline constexpr std::size_t kSpellNameStride = 4;

// Japanese title centering counts every glyph at this width (field/text.asm:151).
inline constexpr int kJapaneseTitleGlyphWidth = 12;

// --- glyph codes ------------------------------------------------------------------------------

// The English space. Word-width measurement stops on it, and TAB fills with it in English.
inline constexpr std::uint16_t kSpaceGlyph = 0x7F;

// The wide space. A CHOICE draws it, and TAB fills with it in Japanese.
inline constexpr std::uint16_t kWideSpaceGlyph = 0xFF;

// The byte that ends a name record before its full length.
inline constexpr std::uint8_t kNameEnd = 0xFF;

// English names are stored in the menu font's encoding; this much below each byte is the same
// letter in the dialog font.
inline constexpr std::uint8_t kMenuToDialogOffset = 0x60;

// The dialog glyph for digit 0 in each language; digits 1-9 follow it.
inline constexpr std::uint16_t kEnglishDigitGlyph = 0x54;
inline constexpr std::uint16_t kJapaneseDigitGlyph = 0x53;

// In Japanese text a byte below kFirstSingleByteGlyph opens a two-byte glyph: the glyph's high
// byte is the opening byte minus kTwoByteGlyphBias, and its low byte is the byte that follows.
inline constexpr std::uint8_t kFirstSingleByteGlyph = 0x20;
inline constexpr std::uint8_t kTwoByteGlyphBias = 0x1B;

// The opening byte the Japanese character-name splice puts before each name byte.
inline constexpr std::uint8_t kJapaneseNameGlyphPrefix = 0x1F;

}  // namespace ostinato::text
