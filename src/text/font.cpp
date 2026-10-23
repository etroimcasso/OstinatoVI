#include "text/font.h"

#include <cassert>

namespace ostinato::text {

namespace {

// The bit of a row that holds the pixel `x` from the left.
constexpr unsigned glyphBit(int x) { return static_cast<unsigned>(kGlyphArtWidth - 1 - x); }
constexpr unsigned cursorBit(int x) { return static_cast<unsigned>(kCursorWidth - 1 - x); }

}  // namespace

std::uint8_t IndexedImage::at(int x, int y) const {
    assert(x >= 0 && x < width && y >= 0 && y < height && "pixel outside the image");
    return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)];
}

CursorArt::CursorArt(std::span<const std::uint8_t> rows) : rows_(rows) {
    assert(rows.size() == kCursorRows && "the arrow is kCursorRows rows");
}

bool CursorArt::ink(int x, int y) const {
    assert(x >= 0 && x < kCursorWidth && y >= 0 && y < kCursorRows && "outside the arrow");
    return (rows_[static_cast<std::size_t>(y)] >> cursorBit(x)) & 1u;
}

std::size_t DialogFont::glyphCount() const {
    return metrics.size() > kFirstDrawnGlyph ? metrics.size() - kFirstDrawnGlyph : 0;
}

std::size_t glyphSlot(std::uint16_t glyph) {
    assert(glyph >= kFirstDrawnGlyph && "control codes have no artwork");
    return glyph - kFirstDrawnGlyph;
}

IndexedImage buildGlyphSheet(std::span<const std::uint8_t> glyphArt, std::size_t glyphCount) {
    assert(glyphArt.size() >= glyphCount * kGlyphArtBytes && "not enough artwork");
    const std::size_t sheetRows = (glyphCount + kSheetColumns - 1) / kSheetColumns;

    IndexedImage sheet;
    sheet.width = kSheetColumns * kGlyphCellPx;
    sheet.height = static_cast<int>(sheetRows) * kGlyphCellPx;
    sheet.pixels.assign(static_cast<std::size_t>(sheet.width) *
                            static_cast<std::size_t>(sheet.height),
                        kBlankPixel);

    for (std::size_t slot = 0; slot < glyphCount; ++slot) {
        const auto art = glyphArt.subspan(slot * kGlyphArtBytes, kGlyphArtBytes);
        const int cellX = static_cast<int>(slot % kSheetColumns) * kGlyphCellPx;
        const int cellY = static_cast<int>(slot / kSheetColumns) * kGlyphCellPx;
        for (int row = 0; row < kGlyphRows; ++row) {
            const std::size_t at = static_cast<std::size_t>(row) * kGlyphArtRowBytes;
            const unsigned bits = static_cast<unsigned>(art[at]) |
                                  static_cast<unsigned>(art[at + 1]) << 8;
            for (int x = 0; x < kGlyphArtWidth; ++x) {
                if ((bits >> glyphBit(x)) & 1u) {
                    sheet.pixels[static_cast<std::size_t>(cellY + row) *
                                     static_cast<std::size_t>(sheet.width) +
                                 static_cast<std::size_t>(cellX + x)] = kInkPixel;
                }
            }
        }
    }
    return sheet;
}

IndexedImage buildCursorSheet(const CursorArt& art) {
    assert(art.loaded() && "no arrow to build a sheet from");
    IndexedImage sheet;
    sheet.width = kCursorWidth;
    sheet.height = kCursorCellHeight;
    sheet.pixels.assign(static_cast<std::size_t>(sheet.width) *
                            static_cast<std::size_t>(sheet.height),
                        kBlankPixel);
    for (int y = 0; y < kCursorRows; ++y) {
        for (int x = 0; x < kCursorWidth; ++x) {
            if (art.ink(x, y)) {
                sheet.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(sheet.width) +
                             static_cast<std::size_t>(x)] = kInkPixel;
            }
        }
    }
    return sheet;
}

}  // namespace ostinato::text
