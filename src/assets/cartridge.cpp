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
#include "retropp/vm.h"

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

// The places one launch reads. The VM checks a whole batch at once and reports every bad entry by
// name, so the families are declared together and answered together rather than one failure at a
// time during play.
//
// One member per family read, because a place is named by a pointer to the member holding it. A
// family the cartridge's language does not ship is left out of the batch entirely — a zero region
// would be a place that exists and is empty, which is a different claim.
struct CartridgeRegions {
    retropp::MemoryRegion charName{};
    retropp::MemoryRegion itemName{};
    retropp::MemoryRegion magicName{};
    retropp::MemoryRegion attackName{};
    retropp::MemoryRegion monsterName{};
    retropp::MemoryRegion monsterSpecialName{};
    retropp::MemoryRegion statusName{};
    retropp::MemoryRegion genjuName{};
    retropp::MemoryRegion genjuAttackName{};
    retropp::MemoryRegion genjuBonusName{};
    retropp::MemoryRegion danceName{};
    retropp::MemoryRegion bushidoName{};
    retropp::MemoryRegion battleCommandName{};
    retropp::MemoryRegion itemTypeName{};
    retropp::MemoryRegion rareItemName{};
    retropp::MemoryRegion dialogue1{};
    retropp::MemoryRegion dialogue2{};
    retropp::MemoryRegion attackMessage{};
    retropp::MemoryRegion battleDialogue{};
    retropp::MemoryRegion monsterDialogue{};
    retropp::MemoryRegion mapTitle{};
    retropp::MemoryRegion itemDescription{};
    retropp::MemoryRegion magicDescription{};
    retropp::MemoryRegion loreDescription{};
    retropp::MemoryRegion blitzDescription{};
    retropp::MemoryRegion bushidoDescription{};
    retropp::MemoryRegion genjuAttackDescription{};
    retropp::MemoryRegion genjuBonusDescription{};
    retropp::MemoryRegion rareItemDescription{};
    retropp::MemoryRegion dteTable{};
    retropp::MemoryRegion worldTiles{};
};

// Which member holds each text class's place, and the name a bad entry is reported under.
constexpr std::array<std::pair<TextClass, retropp::MemoryRegion CartridgeRegions::*>,
                     kTextClassCount>
kTextClassRegions = {{
    { TextClass::CHAR_NAME,            &CartridgeRegions::charName               },
    { TextClass::ITEM_NAME,            &CartridgeRegions::itemName               },
    { TextClass::MAGIC_NAME,           &CartridgeRegions::magicName              },
    { TextClass::ATTACK_NAME,          &CartridgeRegions::attackName             },
    { TextClass::MONSTER_NAME,         &CartridgeRegions::monsterName            },
    { TextClass::MONSTER_SPECIAL_NAME, &CartridgeRegions::monsterSpecialName     },
    { TextClass::STATUS_NAME,          &CartridgeRegions::statusName             },
    { TextClass::GENJU_NAME,           &CartridgeRegions::genjuName              },
    { TextClass::GENJU_ATTACK_NAME,    &CartridgeRegions::genjuAttackName        },
    { TextClass::GENJU_BONUS_NAME,     &CartridgeRegions::genjuBonusName         },
    { TextClass::DANCE_NAME,           &CartridgeRegions::danceName              },
    { TextClass::BUSHIDO_NAME,         &CartridgeRegions::bushidoName            },
    { TextClass::BATTLE_CMD_NAME,      &CartridgeRegions::battleCommandName      },
    { TextClass::ITEM_TYPE_NAME,       &CartridgeRegions::itemTypeName           },
    { TextClass::RARE_ITEM_NAME,       &CartridgeRegions::rareItemName           },
    { TextClass::DLG1,                 &CartridgeRegions::dialogue1              },
    { TextClass::DLG2,                 &CartridgeRegions::dialogue2              },
    { TextClass::ATTACK_MSG,           &CartridgeRegions::attackMessage          },
    { TextClass::BATTLE_DLG,           &CartridgeRegions::battleDialogue         },
    { TextClass::MONSTER_DLG,          &CartridgeRegions::monsterDialogue        },
    { TextClass::MAP_TITLE,            &CartridgeRegions::mapTitle               },
    { TextClass::ITEM_DESC,            &CartridgeRegions::itemDescription        },
    { TextClass::MAGIC_DESC,           &CartridgeRegions::magicDescription       },
    { TextClass::LORE_DESC,            &CartridgeRegions::loreDescription        },
    { TextClass::BLITZ_DESC,           &CartridgeRegions::blitzDescription       },
    { TextClass::BUSHIDO_DESC,         &CartridgeRegions::bushidoDescription     },
    { TextClass::GENJU_ATTACK_DESC,    &CartridgeRegions::genjuAttackDescription },
    { TextClass::GENJU_BONUS_DESC,     &CartridgeRegions::genjuBonusDescription  },
    { TextClass::RARE_ITEM_DESC,       &CartridgeRegions::rareItemDescription    },
    { TextClass::DTE_TABLE,            &CartridgeRegions::dteTable               },
}};

