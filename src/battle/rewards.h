// What a won battle gives the party: the fallen monsters' experience and gold, each living party
// member's share of the experience, the levels it reaches, the abilities a level teaches, the spells
// its equipment teaches, the lores Strago saw, Mog's dance, the Morph supply and the rages.
//
// Each rule here is a function over the party and the battle that changes what the rule changes and
// returns what the win tells the player about. None of them shows a message: the win calls them in
// the cartridge's order and shows each message as it goes.
//
//     const battle::BattleSpoils spoils = battle::spoilsOf(state);
//     const std::uint32_t share = battle::experienceShare(spoils.experience, state.allyCount);
//     game::CharacterRecord& record = state.party.characters[*battler.record];
//     battle::addExperience(record, share);
//     while (battle::earnedNextLevel(record)) {
//         battle::levelUp(record, state.version);
//         battle::learnAbilitiesAtLevel(state.party, record.actor, record.level);
//     }
//
// Experience and gil are 24-bit amounts. A sum that passes 24 bits keeps its low 24 bits and is then
// held to kMaxExperience or kMaxGil (ostinato/game_limits.h), so a share of 16,777,215 added to 1
// leaves 0.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_rewards.md.
#pragma once

#include <bitset>
#include <cstdint>
#include <optional>

#include "battle/battle_state.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/dance_id.h"

namespace ostinato::battle {

// The experience and gold of every monster that fell: one with Dead, Petrify or Zombie when the
// battle is won. A monster that fled or was sent away gives neither. On the Veldt the monsters give
// their gold and no experience.
struct BattleSpoils {
    std::uint32_t experience = 0;
    std::uint32_t gold = 0;
};

[[nodiscard]] BattleSpoils spoilsOf(const BattleState& state);

// Each ally's share of `total` experience: `total` divided by `allies`, the remainder dropped. The
// console's divider answers a divisor of 0, so a share for no allies is 16,777,215; a won battle
// always has an ally alive.
[[nodiscard]] std::uint32_t experienceShare(std::uint32_t total, std::uint8_t allies);

// `share` added to the record's experience: the sum's low 24 bits, held to kMaxExperience. The level
// is not looked at, so a character at 99 still gains experience.
void addExperience(game::CharacterRecord& record, std::uint32_t share);

// Whether the record has the experience its next level needs: 8 times the sum of the steps up to it
// (levelUpExp() in data/level_up.h). False at level 99. The level must be 1 or more.
[[nodiscard]] bool earnedNextLevel(const game::CharacterRecord& record);

// What a level teaches: a SwdTech Cyan did not know, a Blitz Sabin did not know.
struct AbilitiesLearned {
    bool swdtech = false;
    bool blitz = false;
};

// The abilities `actor` learns on reaching `level`. Terra and Celes learn each of their natural
// spells whose level is exactly `level` (kNaturalMagicTerra / kNaturalMagicCeles), each marked
// game::SpellProgress::kSpellJustLearned unless already learned; nothing is returned for them, since
// the spells are told of once the levels are done. Cyan learns the first SwdTech taught at `level`
// and Sabin the first Blitz (abilityLearnLevels() in data/level_up.h); one already known is learned
// again and not returned. Anyone else learns nothing.
AbilitiesLearned learnAbilitiesAtLevel(game::Party& party, CharacterPropId actor, std::uint8_t level);

// What the equipment's spells did: whether any spell was learned further, and whether the cursed
// shield's curse came off.
struct EquipmentLearning {
    bool progressed = false;
    bool curseDispelled = false;
};

// The spells the record's six pieces of equipment teach, learned further into `spells` in
// equipment order — weapon, shield, helmet, armor, relic 1, relic 2 — each through
// addSpellProgress() (battle/spell_learning.h) at its item's learn rate; an empty slot is passed
// over.
//
// Each slot holding the Cursed Shield adds one to the battles won with it
// (game::BattleSwitches::cursedShieldBattles()). When the count comes round to 0, at the 256th, the
// slot's shield becomes the Paladin Shield, which then teaches its spell in this same win. Two
// slots holding the shield count twice.
EquipmentLearning learnFromEquipment(game::CharacterRecord& record, game::SpellProgress& spells,
                                     game::BattleSwitches& switches, std::uint8_t magicPoints);

// The lores Strago saw this battle, added to the lores the party knows. The win tells of each one
// it was handed, Exploder first.
void learnLores(game::Party& party, const std::bitset<game::kLores>& learnedThisBattle);

// The dance of the battle's background as it now stands (backgroundDance() in
// data/battle_tables.h), learned when Mog is one of the party members in the battle — fallen or
// not. Returns the dance when the party did not know it, and nothing otherwise.
std::optional<DanceId> learnDance(game::Party& party, const BattleState& state);

// Terra's Morph supply after a win with `magicPoints`: twice the magic points added to it, held to
// 255. The doubling keeps its low byte and carries its top bit into the sum, so 128 magic points add
// 1 and 200 add 145.
void gainMorphSupply(game::Party& party, std::uint8_t magicPoints);

// The rage of every monster in the battle's six monster slots, alive, fallen or gone, added to the
// rages the party knows. A monster numbered 256 or more has no rage; an empty slot is passed over.
void learnRages(game::Party& party, const BattleState& state);

}  // namespace ostinato::battle
