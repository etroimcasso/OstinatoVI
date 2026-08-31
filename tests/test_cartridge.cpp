// Tests for the cartridge route: recognising an image, keeping it, and reading it.
//
// Recognition and refusal are exercised with synthetic images. The install round-trip and the read
// need a real cartridge, and a machine that cannot supply one fails rather than skipping.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "assets/cartridge.h"
#include "assets/rom_reader.h"
#include "data/rom_regions.h"
#include "ostinato/rom_identity.h"

#include "vanilla_rom.h"

namespace {

using namespace ostinato;
namespace fs = std::filesystem;

// A directory of this test's own, removed when the test ends. Nothing here touches the real
// per-user data directory: an install writes a three-megabyte file, and a test has no business
// putting one where the player's belongs.
class TempRoot {
public:
    TempRoot() {
        static std::mt19937 sequence{std::random_device{}()};
        path_ = fs::temp_directory_path() /
                ("ostinato-cartridge-test-" + std::to_string(sequence()));
        fs::create_directories(path_);
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

// An image of the right length that is not any cartridge: the size gate passes, the checksum does
// not.
std::vector<std::uint8_t> imageOfRightSizeWrongContent() {
    return std::vector<std::uint8_t>(kRomSizeBytes, 0x5A);
}

// --- recognising an image ----------------------------------------------------

TEST(Cartridge, CopierHeaderIsDroppedByLength) {
    const std::vector<std::uint8_t> headered(kRomSizeBytes + kCopierHeaderBytes, 0x00);
    const auto stripped = assets::stripCopierHeader(headered);
    EXPECT_EQ(stripped.size(), kRomSizeBytes);
    EXPECT_EQ(stripped.data(), headered.data() + kCopierHeaderBytes);
}

TEST(Cartridge, AHeaderlessImageIsLeftAlone) {
    const std::vector<std::uint8_t> plain(kRomSizeBytes, 0x00);
    const auto stripped = assets::stripCopierHeader(plain);
    EXPECT_EQ(stripped.size(), kRomSizeBytes);
    EXPECT_EQ(stripped.data(), plain.data());
}

TEST(Cartridge, AnImageOfSomeOtherLengthIsLeftAlone) {
    // Only the one length means "headered". Anything else is passed through to be refused on its
    // checksum, which is a clearer answer than a silent 512-byte shift.
    const std::vector<std::uint8_t> odd(1024, 0x00);
    EXPECT_EQ(assets::stripCopierHeader(odd).size(), 1024u);
}

TEST(Cartridge, AnImageOfTheWrongLengthIsNotACartridge) {
    const std::vector<std::uint8_t> tooShort(1024, 0x00);
    EXPECT_FALSE(assets::identifyRom(tooShort).has_value());
}

TEST(Cartridge, AnImageWithTheWrongContentIsNotACartridge) {
    EXPECT_FALSE(assets::identifyRom(imageOfRightSizeWrongContent()).has_value());
}

TEST(Cartridge, TheVanillaCartridgeIsRecognised) {
    const std::vector<std::uint8_t> rom = test::vanillaRom();
    const auto version = assets::identifyRom(assets::stripCopierHeader(rom));
    ASSERT_TRUE(version.has_value());
    // Every runner is provisioned with the 1.1 US cartridge.
    EXPECT_EQ(*version, GameVersion::US_1_1);
    EXPECT_EQ(language(*version), Language::EN);
}

// --- keeping one -------------------------------------------------------------

TEST(Cartridge, AnUnrecognisedImageIsRefusedAndNothingIsWritten) {
    const TempRoot root;
    const auto result = assets::installCartridge(imageOfRightSizeWrongContent(), root.path());
    EXPECT_FALSE(result.succeeded);
    EXPECT_FALSE(result.version.has_value());
    // The refusal names what would work, which is the only thing that helps.
    EXPECT_NE(result.message.find("Final Fantasy III 1.1 (U)"), std::string::npos);
    EXPECT_FALSE(assets::cartridgeInstalled(root.path()));
}

TEST(Cartridge, AMissingFileIsRefusedWithItsName) {
    const TempRoot root;
    const fs::path absent = root.path() / "not-here.sfc";
    const auto result = assets::installCartridge(absent, root.path());
    EXPECT_FALSE(result.succeeded);
    EXPECT_NE(result.message.find(absent.string()), std::string::npos);
}

TEST(Cartridge, NothingIsInstalledInAFreshRoot) {
    const TempRoot root;
    EXPECT_FALSE(assets::cartridgeInstalled(root.path()));
}

TEST(Cartridge, InstallingKeepsTheImageByteForByte) {
    const std::vector<std::uint8_t> rom = test::vanillaRom();
    const TempRoot root;
    const auto result = assets::installCartridge(rom, root.path());
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_EQ(result.version, GameVersion::US_1_1);
    EXPECT_TRUE(assets::cartridgeInstalled(root.path()));

    std::ifstream kept{root.path() / "rom" / "cartridge.sfc", std::ios::binary};
    ASSERT_TRUE(kept);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{kept},
                                          std::istreambuf_iterator<char>{}};
    const auto expected = assets::stripCopierHeader(rom);
    ASSERT_EQ(bytes.size(), expected.size());
    EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), expected.begin()));
}

