// The dialogue font, as the player's cartridge holds it: each glyph's artwork, how far the pen
// moves after it, and the arrow drawn beside the selected choice. Nothing here is compiled in — the
// cartridge is read once at launch (assets::ingestCartridge) and this is the view over what it held.
//
// The two builders turn the cartridge's one-bit artwork into indexed images ready to upload as
// sprite sheets: every pixel is kBlankPixel or kInkPixel, and colour comes from the palette a sprite
// draws through. The same glyph sheet serves the letter and its drop shadow; the rendering
// components (src/render/text/) decide where and in which colour each is drawn.
//
//     const DialogueFont& font = content.dialogueFont;
//     const IndexedImage glyphs = buildGlyphSheet(font.glyphArt, font.glyphCount());
//     const IndexedImage arrow = buildCursorSheet(font.cursor);
//     // glyph g is cell glyphSlot(g) of the glyph sheet, read in rows of kSheetColumns
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "text/font_metrics.h"
#include "text/layout.h"

namespace ostinato::text {

// --- the cartridge's artwork --------------------------------------------------------------------

// The first glyph code with artwork. The codes below it are control codes and are never drawn.
inline constexpr std::uint16_t kFirstDrawnGlyph = 0x20;

// A glyph's artwork: kGlyphRows rows, each kGlyphArtWidth pixels stored in two bytes, low byte
// first, with the leftmost pixel in the row's top bit.
inline constexpr int kGlyphArtWidth = 16;
inline constexpr std::size_t kGlyphArtRowBytes = 2;
inline constexpr std::size_t kGlyphArtBytes = kGlyphRows * kGlyphArtRowBytes;

// The arrow's artwork: kCursorRows rows of kCursorWidth pixels, one byte each, leftmost pixel in
// the top bit.
inline constexpr int kCursorRows = 12;
inline constexpr int kCursorWidth = 8;

// --- the sheets ---------------------------------------------------------------------------------

// The glyph sheet puts kSheetColumns glyphs on a row, each in a square cell kGlyphCellPx on a side
// with its artwork in the top kGlyphRows rows. The arrow's sheet is one cell, kCursorWidth by
// kCursorCellHeight, its artwork in the top kCursorRows rows. Both cell sizes are whole multiples
// of the 8-pixel grid sprites are read from.
inline constexpr int kSheetColumns = 16;
inline constexpr int kGlyphCellPx = 16;
inline constexpr int kCursorCellHeight = 16;

// The two pixel values a sheet holds.
inline constexpr std::uint8_t kBlankPixel = 0;
inline constexpr std::uint8_t kInkPixel = 1;

// A sheet of indexed pixels, row-major.
struct IndexedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    // The pixel at (x, y). PRECONDITION (asserted): inside the image.
    std::uint8_t at(int x, int y) const;
};

// The arrow drawn beside the selected choice, as a view over the cartridge's bytes.
class CursorArt {
public:
    // No arrow: a cartridge without a known place for one.
    CursorArt() = default;

    // A view over kCursorRows bytes. The bytes must outlive this object.
    // PRECONDITION (asserted): exactly kCursorRows bytes.
    explicit CursorArt(std::span<const std::uint8_t> rows);

    bool loaded() const { return !rows_.empty(); }

    // Whether the arrow has ink at (x, y), x < kCursorWidth, y < kCursorRows.
    bool ink(int x, int y) const;

private:
    std::span<const std::uint8_t> rows_{};
};

// Everything the cartridge holds for the dialogue font.
struct DialogueFont {
    // Every drawn glyph's artwork, kGlyphArtBytes per glyph from kFirstDrawnGlyph up. The English
    // cartridge stores a little past its last glyph; glyphCount() says how many there are.
    std::span<const std::uint8_t> glyphArt;

    FontMetrics metrics;

    // Empty on the Japanese cartridge, whose arrow has no known place.
    CursorArt cursor;

    // How many glyphs have artwork: every code the width table covers, from kFirstDrawnGlyph up
    // (224 in English, 1,248 in Japanese).
    std::size_t glyphCount() const;
};

// Which cell of the glyph sheet holds `glyph`. PRECONDITION (asserted): glyph >= kFirstDrawnGlyph.
std::size_t glyphSlot(std::uint16_t glyph);

// The glyph sheet for the first `glyphCount` glyphs of `glyphArt`: kSheetColumns cells across and
// as many rows as the glyphs need, every pixel kBlankPixel or kInkPixel.
// PRECONDITION (asserted): glyphArt holds at least glyphCount * kGlyphArtBytes bytes.
IndexedImage buildGlyphSheet(std::span<const std::uint8_t> glyphArt, std::size_t glyphCount);

// The arrow's one-cell sheet. PRECONDITION (asserted): art.loaded().
IndexedImage buildCursorSheet(const CursorArt& art);

}  // namespace ostinato::text
