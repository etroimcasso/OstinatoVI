// The runtime version axis: one binary serves all three supported ROM
// revisions. This mirrors the upstream assembly-config axes as clean C++
// predicates — LANG_EN and ROM_VERSION in const.inc — without any
// assembler-config or hardware idiom leaking into the port surface. Which
// revision a cartridge is lives next to the checksums that answer it, in
// ostinato/rom_identity.h.
#pragma once

#include <cstdint>

namespace ostinato {

// The three supported ROM revisions. Ordering is arbitrary but stable; values
// are never persisted as-is (a save/pack records the identified revision).
enum class GameVersion : std::uint8_t {
    JP_1_0,  // Final Fantasy VI (J) 1.0     — LANG_EN=0, ROM_VERSION=0
    US_1_0,  // Final Fantasy III (US) 1.0    — LANG_EN=1, ROM_VERSION=0
    US_1_1,  // Final Fantasy III (US) 1.1    — LANG_EN=1, ROM_VERSION=1 (REV1)
};

// The script/text language a version ships. Upstream: LANG_EN.
enum class Language : std::uint8_t {
    JP,
    EN,
};

// Language of a version — upstream LANG_EN (the Japanese original vs the two
// US localizations).
constexpr Language language(GameVersion version) {
    return version == GameVersion::JP_1_0 ? Language::JP : Language::EN;
}

// The 1.1 US revision — upstream LANG_EN_REV1 = (LANG_EN && ROM_VERSION), which
// is true only for the US 1.1 ROM. Version-conditional data/behavior keyed on
// the revision-1 bug fixes test this.
constexpr bool isRevision1(GameVersion version) {
    return version == GameVersion::US_1_1;
}

}  // namespace ostinato
