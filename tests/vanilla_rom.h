// Reading a real cartridge in a test, and the oracle its contents are checked against.
//
// Tests that exercise the real content route need a real cartridge. Every CI runner exports
// FF6_VANILLA_ROM naming one; a developer's machine may or may not. A test that cannot find one
// skips with the reason said out loud rather than passing vacuously.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "data/rom_regions.h"
#include "ostinato/game_version.h"
#include "ostinato/rom_asset.h"

namespace ostinato::test {

// What a test says when it skips for want of a cartridge.
inline constexpr const char* kNoVanillaRom = "no cartridge available (set FF6_VANILLA_ROM)";

// What a test says when the cartridge is there but the machine that reads it is not yet built.
inline constexpr const char* kNoSnesBackend = "SNES VM backend not built";

// The cartridge FF6_VANILLA_ROM names, or nothing when the variable is unset or the file cannot be
// read.
inline std::optional<std::vector<std::uint8_t>> vanillaRom() {
    const char* path = std::getenv("FF6_VANILLA_ROM");
    if (path == nullptr || *path == '\0') {
        return std::nullopt;
    }
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{file},
                                    std::istreambuf_iterator<char>{}};
    if (bytes.empty()) {
        return std::nullopt;
    }
    return bytes;
}

// One reading of the cartridge, kept alongside the image it came from so a test can compare what
// the machine handed back against the file's own bytes.
//
// `skipReason` is empty when `content` is there, and otherwise says which of the two things is
// missing — the cartridge, or the machine that reads it.
struct IngestedCartridge {
    std::vector<std::uint8_t> image;
    std::optional<assets::IngestedContent> content;
    std::string skipReason;

    bool available() const { return content.has_value(); }
};

// Read the cartridge FF6_VANILLA_ROM names. Neither absence is a failure: a missing cartridge and
// an unbuilt machine both come back as a reason to skip.
inline IngestedCartridge ingestVanilla() {
    IngestedCartridge result;
    auto rom = vanillaRom();
    if (!rom) {
        result.skipReason = kNoVanillaRom;
        return result;
    }
    result.image = std::move(*rom);
    try {
        result.content = assets::ingestCartridge(result.image);
    } catch (const std::runtime_error&) {
        result.skipReason = kNoSnesBackend;
    }
    return result;
}

// A family's bytes taken straight out of the image, sliced at the place the region table names.
//
// This is deliberately a second route to the same bytes: the corpus under test comes back from the
// machine, and this comes from the file, so a disagreement means one of them is wrong. Mapping the
// cartridge address to a file offset is the machine's job everywhere else — it is done by hand here
// only because an oracle that used the machine would not be independent of it.
inline std::vector<std::uint8_t> romSlice(std::span<const std::uint8_t> image, RomAsset asset,
                                          Language spoken = Language::EN) {
    const auto place = romRegion(asset, spoken);
    if (!place) {
        return {};
    }
    const std::size_t offset =
        static_cast<std::size_t>((place->at >> 16) & 0x3F) << 16 | (place->at & 0xFFFF);
    if (offset + place->size > image.size()) {
        return {};
    }
    const auto slice = image.subspan(offset, place->size);
    return std::vector<std::uint8_t>{slice.begin(), slice.end()};
}

// The same, for the family a text class's records live in.
inline std::vector<std::uint8_t> romSlice(std::span<const std::uint8_t> image, TextClass klass,
                                          Language spoken = Language::EN) {
    return romSlice(image, textClassAsset(klass), spoken);
}

}  // namespace ostinato::test
