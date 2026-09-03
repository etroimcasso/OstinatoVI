#include "data/rom_regions.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <utility>

namespace ostinato {

namespace {

// Every (family, language) pair there is an address for. A family both
// languages ship has two rows; one only a single language ships has one.
constexpr std::array<RomRegionEntry, 310> kRomRegions = {{
#include "data/generated/rom_regions_data.inc"
}};

// The row order the lookup depends on: ascending by asset, and within one
// asset ascending by language. Emitted that way, and checked here so a
// re-emission that changed the order fails the build rather than the lookup.
constexpr bool rowsAreSorted() {
    for (std::size_t i = 1; i < kRomRegions.size(); ++i) {
        const auto& previous = kRomRegions[i - 1];
        const auto& current = kRomRegions[i];
        const auto previousKey = std::pair(static_cast<std::size_t>(previous.asset),
                                           static_cast<std::size_t>(previous.language));
        const auto currentKey = std::pair(static_cast<std::size_t>(current.asset),
                                          static_cast<std::size_t>(current.language));
        if (!(previousKey < currentKey)) {
            return false;
        }
    }
    return true;
}

static_assert(rowsAreSorted(),
              "kRomRegions rows must ascend by asset then language, with no "
              "asset/language pair listed twice");

// Every row names a family this port knows and covers a non-empty extent.
static_assert([] {
    for (const auto& row : kRomRegions) {
        if (static_cast<std::size_t>(row.asset) >= kRomAssetCount) {
            return false;
        }
        if (row.region.size == 0 || row.region.count != 1) {
            return false;
        }
    }
    return true;
}(), "every region must name a known asset and cover one non-empty extent");

// Which family holds each text class's records. The names line up class for
// class; only the DTE pair table is spelled differently upstream.
constexpr std::array<std::pair<TextClass, RomAsset>, kTextClassCount>
kTextClassAssets = {{
    { TextClass::CHAR_NAME,            RomAsset::CHAR_NAME            },
    { TextClass::ITEM_NAME,            RomAsset::ITEM_NAME            },
    { TextClass::MAGIC_NAME,           RomAsset::MAGIC_NAME           },
    { TextClass::ATTACK_NAME,          RomAsset::ATTACK_NAME          },
    { TextClass::MONSTER_NAME,         RomAsset::MONSTER_NAME         },
    { TextClass::MONSTER_SPECIAL_NAME, RomAsset::MONSTER_SPECIAL_NAME },
    { TextClass::STATUS_NAME,          RomAsset::STATUS_NAME          },
    { TextClass::GENJU_NAME,           RomAsset::GENJU_NAME           },
    { TextClass::GENJU_ATTACK_NAME,    RomAsset::GENJU_ATTACK_NAME    },
    { TextClass::GENJU_BONUS_NAME,     RomAsset::GENJU_BONUS_NAME     },
    { TextClass::DANCE_NAME,           RomAsset::DANCE_NAME           },
    { TextClass::BUSHIDO_NAME,         RomAsset::BUSHIDO_NAME         },
    { TextClass::BATTLE_CMD_NAME,      RomAsset::BATTLE_CMD_NAME      },
    { TextClass::ITEM_TYPE_NAME,       RomAsset::ITEM_TYPE_NAME       },
    { TextClass::RARE_ITEM_NAME,       RomAsset::RARE_ITEM_NAME       },
    { TextClass::DLG1,                 RomAsset::DLG1                 },
    { TextClass::DLG2,                 RomAsset::DLG2                 },
    { TextClass::ATTACK_MSG,           RomAsset::ATTACK_MSG           },
    { TextClass::BATTLE_DLG,           RomAsset::BATTLE_DLG           },
    { TextClass::MONSTER_DLG,          RomAsset::MONSTER_DLG          },
    { TextClass::MAP_TITLE,            RomAsset::MAP_TITLE            },
    { TextClass::ITEM_DESC,            RomAsset::ITEM_DESC            },
    { TextClass::MAGIC_DESC,           RomAsset::MAGIC_DESC           },
    { TextClass::LORE_DESC,            RomAsset::LORE_DESC            },
    { TextClass::BLITZ_DESC,           RomAsset::BLITZ_DESC           },
    { TextClass::BUSHIDO_DESC,         RomAsset::BUSHIDO_DESC         },
    { TextClass::GENJU_ATTACK_DESC,    RomAsset::GENJU_ATTACK_DESC    },
    { TextClass::GENJU_BONUS_DESC,     RomAsset::GENJU_BONUS_DESC     },
    { TextClass::RARE_ITEM_DESC,       RomAsset::RARE_ITEM_DESC       },
    { TextClass::DTE_TABLE,            RomAsset::DTE_TBL              },
}};

// The mapping is indexed directly, so each row must sit at its class's
// position.
static_assert([] {
    for (std::size_t i = 0; i < kTextClassAssets.size(); ++i) {
        if (static_cast<std::size_t>(kTextClassAssets[i].first) != i) {
            return false;
        }
    }
    return true;
}(), "kTextClassAssets rows must sit at their TextClass positions");

}  // namespace

std::span<const RomRegionEntry> romRegions() {
    return kRomRegions;
}

std::optional<retropp::MemoryRegion> romRegion(RomAsset asset, Language language) {
    const auto key = std::pair(static_cast<std::size_t>(asset),
                               static_cast<std::size_t>(language));
    const auto found = std::lower_bound(
        kRomRegions.begin(), kRomRegions.end(), key,
        [](const RomRegionEntry& row, const std::pair<std::size_t, std::size_t>& want) {
            return std::pair(static_cast<std::size_t>(row.asset),
                             static_cast<std::size_t>(row.language)) < want;
        });
    if (found == kRomRegions.end() || found->asset != asset ||
        found->language != language) {
        return std::nullopt;
    }
    return found->region;
}

RomAsset textClassAsset(TextClass klass) {
    const auto index = static_cast<std::size_t>(klass);
    assert(index < kTextClassAssets.size() && "text class out of range");
    return kTextClassAssets[index].second;
}

}  // namespace ostinato
