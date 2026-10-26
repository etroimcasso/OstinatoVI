// The spells a party member can cast in battle, and what each costs them.
//
// When a battle starts, every spell any party member knows goes into one list, laid out by the
// player's spell-order setting, with the lores after it. Each party member then gets their own
// copy of that list, keeping the spells they know themselves (Gogo keeps them all) at the
// positions the whole party's list gives them, so every party member's magic menu has the same
// layout. MP costs are each member's own: a Gold Hairpin halves them, an Economizer makes them 1.
//
//     buildSpellLists(state, savedPlayTime);   // once, when the battle starts
//     loadSpellList(state, slot);              // per party member, after its equipment
//     refreshCastableSpells(state, slot);      // whenever MP or Imp may have changed
//
// The rules, and the line in the original each comes from, are in docs/contracts/battle_core.md.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "battle/battler_set.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/targeting.h"

namespace ostinato::battle {

struct BattleState;

// One spell in a party member's list.
struct SpellListEntry {
    AttackId spell{};
    Targeting targeting;
    std::uint8_t mpCost = 0;  // what this party member pays
    bool castable = false;    // enough MP, and not barred by Imp; see refreshCastableSpells()
};

// A party member's battle spell list: the esper they can summon, then the spells and lores at
// the positions the party's list gives them. An empty position is a spell someone else in the
// party knows, or a gap the layout leaves.
struct SpellList {
    std::optional<EsperId> esper;
    SpellListEntry summon;  // the esper's summon; meaningful only while `esper` is set
    std::array<std::optional<SpellListEntry>, game::kLearnableSpells> magic{};
    std::array<std::optional<SpellListEntry>, game::kLores> lores{};

    // The entry for `spell` among the spells and lores, if this member has it.
    const SpellListEntry* find(AttackId spell) const;
};

// Build the party's shared spell list from every party member's learned spells and the party's
// known lores, and give each party member a copy of it. Step Mine's MP cost is one per half hour
// of `savedPlayTime`, the play time when the game was last saved.
void buildSpellLists(BattleState& state, game::PlayTime savedPlayTime);

// Narrow the party member in `slot` to the spells they know and price them with their equipment;
// add their equipped esper. Counts the spells they keep (not lores) into Battler::spellCount.
void loadSpellList(BattleState& state, BattlerSlot slot);

// Mark which of a party member's spells they can cast right now: those costing no more than their
// current MP and less than 255, and, while they are an Imp, only Imp. An esper they have already
// summoned this battle stays uncastable. Monsters have no list and are left alone.
void refreshCastableSpells(BattleState& state, BattlerSlot slot);

}  // namespace ostinato::battle