TEST(Cartridge, InstallingFromAFileTakesTheSameRoute) {
    const std::vector<std::uint8_t> rom = test::vanillaRom();
    const TempRoot root;
    const fs::path source = root.path() / "source.sfc";
    {
        std::ofstream out{source, std::ios::binary};
        out.write(reinterpret_cast<const char*>(rom.data()),
                  static_cast<std::streamsize>(rom.size()));
    }
    const auto result = assets::installCartridge(source, root.path());
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_TRUE(assets::cartridgeInstalled(root.path()));
}

// --- reading one -------------------------------------------------------------

TEST(Cartridge, ReadingACartridgeYieldsItsContent) {
    const std::vector<std::uint8_t> rom = test::vanillaRom();
    const assets::IngestedContent content = assets::ingestCartridge(rom);
    EXPECT_EQ(content.version, GameVersion::US_1_1);
    // Every text class the US cartridge ships is there; the one it does not is absent.
    EXPECT_TRUE(content.text.has(TextClass::CHAR_NAME));
    EXPECT_TRUE(content.text.has(TextClass::ITEM_TYPE_NAME));
    EXPECT_TRUE(content.text.has(TextClass::DTE_TABLE));
    // The world-map tile pool, and the offset the chunks count from.
    EXPECT_EQ(content.worldTiles.bytes.size(), 1182u);
    EXPECT_EQ(content.worldTiles.offsetInBlock, 0x48u);
}

TEST(Cartridge, ReadingAnUnrecognisedImageThrows) {
    EXPECT_THROW((void)assets::ingestCartridge(imageOfRightSizeWrongContent()), std::runtime_error);
}

// --- addressing an image -----------------------------------------------------

// A stand-in image the size of a cartridge, each byte carrying its own offset so a read can be
// checked against where it came from.
std::vector<std::uint8_t> countingImage() {
    std::vector<std::uint8_t> image(kRomSizeBytes);
    for (std::size_t i = 0; i < image.size(); ++i) {
        image[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    return image;
}

TEST(HiRomImage, TheFirstBankStartsAtTheStartOfTheImage) {
    const auto image = countingImage();
    const assets::HiRomImage cartridge{image};
    const auto bytes = cartridge.read({.at = 0xC00000, .size = 4});
    EXPECT_EQ(bytes.data(), image.data());
    EXPECT_EQ(bytes.size(), 4u);
}

TEST(HiRomImage, ABankNumberPicksItsSliceOfTheImage) {
    const auto image = countingImage();
    const assets::HiRomImage cartridge{image};
    // Bank $CE is the fourteenth bank, so it begins fourteen 64 KB slices in.
    const auto bytes = cartridge.read({.at = 0xCE0000, .size = 1});
    EXPECT_EQ(bytes.data(), image.data() + (0x0E << 16));
}

TEST(HiRomImage, AReadRunsStraightThroughABankBoundary) {
    // The dialogue bank genuinely does this, which is why a read is one extent.
    const auto image = countingImage();
    const assets::HiRomImage cartridge{image};
    const auto bytes = cartridge.read({.at = 0xCDFFFE, .size = 4});
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(bytes[0], 0xFE);
    EXPECT_EQ(bytes[1], 0xFF);
    EXPECT_EQ(bytes[2], 0x00);  // first byte of the next bank
    EXPECT_EQ(bytes[3], 0x01);
}

TEST(HiRomImage, TheLastByteOfTheImageIsReachable) {
    const auto image = countingImage();
    const assets::HiRomImage cartridge{image};
    const auto bytes = cartridge.read({.at = 0xEFFFFF, .size = 1});
    EXPECT_EQ(bytes.data(), image.data() + kRomSizeBytes - 1);
}

TEST(HiRomImage, AReadPastTheEndThrows) {
    const auto image = countingImage();
    const assets::HiRomImage cartridge{image};
    EXPECT_THROW((void)cartridge.read({.at = 0xEFFFFF, .size = 2}), std::out_of_range);
    EXPECT_FALSE(cartridge.contains({.at = 0xEFFFFF, .size = 2}));
    EXPECT_TRUE(cartridge.contains({.at = 0xEFFFFF, .size = 1}));
}

TEST(HiRomImage, AnAddressBelowTheCartridgeIsRejected) {
    // Banks under $C0 are the console's own memory, not this cartridge.
    const auto image = countingImage();
    const assets::HiRomImage cartridge{image};
    EXPECT_THROW((void)cartridge.read({.at = 0x7E0000, .size = 1}), std::invalid_argument);
    EXPECT_FALSE(cartridge.contains({.at = 0x7E0000, .size = 1}));
}

TEST(HiRomImage, EveryShippedRegionIsInsideACartridge) {
    const auto image = countingImage();
    const assets::HiRomImage cartridge{image};
    for (const auto& row : romRegions()) {
        EXPECT_TRUE(cartridge.contains(row.region))
            << "region " << static_cast<std::size_t>(row.asset) << " does not fit a cartridge";
    }
}

}  // namespace
