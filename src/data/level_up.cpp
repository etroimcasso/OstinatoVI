#include "data/level_up.h"

#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// Every progression table is in level order starting at the first progression
// level, so a level indexes its own row. The accessors rest on that.
template <typename Table>
constexpr bool levelOrder(const Table& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].level != kFirstProgressionLevel + i) {
            return false;
        }
    }
    return true;
}

constexpr bool levelModOrder() {
    for (std::size_t i = 0; i < kCharacterLevelModifiers.size(); ++i) {
        const auto expected =
            static_cast<LevelMod>(i * (static_cast<std::size_t>(LevelMod::HIGH)));
        if (kCharacterLevelModifiers[i].mod != expected) {
            return false;
        }
    }
    return true;
}

static_assert(levelOrder(kLevelUpExp), "kLevelUpExp must be in level order");
static_assert(levelOrder(kLevelUpHp), "kLevelUpHp must be in level order");
static_assert(levelOrder(kLevelUpMpEn), "kLevelUpMpEn must be in level order");
static_assert(levelOrder(kLevelUpMpJp), "kLevelUpMpJp must be in level order");
static_assert(levelModOrder(),
              "kCharacterLevelModifiers must be in level-mod order");
static_assert(kLevelUpExp.back().level == kMaxLevel,
              "the progression tables must end at the maximum level");

std::size_t levelIndex(std::uint8_t level) {
    assert(level >= kFirstProgressionLevel && level <= kMaxLevel &&
           "level out of range");
    return static_cast<std::size_t>(level - kFirstProgressionLevel);
}

}  // namespace

std::uint16_t levelUpExp(std::uint8_t level) {
    return kLevelUpExp[levelIndex(level)].exp;
}

std::uint8_t levelUpHp(std::uint8_t level) {
    return kLevelUpHp[levelIndex(level)].gain;
}

std::span<const LevelUpStatEntry> levelUpMp(GameVersion version) {
    return language(version) == Language::JP ? std::span<const LevelUpStatEntry>(kLevelUpMpJp)
                                             : std::span<const LevelUpStatEntry>(kLevelUpMpEn);
}

std::uint8_t levelUpMp(GameVersion version, std::uint8_t level) {
    return levelUpMp(version)[levelIndex(level)].gain;
}

std::span<const AbilityLearnLevelEntry> abilityLearnLevels(
    BattleCommandId command) {
    assert((command == BattleCommandId::BUSHIDO ||
            command == BattleCommandId::BLITZ) &&
           "only bushido and blitz have level-gated abilities");
    return command == BattleCommandId::BUSHIDO
               ? std::span<const AbilityLearnLevelEntry>(kBushidoLearnLevels)
               : std::span<const AbilityLearnLevelEntry>(kBlitzLearnLevels);
}

std::int8_t characterLevelModifier(LevelMod mod) {
    const auto i = static_cast<std::size_t>(mod) /
                   static_cast<std::size_t>(LevelMod::HIGH);
    assert(i < kCharacterLevelModifiers.size() && "level modifier out of range");
    return kCharacterLevelModifiers[i].levels;
}

}  // namespace ostinato
