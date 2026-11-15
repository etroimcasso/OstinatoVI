// The party's command menus, spell lists and skills, laid into the cells as the cartridge's start
// leaves them: the four command menus, the four spell lists with the party's own list and every
// attack's place in it, the espers equipped, and the SwdTechs, Blitzes, lores, rages and dances known.

#include "oracle/damage_oracle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/spell_lists.h"
#include "game/character_record.h"

#include "cells/battle/state.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// The spell list's entries: the esper first, then the spells by place, then the lores by number.
constexpr std::size_t kEsperEntry = 0;
constexpr std::size_t kFirstSpellEntry = 1;
constexpr std::size_t kFirstLoreEntry = kFirstSpellEntry + game::kLearnableSpells;
constexpr std::size_t kSpellEntries = kFirstLoreEntry + game::kLores;

// The four bytes of a spell-list entry.
struct SpellBytes {
    std::uint8_t spell = kNoSpell;
    std::uint8_t notCastable = kNotCastableBit;
    std::uint8_t targeting = kNoSpell;
    std::uint8_t mpCost = kNoSpell;
};

// The party's own list, place by place: the entry the party has at each place, from whichever party
// slot's list holds it (every list is built from the same one, and a member keeps what it knows).
struct PartyList {
    std::array<std::optional<battle::SpellListEntry>, game::kLearnableSpells> magic{};
    std::array<std::optional<battle::SpellListEntry>, game::kLores> lores{};
};

PartyList partyList(const battle::BattleState& state) {
    PartyList list;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::SpellList& own = state.at(battle::BattlerSlot::character(position)).spells;
        for (std::size_t place = 0; place < list.magic.size(); ++place) {
            if (own.magic[place] && !list.magic[place]) {
                list.magic[place] = own.magic[place];
            }
        }
        for (std::size_t lore = 0; lore < list.lores.size(); ++lore) {
            if (own.lores[lore] && !list.lores[lore]) {
                list.lores[lore] = own.lores[lore];
            }
        }
    }
    return list;
}

// A party slot's entry: what it has. Where it has nothing, the place keeps the party's targeting, or
// the fill's where no one has the place; and a slot with a party member in it has the entry's cost
// cleared (ValidateSpellList's AddToSpellList_01 :14279-14284), where an empty slot's list, never
// validated, keeps the fill's.
SpellBytes entryBytes(const std::optional<battle::SpellListEntry>& own,
                      const std::optional<battle::SpellListEntry>& party, std::uint8_t spellByte, bool seated) {
    if (own) {
        return SpellBytes{.spell = spellByte,
                          .notCastable = own->castable ? std::uint8_t{0} : kNotCastableBit,
                          .targeting = own->targeting.bits,
                          .mpCost = own->mpCost};
    }
    SpellBytes none;
    if (party) {
        none.targeting = party->targeting.bits;
        none.mpCost = party->mpCost;
    }
    if (seated) {
        none.mpCost = 0;
    }
    return none;
}

std::uint8_t esperNumber(EsperId esper) {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(esper) - kFirstEsperAttack);
}
}  // namespace

