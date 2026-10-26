// The party's side of a battle's start: who stands in the four party slots, and each party
// member's battle stats, statuses, commands and spells, worked out from their record and their
// equipment.
//
// A battle start runs these in order (with the battle's own setup between them):
//
//     const BattlerSet fromSetup = placeParty(state, {.fighters = CurrentParty{}, .scriptedSetup = setup});
//     loadParty(state, fromSetup, party.playTimeAtLastSave);
//     loadBattleInventory(state);   // battle/battle_inventory.h
//     loadKnownSkills(state);
//
// and during the battle, after the Item menu changes what a party member holds, the battle loop
// calls applyEquipmentChanges().
//
// The rules, and the line in the original each comes from, are in docs/contracts/battle_core.md.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include "battle/battle_state.h"
#include "game/character_record.h"
#include "ostinato/char_ai_id.h"
#include "ostinato/character_prop_id.h"

namespace ostinato::battle {

class ActionExecutor;

// How much equipment raises a character's maximum HP or MP. Only the largest boost worn counts.
enum class MaxBoost : std::uint8_t {
    None,
    Plus12Point5Percent,
    Plus25Percent,
    Plus50Percent,
};

// A character's stats with everything they have equipped.
struct DerivedStats {
    // Clamped to 0-255, except vigor, which a Hyper Wrist raises by half again after the clamp.
    int vigor = 0;
    int speed = 0;
    int stamina = 0;
    int magicPower = 0;
    int evade = 0;          // the evade rating, 0-255
    int magicBlock = 0;
    std::uint8_t defense = 0;
    std::uint8_t magicDefense = 0;

    Hand weaponHand;
    Hand shieldHand;
    bool twoWeapons = false;  // a weapon in each hand
    ItemId armor = ItemId::EMPTY;
    std::array<ItemId, 2> relics{ItemId::EMPTY, ItemId::EMPTY};

    ElementSet elementsAbsorbed;
    ElementSet elementsNullified;
    ElementSet elementsWeak;
    ElementSet elementsHalved;
    StatusSet protection;          // the statuses the equipment protects against
    StatusSet equipmentStatuses;   // the statuses the equipment keeps on its wearer
    RelicEffect1Set relicEffects1;
    RelicEffect2Set relicEffects2;
    RelicEffect3Set relicEffects3;
    RelicEffect4Set relicEffects4;
    RelicEffect5Set relicEffects5;
    BlockGraphicSet physicalBlockGraphics;
    BlockGraphicSet magicBlockGraphics;
    std::uint8_t runFactor = 0;    // 2-5: the most one escape attempt can add

    MaxBoost hpBoost = MaxBoost::None;
    MaxBoost mpBoost = MaxBoost::None;
};

// The stats the character in `record` fights with, from their base battle properties, their own
// stats and their six equipment slots.
DerivedStats deriveEquipment(const game::CharacterRecord& record);

// A maximum raised by a boost: 12.5%, 25% or 50% more, rounding down.
int boostedMaximum(int maximum, MaxBoost boost);

// Give the party member in `slot` the stats in `stats`.
void applyDerivedStats(BattleState& state, BattlerSlot slot, const DerivedStats& stats);

// An evade or magic-block rating as the hit check reads it (Battler::evadeFactor): 255 for a
// rating of 0, 256 less twice the rating up to 127, and 1 from 128 on.
std::uint8_t hitCheckFactor(int rating);

// A battler that cannot be given Dead is protected from instant death instead, and Dead becomes
// a status that can be put on it; a battler that nullifies poison cannot be poisoned. Party members
// and monsters both get this once their immunities are set.
void applyDeathAndPoisonImmunity(Battler& battler);

// Narrow which statuses can be put on the battler in `slot`: not Float while its equipment keeps
// Float on, not Morph with a permanent morph, not any of Silence, Berserk, Confuse, Sap, Regen,
// Slow, Haste, Shell, Protect and Reflect its equipment keeps on it; and Regen and Sap, like Slow
// and Haste, either both or neither. Nothing here makes a status possible again.
void refreshStatusImmunity(BattleState& state, BattlerSlot slot);

// Who stands in the party slots.

// The party the player controls, in the slots it is lined up in.
struct CurrentParty {};

// A colosseum match: one fighter in the first slot.
struct ColosseumFighter {
    CharacterPropId fighter = CharacterPropId::TERRA;
};

// A stage of the final battle: the fighters still standing from the stage before, by slot (all
// empty for the first stage). An empty slot takes the next character waiting in
// BattleState::finalBattleReserve.
struct FinalBattleStage {
    std::array<std::optional<CharacterPropId>, 4> standing{};
};

struct PartyLineup {
    std::variant<CurrentParty, ColosseumFighter, FinalBattleStage> fighters;

    // A scripted battle (CharAI) the formation stages: its characters take the slot of the party
    // member they match, or an empty one.
    std::optional<CharAiId> scriptedSetup;
};

// Fill the four party slots: each party member's actor, record, graphics, row, presence, which
// side it fights on, and the items it may hold. A Veldt battle may stage Gau's return
// (BattleState::gauMayAppear, which this clears when Gau cannot come back now). Returns the
// party slots a scripted setup or a colosseum match filled; loadParty() heals those characters to
// full.
BattlerSet placeParty(BattleState& state, const PartyLineup& lineup);

// Load every placed party member: the party's spell list, then for each, their equipment, HP, MP
// and statuses, their own spell list and their battle commands.
void loadParty(BattleState& state, BattlerSet joinedFromSetup, game::PlayTime savedPlayTime);

// The party member in `slot` takes their HP, MP, level and lasting statuses from their record,
// their maximum HP and MP raised by `stats`' boosts; `joinedFromSetup` heals them to full.
void loadPartyMember(BattleState& state, BattlerSlot slot, const DerivedStats& stats, bool joinedFromSetup);

// Build the party member in `slot`'s battle command menu from their record.
void buildCommandList(BattleState& state, BattlerSlot slot);

// Re-derive every party member whose equipment the battle's Item menu changed.
void applyEquipmentChanges(BattleState& state, ActionExecutor& executor);

}  // namespace ostinato::battle
