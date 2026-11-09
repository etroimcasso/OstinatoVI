// Spells learned a little further at the end of a won battle, from an equipped esper or an equipped
// item that teaches a spell.
//
// Each spell a character can learn has a learned percentage in its game::SpellProgress. A battle
// won gives magic points (magicPointsForBattle() in data/battle_magic_points.h), and every spell an
// esper or an item teaches goes up by its learn rate times those magic points. At 100 % or more it
// is learned, and holds game::SpellProgress::kSpellJustLearned until the battle's "learned" message
// for it is shown.
//
//     game::SpellProgress& spells = party.spells[actor];
//     if (record.esper) {
//         battle::learnFromEsper(spells, *record.esper, magicPoints);
//     }
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_espers.md.
#pragma once

#include <cstdint>

#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/esper_id.h"

namespace ostinato::battle {

// One spell learned further by `learnRate` × `magicPoints` percent, the product kept to its low byte
// (a rate of 30 with 10 magic points adds 44). `spell` is one of the 54 learnable spells, Fire to
// Life 3.
//
// A rate of 0 and a spell already learned change nothing and return false. Otherwise the spell's
// percentage becomes the sum, or kSpellJustLearned when the sum is 100 or more, and the call
// returns true — with 0 magic points too, when the percentage stays as it was. A spell already
// kSpellJustLearned stays so.
bool addSpellProgress(game::SpellProgress& progress, AttackId spell, std::uint8_t learnRate,
                      std::uint8_t magicPoints);

// The five spells `esper` teaches (getEsperProperties() in data/esper_properties.h), each learned
// further in the esper's order through addSpellProgress(); an empty slot is passed over. Returns
// true when any of the five calls did.
bool learnFromEsper(game::SpellProgress& progress, EsperId esper, std::uint8_t magicPoints);

}  // namespace ostinato::battle
