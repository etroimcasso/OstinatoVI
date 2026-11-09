#include "battle/leveling.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "data/esper_properties.h"
#include "data/level_up.h"

namespace ostinato::battle {

namespace {

// The percentage bonuses add these 256ths of the gain: 26/256 = 10.16 %, 78/256 = 30.47 %,
// 128/256 = 50 %.
constexpr int kTenPercentOf256 = 26;
constexpr int kThirtyPercentOf256 = 78;
constexpr int kFiftyPercentOf256 = 128;

// An esper's stat bonus raises a stat to at most this.
constexpr int kStatBonusMax = 128;

using BonusRule = void (*)(LevelUpGains&, game::CharacterRecord&);

// The gain raised by `fraction`/256 of itself, rounded down but never by less than 1.
int raisedByFraction(int gain, int fraction) {
    const int extra = gain * fraction / 256;
    return gain + std::max(extra, 1);
}

template <int Fraction>
void raiseHpGain(LevelUpGains& gains, game::CharacterRecord&) {
    gains.hp = raisedByFraction(gains.hp, Fraction);
}

template <int Fraction>
void raiseMpGain(LevelUpGains& gains, game::CharacterRecord&) {
    gains.mp = raisedByFraction(gains.mp, Fraction);
}

// "+100%": the HP gain doubled, with no least amount.
void doubleHpGain(LevelUpGains& gains, game::CharacterRecord&) {
    gains.hp *= 2;
}

void noLevelUpBonus(LevelUpGains&, game::CharacterRecord&) {}

// The stat raised by `By` within its byte, then held to at most kStatBonusMax: a stat of 255 raised
// by 1, or 254 by 2, comes round to 0, and 255 by 2 to 1, each below the limit and kept.
template <std::uint8_t game::CharacterRecord::*Stat, int By>
void raiseStat(LevelUpGains&, game::CharacterRecord& record) {
    std::uint8_t& stat = record.*Stat;
    const auto raised = static_cast<std::uint8_t>(stat + By);
    stat = static_cast<std::uint8_t>(std::min<int>(raised, kStatBonusMax));
}

struct EsperBonusRule {
    EsperBonus bonus;
    BonusRule rule;
};

// One rule per bonus, in the bonus's order.
constexpr std::array<EsperBonusRule, 17> kEsperBonusRules{{
    {EsperBonus::HP_10, raiseHpGain<kTenPercentOf256>},
    {EsperBonus::HP_30, raiseHpGain<kThirtyPercentOf256>},
    {EsperBonus::HP_50, raiseHpGain<kFiftyPercentOf256>},
    {EsperBonus::MP_10, raiseMpGain<kTenPercentOf256>},
    {EsperBonus::MP_30, raiseMpGain<kThirtyPercentOf256>},
    {EsperBonus::MP_50, raiseMpGain<kFiftyPercentOf256>},
    {EsperBonus::HP_100, doubleHpGain},
    {EsperBonus::LV_30, noLevelUpBonus},
    {EsperBonus::LV_50, noLevelUpBonus},
    {EsperBonus::STRENGTH_1, raiseStat<&game::CharacterRecord::vigor, 1>},
    {EsperBonus::STRENGTH_2, raiseStat<&game::CharacterRecord::vigor, 2>},
    {EsperBonus::SPEED_1, raiseStat<&game::CharacterRecord::speed, 1>},
    {EsperBonus::SPEED_2, raiseStat<&game::CharacterRecord::speed, 2>},
    {EsperBonus::STAMINA_1, raiseStat<&game::CharacterRecord::stamina, 1>},
    {EsperBonus::STAMINA_2, raiseStat<&game::CharacterRecord::stamina, 2>},
    {EsperBonus::MAGPWR_1, raiseStat<&game::CharacterRecord::magicPower, 1>},
    {EsperBonus::MAGPWR_2, raiseStat<&game::CharacterRecord::magicPower, 2>},
}};

static_assert([] {
    for (std::size_t i = 0; i < kEsperBonusRules.size(); ++i) {
        if (static_cast<std::size_t>(kEsperBonusRules[i].bonus) != i) {
            return false;
        }
    }
    return true;
}(), "kEsperBonusRules holds one rule per bonus, at the bonus's own place");

}  // namespace

void applyEsperBonus(EsperBonus bonus, LevelUpGains& gains, game::CharacterRecord& record) {
    if (bonus == EsperBonus::NONE) {
        return;
    }
    const auto index = static_cast<std::size_t>(bonus);
    assert(index < kEsperBonusRules.size() && "an esper's bonus is one of the seventeen or NONE");
    kEsperBonusRules[index].rule(gains, record);
}

void levelUp(game::CharacterRecord& record, GameVersion version) {
    assert(record.level < kMaxLevel && "the game never levels up a character at 99");
    ++record.level;
    LevelUpGains gains{.hp = levelUpHp(record.level), .mp = levelUpMp(version, record.level)};
    if (record.esper) {
        applyEsperBonus(getEsperProperties(*record.esper).bonus, gains, record);
    }
    record.maxHp = static_cast<std::uint16_t>(std::min(record.maxHp + gains.hp, game::kMaxHp));
    record.maxMp = static_cast<std::uint16_t>(std::min(record.maxMp + gains.mp, game::kMaxMp));
}

}  // namespace ostinato::battle
