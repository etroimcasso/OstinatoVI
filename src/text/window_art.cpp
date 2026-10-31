#include "text/window_art.h"

#include <cassert>

namespace ostinato::text {

namespace {

// The bytes of one tile's first two bit planes, two per row, and where the next two begin.
constexpr std::size_t kPlanePairBytes = 16;

// Five-bit color channels, and the mask and positions of each in a color word.
constexpr unsigned kChannelMax = 31;
constexpr unsigned kChannelMask = 0x1F;
constexpr unsigned kGreenShift = 5;
constexpr unsigned kBlueShift = 10;

std::uint8_t expandChannel(unsigned fiveBit) {
    return static_cast<std::uint8_t>((fiveBit * 255 + kChannelMax / 2) / kChannelMax);
}

// The five-bit channel an expanded channel came from.
unsigned narrowChannel(std::uint8_t eightBit) {
    return (eightBit * kChannelMax + 255 / 2) / 255;
}

std::uint8_t brightenChannel(std::uint8_t eightBit) {
    const unsigned raised = narrowChannel(eightBit) + static_cast<unsigned>(kBattleWindowBrightening);
    return expandChannel(raised < kChannelMax ? raised : kChannelMax);
}

std::span<const std::uint8_t> wallpaperTiles(const WindowArt& art, WallpaperId wallpaper) {
    const auto at = static_cast<std::size_t>(wallpaper) * kWallpaperBytes;
    assert(art.tiles.size() >= at + kWallpaperBytes && "no such wallpaper");
    return art.tiles.subspan(at, kWallpaperBytes);
}

}  // namespace

IndexedImage buildWindowSheet(const WindowArt& art, WallpaperId wallpaper) {
    const std::span<const std::uint8_t> tiles = wallpaperTiles(art, wallpaper);

    IndexedImage sheet;
    sheet.width = static_cast<int>(kWindowTileCount) * kWindowTilePx;
    sheet.height = kWindowTilePx;
    sheet.pixels.assign(static_cast<std::size_t>(sheet.width) *
                            static_cast<std::size_t>(sheet.height),
                        0);

    for (std::size_t t = 0; t < kWindowTileCount; ++t) {
        const auto tile = tiles.subspan(t * kWindowTileBytes, kWindowTileBytes);
        const int cellX = static_cast<int>(t) * kWindowTilePx;
        for (int y = 0; y < kWindowTilePx; ++y) {
            const auto row = static_cast<std::size_t>(y) * 2;
            const unsigned plane0 = tile[row];
            const unsigned plane1 = tile[row + 1];
            const unsigned plane2 = tile[kPlanePairBytes + row];
            for (int x = 0; x < kWindowTilePx; ++x) {
                const unsigned bit = 7u - static_cast<unsigned>(x);
                const unsigned value = ((plane0 >> bit) & 1u) | ((plane1 >> bit) & 1u) << 1 |
                                       ((plane2 >> bit) & 1u) << 2;
                sheet.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(sheet.width) +
                             static_cast<std::size_t>(cellX + x)] =
                    static_cast<std::uint8_t>(value);
            }
        }
    }
    return sheet;
}

WindowColors defaultWindowColors(const WindowArt& art, WallpaperId wallpaper) {
    const auto at = static_cast<std::size_t>(wallpaper) * kWallpaperPaletteBytes;
    assert(art.palettes.size() >= at + kWallpaperPaletteBytes && "no such wallpaper palette");
    WindowColors colors{};
    for (std::size_t i = 0; i < kWindowColorCount; ++i) {
        const std::size_t color = at + (i + 1) * kColorBytes;
        const unsigned word = static_cast<unsigned>(art.palettes[color]) |
                              static_cast<unsigned>(art.palettes[color + 1]) << 8;
        colors[i] = retropp::Rgba8{
            .r = expandChannel(word & kChannelMask),
            .g = expandChannel((word >> kGreenShift) & kChannelMask),
            .b = expandChannel((word >> kBlueShift) & kChannelMask),
        };
    }
    return colors;
}

WindowColors battleWindowColors(const WindowColors& colors) {
    WindowColors battle{};
    for (std::size_t i = 0; i < kWindowColorCount; ++i) {
        battle[i] = retropp::Rgba8{
            .r = brightenChannel(colors[i].r),
            .g = brightenChannel(colors[i].g),
            .b = brightenChannel(colors[i].b),
        };
    }
    return battle;
}

}  // namespace ostinato::text
