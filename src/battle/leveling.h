// A character's level-up: one level more, the maximum HP and MP it gains, and the equipped esper's
// level-up bonus.
//
// A won battle adds its experience to each party member's record and then, for as long as a
// record's experience reaches the total for its next level, levels it up once more. Each level-up
// raises the level first and reads the HP and MP gained on reaching the new level
// (levelUpHp()/levelUpMp() in data/level_up.h). The equipped esper's bonus (EsperProperties::bonus)
// then raises the HP or MP gained, or raises one stat. The new maximums stop at game::kMaxHp and
// game::kMaxMp.
//
//     game::CharacterRecord& record = party.characters[0];
//     if (record.level < kMaxLevel) {
//         battle::levelUp(record, GameVersion::US_1_1);
//     }
//
// The percentage bonuses are 256ths of the gain: "+10%" adds 26/256 of it (10.16 %), "+30%" 78/256
// (30.47 %) and "+50%" half, each at least 1; "+100%" doubles the HP gain. A stat bonus adds 1 or 2
// within the stat's byte and stops at 128, so a stat of 254 or 255 comes round to 0 or 1. LV_30 and
// LV_50 change nothing here.
//
// A level the story gives a character is not a level-up: it sums the curves with no esper bonus.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_espers.md.
#pragma once

#include "game/character_record.h"
#include "ostinato/esper_bonus.h"
#include "ostinato/game_version.h"

namespace ostinato::battle {

// The maximum HP and MP one level-up adds.
struct LevelUpGains {
    int hp = 0;
    int mp = 0;
};

// One esper bonus applied to a level-up: a percentage bonus raises `gains`, a stat bonus raises
// that stat of `record` (vigor, speed, stamina or magic power). NONE, LV_30 and LV_50 change
// nothing.
void applyEsperBonus(EsperBonus bonus, LevelUpGains& gains, game::CharacterRecord& record);

// One level-up of `record`, with its equipped esper's bonus. The MP gained comes from `version`'s
// curve. The level must be below kMaxLevel (data/level_up.h); the game never levels up a character
// at 99.
void levelUp(game::CharacterRecord& record, GameVersion version);

}  // namespace ostinato::battle