void DamageOracle::layMenusAsTheCartridgeHoldsIt(const battle::BattleState& state) {
    const PartyList party = partyList(state);
    std::uint8_t espersEquipped = 0;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::BattlerSlot slot = battle::BattlerSlot::character(position);
        const battle::Battler& member = state.at(slot);
        const auto offset = static_cast<std::uint32_t>(position);

        // The command menu. A slot with no party member has none: the fill's.
        const std::uint32_t menu = kCommandMenus + offset * kCommandMenuSize;
        if (member.actor) {
            for (std::size_t row = 0; row < member.commands.size(); ++row) {
                const battle::MenuCommand& command = member.commands[row];
                const std::uint32_t at = menu + static_cast<std::uint32_t>(row) * kCommandRowSize;
                const auto id = static_cast<std::uint8_t>(command.command);
                const std::array<std::uint8_t, kCommandRowSize> bytes{
                    id, static_cast<std::uint8_t>(command.usable ? 0 : kGrayBit), command.targeting.bits};
                layAsTheCartridgeHoldsIt(at, bytes);
            }
            layByteAsTheCartridgeHoldsIt(kGenjiGlove + offset, member.mayHoldTwoWeapons ? kGenjiGloveByte : 0);
        } else {
            leaveOut(menu, kCommandMenuSize);
            leaveOut(kGenjiGlove + offset, 1);
        }

        // The spell list: the esper, then every spell place and every lore.
        const battle::SpellList& list = member.spells;
        const std::uint32_t first = kSpellLists + offset * kSpellListSize;
        std::array<SpellBytes, kSpellEntries> entries{};
        if (list.esper) {
            espersEquipped |= static_cast<std::uint8_t>(1u << position);  // the slot's bit (UpdateEnabledMagic :14380-14384)
            entries[kEsperEntry] = SpellBytes{.spell = esperNumber(*list.esper),
                                              .notCastable = list.summon.castable ? std::uint8_t{0} : kNotCastableBit,
                                              .targeting = list.summon.targeting.bits,
                                              .mpCost = list.summon.mpCost};
        }
        for (std::size_t place = 0; place < list.magic.size(); ++place) {
            const auto& own = list.magic[place];
            entries[kFirstSpellEntry + place] =
                entryBytes(own, party.magic[place], own ? static_cast<std::uint8_t>(own->spell) : kNoSpell,
                           member.actor.has_value());
        }
        for (std::size_t lore = 0; lore < list.lores.size(); ++lore) {
            entries[kFirstLoreEntry + lore] =
                entryBytes(list.lores[lore], party.lores[lore], static_cast<std::uint8_t>(lore), member.actor.has_value());
        }
        for (std::size_t entry = 0; entry < entries.size(); ++entry) {
            const SpellBytes& bytes = entries[entry];
            const std::array<std::uint8_t, kSpellEntrySize> laid{bytes.spell, bytes.notCastable, bytes.targeting, bytes.mpCost};
            layAsTheCartridgeHoldsIt(first + static_cast<std::uint32_t>(entry) * kSpellEntrySize, laid);
        }
        layWordAsTheCartridgeHoldsIt(kSpellListPlaces + offset * 2, static_cast<std::uint16_t>(first & 0xFFFF));

        const std::uint32_t at = slotIndex(slot);
        layByteAsTheCartridgeHoldsIt(kEsperIndex + at, list.esper ? esperNumber(*list.esper) : kNoSpell);
        layByteAsTheCartridgeHoldsIt(kEsperTargeting + at, list.esper ? list.summon.targeting.bits : kNoSpell);
    }
    layByteAsTheCartridgeHoldsIt(kEspersEquipped, espersEquipped);
    for (int position = 0; position < battle::kMonsterSlots; ++position) {
        const std::uint32_t at = slotIndex(battle::BattlerSlot::monster(position));
        layByteAsTheCartridgeHoldsIt(kEsperIndex + at, kNoSpell);
        layByteAsTheCartridgeHoldsIt(kEsperTargeting + at, kNoSpell);
    }
    for (std::size_t i = 0; i < state.battlers.size(); ++i) {
        layByteAsTheCartridgeHoldsIt(kSpellCount + static_cast<std::uint32_t>(i * 2),
                                     static_cast<std::uint8_t>(state.battlers[i].spellCount));
    }

    // The party's own list and every attack's place in it.
    std::array<std::uint8_t, kAttackCount> places{};
    places.fill(kNoSpell);
    for (std::uint32_t esper = 0; esper < kEspers; ++esper) {
        places[kFirstEsperAttack + esper] = 0;
    }
    for (std::size_t place = 0; place < party.magic.size(); ++place) {
        const auto& entry = party.magic[place];
        layByteAsTheCartridgeHoldsIt(kPartySpells + static_cast<std::uint32_t>(place),
                                     entry ? static_cast<std::uint8_t>(entry->spell) : kNoSpell);
        if (entry) {
            places[static_cast<std::uint8_t>(entry->spell)] = static_cast<std::uint8_t>(kFirstSpellEntry + place);
        }
    }
    for (std::size_t lore = 0; lore < party.lores.size(); ++lore) {
        const bool known = party.lores[lore].has_value();
        layByteAsTheCartridgeHoldsIt(kPartyLores + static_cast<std::uint32_t>(lore),
                                     known ? static_cast<std::uint8_t>(kFirstLoreAttack + lore) : kNoSpell);
        if (known) {
            places[kFirstLoreAttack + lore] = static_cast<std::uint8_t>(kFirstLoreEntry + lore);
        }
    }
    layAsTheCartridgeHoldsIt(kSpellPlaces, places);

    // The skills known.
    layByteAsTheCartridgeHoldsIt(kSwdtechsKnownLessOne, static_cast<std::uint8_t>(state.swdtechsKnown - 1));
    layByteAsTheCartridgeHoldsIt(kBlitzCount, static_cast<std::uint8_t>(state.blitzesKnown));
    layByteAsTheCartridgeHoldsIt(kLoreCount, static_cast<std::uint8_t>(state.loresKnown));
    layByteAsTheCartridgeHoldsIt(kRageCount, static_cast<std::uint8_t>(state.ragesKnown.size()));
    std::array<std::uint8_t, kAttackCount> rages{};
    rages.fill(kNotKnown);
    for (std::size_t i = 0; i < state.ragesKnown.size() && i < rages.size(); ++i) {
        rages[i] = static_cast<std::uint8_t>(state.ragesKnown[i]);
    }
    layAsTheCartridgeHoldsIt(kRageList, rages);
    std::array<std::uint8_t, 8> dances{};
    for (std::size_t dance = 0; dance < dances.size(); ++dance) {
        dances[dance] = state.dancesKnown[dance] ? static_cast<std::uint8_t>(*state.dancesKnown[dance]) : kNotKnown;
    }
    layAsTheCartridgeHoldsIt(kDanceList, dances);
}

}  // namespace ostinato::trace
