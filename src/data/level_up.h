// Character level progression: the experience needed for each level, the HP and
// MP gained on reaching it, the levels the swdtechs and blitzes are learned at,
// and the level a character joins the party at. The row data is generated
// (src/data/generated/level_up_data.inc); this header owns the entry types and
// the accessors.
//
// Every progression row is keyed by the level it applies to, so a row reads as
// "at level N, gain this much". Levels run from 2 (nothing is gained on reaching
// level 1) to 99.
//
// The MP curve is the port's first version-forked table: the Japanese release
// levels magic users differently, so the table is selected by GameVersion.
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "ostinato/ability_learned_set.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/game_version.h"
#include "ostinato/level_mod.h"

namespace ostinato {

// The level a character joins at, relative to the party average.
struct CharacterLevelModifierEntry {
    LevelMod mod;
    std::int8_t levels;
};

// The abilities a character has learned once they have reached the level of
// `learnedCount` of them (LearnAbilityTbl).
struct LearnedAbilityFlagsEntry {
    std::uint8_t learnedCount;
    AbilityLearnedSet abilities;
};

// The level one swdtech or blitz is learned at.
struct AbilityLearnLevelEntry {
    AttackId ability;
    std::uint8_t level;
};

// The experience step from the previous level to this one.
struct LevelUpExpEntry {
    std::uint8_t level;
    std::uint16_t exp;
};

// The HP or MP gained on reaching a level.
struct LevelUpStatEntry {
    std::uint8_t level;
    std::uint8_t gain;
};

// The progression tables. Public constants; verified in full by the tests.
#include "data/generated/level_up_data.inc"

// The lowest and highest character level. Level 1 has no progression row.
inline constexpr std::uint8_t kFirstProgressionLevel = 2;
inline constexpr std::uint8_t kMaxLevel = 99;

// --- accessors ---------------------------------------------------------------

// The experience step from the previous level to `level` (2-99). The total for a
// level is the sum of every step below it, times 8.
std::uint16_t levelUpExp(std::uint8_t level);

// The max HP gained on reaching `level` (2-99).
std::uint8_t levelUpHp(std::uint8_t level);

// The max MP gained on reaching `level` (2-99), on the given version's curve.
std::uint8_t levelUpMp(GameVersion version, std::uint8_t level);

// The whole MP curve for a version, in level order.
std::span<const LevelUpStatEntry> levelUpMp(GameVersion version);

// The levels a command's abilities are learned at, in teaching order. `command`
// must be BUSHIDO or BLITZ — the two commands whose abilities are level-gated.
std::span<const AbilityLearnLevelEntry> abilityLearnLevels(
    BattleCommandId command);

// The level offset a joining character gets relative to the party average. The
// character record stores the setting in a field that needs masking and
// shifting; pass the decoded LevelMod.
std::int8_t characterLevelModifier(LevelMod mod);

}  // namespace ostinato
