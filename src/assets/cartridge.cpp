#include "assets/cartridge.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "retropp/data_library.h"
#include "retropp/memory_region.h"
#include "retropp/user_files.h"

#include "assets/rom_reader.h"
#include "data/rom_regions.h"
#include "ostinato/rom_asset.h"
#include "ostinato/rom_identity.h"
#include "ostinato/text_class.h"

namespace ostinato::assets {

namespace {

// CRC32 as the upstream tooling computes it (the ISO-HDLC polynomial in its reflected form). The
// table is built once on first use; a cartridge is three megabytes, so a byte-at-a-time pass costs
// nothing worth optimising.
constexpr std::uint32_t kCrcPolynomial = 0xEDB88320u;

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> built{};
        for (std::uint32_t byte = 0; byte < built.size(); ++byte) {
            std::uint32_t remainder = byte;
            for (int bit = 0; bit < 8; ++bit) {
                remainder = (remainder & 1u) ? (remainder >> 1) ^ kCrcPolynomial
                                             : (remainder >> 1);
            }
            built[byte] = remainder;
        }
        return built;
    }();
    return table;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    const auto& table = crcTable();
    std::uint32_t remainder = 0xFFFFFFFFu;
    for (const std::uint8_t byte : bytes) {
        remainder = table[(remainder ^ byte) & 0xFFu] ^ (remainder >> 8);
    }
    return remainder ^ 0xFFFFFFFFu;
}

// What a refusal tells the player. Naming the accepted cartridges is the whole content of the
// message: someone whose dump is rejected needs to know which ones work, not that a checksum
// disagreed.
std::string acceptedCartridges() {
    return "Accepted cartridges: Final Fantasy VI 1.0 (J), Final Fantasy III 1.0 (U), and "
           "Final Fantasy III 1.1 (U).";
}

std::string versionName(GameVersion version) {
    switch (version) {
        case GameVersion::JP_1_0: return "Final Fantasy VI 1.0 (J)";
        case GameVersion::US_1_0: return "Final Fantasy III 1.0 (U)";
        case GameVersion::US_1_1: return "Final Fantasy III 1.1 (U)";
    }
    return "an unknown revision";
}

}  // namespace

std::span<const std::uint8_t> stripCopierHeader(std::span<const std::uint8_t> image) {
    if (image.size() == kRomSizeBytes + kCopierHeaderBytes) {
        return image.subspan(kCopierHeaderBytes);
    }
    return image;
}

std::optional<GameVersion> identifyRom(std::span<const std::uint8_t> image) {
    if (image.size() != kRomSizeBytes) {
        return std::nullopt;
    }
    const std::uint32_t checksum = crc32(image);
    for (const auto& candidate : kRomIdentities) {
        if (candidate.crc32 == checksum) {
            return candidate.version;
        }
    }
    return std::nullopt;
}

InstallResult installCartridge(std::span<const std::uint8_t> image,
                               const std::filesystem::path& root) {
    const std::span<const std::uint8_t> payload = stripCopierHeader(image);
    const std::optional<GameVersion> version = identifyRom(payload);
    if (!version) {
        return {false, std::nullopt,
                "That file is not a Final Fantasy VI cartridge this port can read. "
                + acceptedCartridges()};
    }

    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(payload.data()), payload.size()};
    if (!retropp::UserFiles::atPath(root).write("rom/cartridge.sfc", bytes)) {
        return {false, version,
                "Recognised " + versionName(*version)
                    + ", but the copy could not be written. Your previous cartridge, if any, is "
                      "untouched."};
    }
    return {true, version, "Installed " + versionName(*version) + "."};
}

InstallResult installCartridge(const std::filesystem::path& romPath,
                               const std::filesystem::path& root) {
    std::ifstream file{romPath, std::ios::binary};
    if (!file) {
        return {false, std::nullopt, "Could not open " + romPath.string() + "."};
    }
    const std::vector<std::uint8_t> image{std::istreambuf_iterator<char>{file},
                                          std::istreambuf_iterator<char>{}};
    if (!file && !file.eof()) {
        return {false, std::nullopt, "Could not read " + romPath.string() + "."};
    }
    return installCartridge(std::span<const std::uint8_t>{image}, root);
}

bool cartridgeInstalled(const std::filesystem::path& root) {
    return retropp::UserFiles::atPath(root).exists("rom/cartridge.sfc");
}

IngestedContent ingestCartridge(std::span<const std::uint8_t> rawImage) {
    retropp::DataLibrary& library = retropp::DataLibrary::instance();
    const std::span<const std::uint8_t> image = stripCopierHeader(rawImage);

    const std::optional<GameVersion> version = identifyRom(image);
    if (!version) {
        throw std::runtime_error("The installed cartridge is not a revision this port can read. "
                                 + acceptedCartridges());
    }
    const Language spoken = language(*version);
    const HiRomImage cartridge{image};

    // A class the cartridge's language does not ship keeps its empty buffer, which is exactly how
    // the corpus reports a family it does not have.
    std::array<std::vector<std::uint8_t>, kTextClassCount> corpus{};
    for (std::size_t i = 0; i < kTextClassCount; ++i) {
        const auto klass = static_cast<TextClass>(i);
        const std::optional<retropp::MemoryRegion> where =
            romRegion(textClassAsset(klass), spoken);
        if (!where) {
            continue;
        }
        const std::span<const std::uint8_t> bytes = cartridge.read(*where);
        corpus[i].assign(bytes.begin(), bytes.end());
    }

    // The tile pool is catalogued so the patches can be views over it: the library holds the bytes
    // for the life of the program, where a view into a caller's image would not outlive the call.
    const std::optional<retropp::MemoryRegion> poolPlace =
        romRegion(RomAsset::WORLD_MOD_TILES, spoken);
    const std::optional<retropp::MemoryRegion> firstList =
        romRegion(RomAsset::WORLD_1_MOD, spoken);
    if (!poolPlace || !firstList) {
        throw std::runtime_error("The cartridge has no world-map modification block.");
    }
    const retropp::DataId pool = library.uploadData(cartridge.read(*poolPlace));

    // A chunk's patch offset counts from the start of the modification block, which begins with the
    // per-world chunk lists; the pool follows them. The distance between the two is the difference
    // between where each begins in the cartridge.
    const auto poolOffsetInBlock = static_cast<std::uint16_t>(poolPlace->at - firstList->at);

    return IngestedContent{
        .version = *version,
        .text = TextCorpus{std::move(corpus)},
        .worldTiles = WorldTilePool{library.data(pool), poolOffsetInBlock},
    };
}

IngestedContent ingestCartridge() {
    // The cartridge is registered at a literal path and read on the first data() call, which is
    // now — after the asset root is settled. Nothing was read at registration.
    retropp::DataLibrary& library = retropp::DataLibrary::instance();
    const retropp::DataId cartridge = library.registerData("rom/cartridge.sfc");
    return ingestCartridge(library.data(cartridge));
}

}  // namespace ostinato::assets
