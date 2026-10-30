#include "render/battle/party_status.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "data/battle_gfx_tables.h"
#include "ostinato/game_version.h"
#include "render/battle/hud_text.h"
#include "render/battle/layout.h"
#include "render/layers.h"

namespace ostinato::render {

namespace {

// The gauge's caps sit just past its cells' glyphs (btlgfx_ram.inc:8-16): the left cap nine past
// the empty cell, the right cap ten.
constexpr std::uint8_t kGaugeLeftCapAfterEmpty = 9;
constexpr std::uint8_t kGaugeRightCapAfterEmpty = 10;

// "~", before the max HP when the gauge is turned off (DrawCharInfo_02 :15897).
constexpr std::uint8_t kTildeGlyph = 0xce;

// A gauge's fill shifted right by three is its row of cells (DrawGaugeText :15851-15852).
constexpr int kGaugeStepShift = 3;

constexpr int kHpDigits = 4;

std::uint8_t digitGlyph(int digit, Language language) {
    return numeralGlyphs(language)[static_cast<std::size_t>(digit)].glyph;
}

// A four-digit number with its leading zeros left blank and its last digit always shown
// (HexToDec4 :15967-15980, TrimZeroes4 :3646-3657, DrawNum4 :16022-16029).
std::vector<std::uint8_t> numberCodes(std::uint16_t value, Language language) {
    std::vector<std::uint8_t> codes(kHpDigits);
    for (int i = kHpDigits - 1; i >= 0; --i) {
        codes[static_cast<std::size_t>(i)] = digitGlyph(value % 10, language);
        value /= 10;
    }
    for (int i = 0; i < kHpDigits - 1 && codes[static_cast<std::size_t>(i)] == digitGlyph(0, language); ++i) {
        codes[static_cast<std::size_t>(i)] = kHudBlank;
    }
    return codes;
}

std::vector<std::uint8_t> gaugeCodes(std::uint8_t fill, Language language) {
    const std::uint8_t empty = gaugeEmptyGlyph(language);
    std::vector<std::uint8_t> codes{static_cast<std::uint8_t>(empty + kGaugeLeftCapAfterEmpty)};
    for (const std::uint8_t cell : gaugeCellFills(fill >> kGaugeStepShift)) {
        codes.push_back(static_cast<std::uint8_t>(empty + cell));
    }
    codes.push_back(static_cast<std::uint8_t>(empty + kGaugeRightCapAfterEmpty));
    return codes;
}

}  // namespace

Sprites PartyStatus(const battle::BattleState& battle, const SmallFontPaletteIds& palettes,
                    const retropp::AtlasManifest& smallFont) {
    const Language lang = language(battle.version);
    const auto columnX = [&](BattleTextPlacementId part) { return battleTextPlacement(part, lang).column * kBattleCellPx; };
    Sprites sprites;
    const auto add = [&](const std::string& part, int slot, std::span<const std::uint8_t> codes, int x,
                         std::size_t palette) {
        const Sprites letters = HudText(std::string{kHudGlyphKey} + ":" + part + ":" + std::to_string(slot), codes, x,
                                        hudLineY(slot), palettes[palette], smallFont);
        sprites.insert(sprites.end(), letters.begin(), letters.end());
    };
    for (int slot = 0; slot < battle::kPartySlots; ++slot) {
        const battle::Battler& member = battle.at(battle::BattlerSlot::character(slot));
        if (!member.present || !member.record) {
            continue;
        }
        add("name", slot, battle.party.characters[*member.record].name, columnX(BattleTextPlacementId::CHARACTER_NAMES),
            kHudTextPalette);
        add("hp", slot, numberCodes(member.hp, lang), columnX(BattleTextPlacementId::HP), kHudTextPalette);
        const int gaugeX = columnX(BattleTextPlacementId::GAUGES);
        if (member.morphGauge != 0) {
            add("gauge", slot, gaugeCodes(member.morphGauge, lang), gaugeX, kMorphGaugePalette);
        } else if (battle.config.gaugeShown) {
            add("gauge", slot, gaugeCodes(member.gauge.fillLevel(), lang), gaugeX,
                member.gauge.isFull() ? kFullGaugePalette : kFillingGaugePalette);
        } else {
            std::vector<std::uint8_t> codes{kTildeGlyph};
            const std::vector<std::uint8_t> maxHp = numberCodes(member.maxHp, lang);
            codes.insert(codes.end(), maxHp.begin(), maxHp.end());
            add("gauge", slot, codes, gaugeX, kHudTextPalette);
        }
    }
    return sprites;
}

}  // namespace ostinato::render
