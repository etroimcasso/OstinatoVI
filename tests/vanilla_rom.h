// Reading a real cartridge in a test, and the oracle its contents are checked against.
//
// Tests that exercise the real content route need a real cartridge, and every machine that runs
// this suite is expected to have one: FF6_VANILLA_ROM names it. A cartridge that cannot be read is
// a provisioning failure and fails the test that wanted it, naming the variable and the path — it
// is never skipped past, because a skip would let a misconfigured machine report a clean run.
//
// Nothing here skips. Reading a cartridge needs no emulator — the port maps a cartridge address
// onto the image directly — so a test that wants one either gets it or fails.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "data/rom_regions.h"
#include "ostinato/game_version.h"
#include "ostinato/rom_asset.h"

namespace ostinato::test {

// The cartridge FF6_VANILLA_ROM names.
//
// Throws std::runtime_error naming the variable and the path when it is unset, or when the file
// cannot be read or is empty. The throw fails the test — which is the point: a machine without a
// readable cartridge is misconfigured, and that has to be visible rather than quietly absorbed.
inline std::vector<std::uint8_t> vanillaRom() {
    const char* path = std::getenv("FF6_VANILLA_ROM");
    if (path == nullptr || *path == '\0') {
        throw std::runtime_error(
            "FF6_VANILLA_ROM is not set. Every machine running this suite needs a cartridge to "
            "read; set it to the path of one.");
    }
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error(std::string{"FF6_VANILLA_ROM names a file that cannot be opened: "}
                                 + path);
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{file},
                                    std::istreambuf_iterator<char>{}};
    if (bytes.empty()) {
        throw std::runtime_error(std::string{"FF6_VANILLA_ROM names an empty file: "} + path);
    }
    return bytes;
}

// One reading of the cartridge, kept alongside the image it came from so a test can compare what
// the reader handed back against the file's own bytes.
//
// `romError` carries whatever stopped a cartridge being read, so the test that wanted one can fail
// on it — a fixture's suite-wide setup cannot fail a test by throwing.
struct IngestedCartridge {
    std::vector<std::uint8_t> image;
    std::optional<assets::IngestedContent> content;
    std::string romError;

    bool available() const { return content.has_value(); }
};

// Read the cartridge FF6_VANILLA_ROM names. Anything that stops it — no variable, an unreadable
// file, an image no revision matches — is recorded for the test to fail on.
inline IngestedCartridge ingestVanilla() {
    IngestedCartridge result;
    try {
        result.image = vanillaRom();
        result.content = assets::ingestCartridge(result.image);
    } catch (const std::exception& error) {
        result.romError = error.what();
    }
    return result;
}

// The guard every cartridge-reading test opens with. There is no skip path: a cartridge that could
// not be read fails the test, naming what stopped it.
#define OSTINATO_REQUIRE_CARTRIDGE(cartridge)                       \
    do {                                                            \
        if (!(cartridge).available()) {                             \
            FAIL() << (cartridge).romError;                         \
        }                                                           \
    } while (0)

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
