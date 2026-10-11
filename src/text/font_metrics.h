// The dialogue font's per-glyph advance widths, as a view over the table the cartridge holds.
//
// The table sits immediately before the dialogue font's graphics in every cartridge: 256 bytes
// in English (one per glyph code $00-$FF) and 1,280 in Japanese (glyph codes $000-$4FF). The
// width of a glyph is how far the pen moves after drawing it, and what the English word wrap
// adds up.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ostinato::text {

class FontMetrics {
public:
    FontMetrics() = default;

    // A view over a width table. The bytes must outlive this object.
    explicit FontMetrics(std::span<const std::uint8_t> widths) : widths_(widths) {}

    // The advance width of `glyph` in pixels. PRECONDITION (asserted): glyph < size().
    int width(std::uint16_t glyph) const {
        assert(glyph < widths_.size() && "glyph outside the width table");
        return widths_[glyph];
    }

    std::size_t size() const { return widths_.size(); }
    bool loaded() const { return !widths_.empty(); }

private:
    std::span<const std::uint8_t> widths_{};
};

}  // namespace ostinato::text
