#include "data/battle_backgrounds.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 56 battle backgrounds in BATTLE_BG index order.
constexpr std::array<BattleBackgroundPropertiesEntry, kBattleBackgroundCount>
    kBattleBackgroundProperties = {{
#include "data/generated/battle_bg_prop_data.inc"
}};

constexpr bool idMatchesPosition() {
    for (std::size_t i = 0; i < kBattleBackgroundProperties.size(); ++i) {
        if (static_cast<std::size_t>(kBattleBackgroundProperties[i].id) != i) {
            return false;
        }
    }
    return true;
}

static_assert(idMatchesPosition(),
              "kBattleBackgroundProperties id fields must match array positions");

}  // namespace

const BattleBackgroundProperties& battleBackgroundProperties(
    BattleBackgroundId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kBattleBackgroundProperties.size()
           && "battle background id has no properties row");
    return kBattleBackgroundProperties[index].record;
}

std::span<const BattleBackgroundPropertiesEntry> battleBackgroundTable() {
    return kBattleBackgroundProperties;
}

}  // namespace ostinato
