// The party as the game keeps it between battles: the sixteen character records, who is in which
// party and where they stand, the spells each character has learned, the party's other learned
// abilities and its inventory.
//
// This is in-memory game state, read and written by the battle (src/battle/) and by the field and
// menu code. It is plain data with no behavior beyond a few lookups.
//
//     game::Party party;
//     game::CharacterRecord& terra = party.characters[0];
//     terra.actor = CharacterPropId::TERRA;
//     terra.weapon = ItemId::MITHRILKNIFE;
//     party.membership[0] = {.party = 1, .slot = 0, .backRow = false};
//     party.currentParty = 1;
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "ostinato/ability_learned_set.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_gfx_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/status_set.h"

namespace ostinato::game {

inline constexpr std::size_t kCharacterRecords = 16;
inline constexpr std::size_t kInventorySlots = 256;

// The characters who learn spells from espers: the first twelve characters, Terra to Gau.
inline constexpr std::size_t kSpellLearners = 12;
// The spells an esper can teach: AttackId Fire ($00) to Life 3 ($35).
inline constexpr std::size_t kLearnableSpells = 54;
// Strago's lores, Condemned to Exploder.
inline constexpr std::size_t kLores = 24;

// One character the party can field: who they are, their progress and what they wear.
//
// `maxHp` and `maxMp` are the character's own maximums; equipment that raises them (a Muscle Belt,
// a Crystal Orb) is applied when the character enters a battle, not stored here.
struct CharacterRecord {
    CharacterPropId actor = CharacterPropId::TERRA;  // whose battle properties the character uses
    CharacterGfxId graphics{};
    std::array<std::uint8_t, 6> name{};  // in the game's text encoding
    std::uint8_t level = 1;
    std::uint16_t hp = 0;
    std::uint16_t maxHp = 0;
    std::uint16_t mp = 0;
    std::uint16_t maxMp = 0;
    std::uint32_t experience = 0;

    // The statuses that last between battles: Blind through Dead, plus Interceptor and Float.
    StatusSet status;

    // The four battle commands, in menu order. NONE leaves a row empty.
    std::array<BattleCommandId, 4> commands{BattleCommandId::NONE, BattleCommandId::NONE,
                                            BattleCommandId::NONE, BattleCommandId::NONE};

    std::uint8_t vigor = 0;
    std::uint8_t speed = 0;
    std::uint8_t stamina = 0;
    std::uint8_t magicPower = 0;

    std::optional<EsperId> esper;

    // What the character has equipped. EMPTY is an empty slot; an empty weapon or shield slot is
    // a bare hand.
    ItemId weapon = ItemId::EMPTY;
    ItemId shield = ItemId::EMPTY;
    ItemId helmet = ItemId::EMPTY;
    ItemId armor = ItemId::EMPTY;
    ItemId relic1 = ItemId::EMPTY;
    ItemId relic2 = ItemId::EMPTY;

    // The six equipment slots in order: weapon, shield, helmet, armor, relic 1, relic 2.
    constexpr std::array<ItemId, 6> equipment() const {
        return {weapon, shield, helmet, armor, relic1, relic2};
    }
};

// Which party a character record belongs to, the place it takes in that party's line-up and its
// row. Party 0 is no party; the game's parties are 1 to 3.
struct PartyMembership {
    std::uint8_t party = 0;
    std::uint8_t slot = 0;  // 0-3
    bool backRow = false;
};

// How far a character has learned each spell from its espers: a percentage, or kSpellLearned.
struct SpellProgress {
    static constexpr std::uint8_t kSpellLearned = 255;

    std::array<std::uint8_t, kLearnableSpells> progress{};

    // `spell` is one of the 54 learnable spells.
    constexpr bool knows(AttackId spell) const {
        return progress[static_cast<std::size_t>(spell)] == kSpellLearned;
    }
};

// One inventory slot. An empty slot holds EMPTY.
struct InventorySlot {
    ItemId item = ItemId::EMPTY;
    std::uint8_t quantity = 0;
};

// The time the game shows as played, in hours and minutes.
struct PlayTime {
    std::uint8_t hours = 0;
    std::uint8_t minutes = 0;
};

struct Party {
    std::array<CharacterRecord, kCharacterRecords> characters{};
    std::array<PartyMembership, kCharacterRecords> membership{};  // one per character record
    std::uint8_t currentParty = 1;                                // the party the player controls

    // The spells learned by each of the first twelve characters, indexed by their actor.
    std::array<SpellProgress, kSpellLearners> spells{};

    std::bitset<kLores> loresKnown;   // bit n: the n-th lore, Condemned first
    AbilityLearnedSet swdtechsKnown;
    AbilityLearnedSet blitzesKnown;
    std::bitset<8> dancesKnown;       // bit n: DanceId n
    std::bitset<256> ragesKnown;      // bit n: the rage of MonsterId n

    // How much longer Terra can stay morphed, as the Morph meter shows it.
    std::uint8_t morphSupply = 0;

    // The play time the game was last saved at.
    PlayTime playTimeAtLastSave;

    std::array<InventorySlot, kInventorySlots> inventory{};
};

}  // namespace ostinato::game