// The mapping is indexed directly, so each row must sit at its class's position.
static_assert([] {
    for (std::size_t i = 0; i < kTextClassRegions.size(); ++i) {
        if (static_cast<std::size_t>(kTextClassRegions[i].first) != i) {
            return false;
        }
    }
    return true;
}(), "kTextClassRegions rows must sit at their TextClass positions");

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

    // The machine exists for this call and no longer: it is a way to address the cartridge's own
    // memory, not something the game runs on.
    retropp::Vm vm{retropp::VMPlatform::Snes};
    vm.hostRom(image);

    CartridgeRegions places{};
    retropp::RegionMap<CartridgeRegions> batch;

    const auto declare = [&](retropp::MemoryRegion CartridgeRegions::* member, RomAsset asset,
                             std::string_view name) {
        const std::optional<retropp::MemoryRegion> where = romRegion(asset, spoken);
        if (!where) {
            return;  // this language does not ship the family
        }
        places.*member = *where;
        batch.bindings.push_back(retropp::region(member, *where, name));
    };

    for (const auto& [klass, member] : kTextClassRegions) {
        declare(member, textClassAsset(klass), textClassMetadata(klass).fileStem);
    }
    declare(&CartridgeRegions::worldTiles, RomAsset::WORLD_MOD_TILES, "world_mod_tiles");

    const retropp::RegionMapId<CartridgeRegions> declared = vm.registerRegions(batch);

    // A class left out of the batch keeps its empty buffer, which is exactly how the corpus
    // reports a family this language does not ship.
    std::array<std::vector<std::uint8_t>, kTextClassCount> corpus{};
    for (const auto& [klass, member] : kTextClassRegions) {
        if ((places.*member).size == 0) {
            continue;
        }
        corpus[static_cast<std::size_t>(klass)] = vm.read(declared, member);
    }

    // The tile pool is catalogued so the patches can be views over it: the library holds the bytes
    // for the life of the program, where the vector read out of the VM would not survive this call.
    const std::vector<std::uint8_t> tiles = vm.read(declared, &CartridgeRegions::worldTiles);
    const retropp::DataId pool = library.uploadData(tiles);

    // A chunk's patch offset counts from the start of the modification block, which begins with the
    // per-world chunk lists; the pool follows them. The distance between the two is the difference
    // between where each begins in the cartridge.
    const std::optional<retropp::MemoryRegion> firstList =
        romRegion(RomAsset::WORLD_1_MOD, spoken);
    const std::optional<retropp::MemoryRegion> poolPlace =
        romRegion(RomAsset::WORLD_MOD_TILES, spoken);
    const std::uint16_t poolOffsetInBlock =
        (firstList && poolPlace)
            ? static_cast<std::uint16_t>(poolPlace->at - firstList->at)
            : 0;

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
