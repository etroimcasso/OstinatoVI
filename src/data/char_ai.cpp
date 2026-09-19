#include "data/char_ai.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The 24 scripted battle setups in CHAR_AI index order.
constexpr std::array<CharAiSetupEntry, kCharAiSetupCount> kCharAiSetups = {{
#include "data/generated/char_ai_data.inc"
}};

constexpr bool idMatchesPosition() {
    for (std::size_t i = 0; i < kCharAiSetups.size(); ++i) {
        if (static_cast<std::size_t>(kCharAiSetups[i].id) != i) {
            return false;
        }
    }
    return true;
}

static_assert(idMatchesPosition(),
              "kCharAiSetups id fields must match array positions");

}  // namespace

const CharAiSetup& charAiSetup(CharAiId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kCharAiSetups.size() && "char a.i. id has no setup row");
    return kCharAiSetups[index].record;
}

std::span<const CharAiSetupEntry> charAiSetups() { return kCharAiSetups; }

}  // namespace ostinato
