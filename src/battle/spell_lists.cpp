#include "battle/spell_lists.h"

#include <algorithm>
#include <cassert>

#include "battle/battle_state.h"
#include "data/attack_properties.h"
#include "data/battle_tables.h"

namespace ostinato::battle {

namespace {

// The spell list is laid out in rows of three: a row with none of its spells known anywhere in
// the party is left out.
constexpr std::size_t kSpellsPerRow = 3;

// Imp is the one spell an Imp can cast.
constexpr AttackId kImpSpell = AttackId::IMP;

// The three bands of spells, by attack id, each placed by its own spell-order offset.
constexpr AttackId kFirstEffectSpell = AttackId::SCAN;
constexpr AttackId kFirstWhiteSpell = AttackId::CURE;

constexpr int kStepMineHalfHoursPerHour = 2;
constexpr std::uint8_t kHalfHourMinutes = 30;

int magicOrderOffset(AttackId spell, std::uint8_t setting) {
    const auto& band = spell < kFirstEffectSpell  ? kBlackMagicOrder
                       : spell < kFirstWhiteSpell ? kEffectMagicOrder
                                                  : kWhiteMagicOrder;
    return band[setting].offset;
}

SpellListEntry entryFor(AttackId spell) {
    const AttackProperties& properties = getAttackProperties(spell);
    return SpellListEntry{.spell = spell, .targeting = properties.targeting, .mpCost = properties.mpCost};
}

// What a spell costs a party member: a Gold Hairpin halves it, rounding up; an Economizer makes it 1.
std::uint8_t mpCostWith(std::uint8_t cost, RelicEffect3Set relics) {
    if (relics.has(RelicEffect3::MP_COST_50_PERCENT)) {
        cost = static_cast<std::uint8_t>((cost + 1) / 2);
    }
    if (relics.has(RelicEffect3::MP_COST_1)) {
        cost = 1;
    }
    return cost;
}

enum class SpellsKept : std::uint8_t {
    All,        // Gogo
    OwnSpells,  // the first twelve characters
    None,       // everyone else
};

}  // namespace

const SpellListEntry* SpellList::find(AttackId spell) const {
    for (const auto& entry : magic) {
        if (entry && entry->spell == spell) return &*entry;
    }
    for (const auto& entry : lores) {
        if (entry && entry->spell == spell) return &*entry;
    }
    return nullptr;
}

void buildSpellLists(BattleState& state, game::PlayTime savedPlayTime) {
    // Every spell some party member has learned.
    std::array<bool, game::kLearnableSpells> known{};
    for (int position = 0; position < kPartySlots; ++position) {
        const auto& actor = state.at(BattlerSlot::character(position)).actor;
        if (!actor || static_cast<std::size_t>(*actor) >= game::kSpellLearners) {
            continue;
        }
        const game::SpellProgress& learned = state.party.spells[static_cast<std::size_t>(*actor)];
        for (std::size_t spell = 0; spell < known.size(); ++spell) {
            known[spell] = known[spell] || learned.knows(static_cast<AttackId>(spell));
        }
    }

    // Laid out by the spell-order setting, then closed up a row at a time.
    std::array<std::optional<AttackId>, game::kLearnableSpells> ordered{};
    for (std::size_t index = 0; index < known.size(); ++index) {
        if (!known[index]) {
            continue;
        }
        const auto spell = static_cast<AttackId>(index);
        const int place = static_cast<int>(index) + magicOrderOffset(spell, state.config.magicOrder);
        assert(place >= 0 && place < static_cast<int>(ordered.size()));
        ordered[static_cast<std::size_t>(place)] = spell;
    }
    std::array<std::optional<AttackId>, game::kLearnableSpells> magic{};
    std::size_t next = 0;
    for (std::size_t row = 0; row < ordered.size(); row += kSpellsPerRow) {
        const auto first = ordered.begin() + static_cast<std::ptrdiff_t>(row);
        const auto last = first + kSpellsPerRow;
        if (std::any_of(first, last, [](const auto& spell) { return spell.has_value(); })) {
            std::copy(first, last, magic.begin() + static_cast<std::ptrdiff_t>(next));
            next += kSpellsPerRow;
        }
    }

    // Lores have fixed places after the spells.
    std::array<std::optional<SpellListEntry>, game::kLores> lores{};
    state.loresKnown = 0;
    for (std::size_t lore = 0; lore < lores.size(); ++lore) {
        if (!state.party.loresKnown.test(lore)) {
            continue;
        }
        ++state.loresKnown;
        const auto spell = static_cast<AttackId>(static_cast<std::size_t>(AttackId::CONDEMNED) + lore);
        lores[lore] = entryFor(spell);
        if (spell == AttackId::STEP_MINE) {
            const bool pastTheHalfHour = savedPlayTime.minutes >= kHalfHourMinutes;
            lores[lore]->mpCost =
                static_cast<std::uint8_t>(savedPlayTime.hours * kStepMineHalfHoursPerHour + (pastTheHalfHour ? 1 : 0));
        }
    }

    for (int position = 0; position < kPartySlots; ++position) {
        SpellList& list = state.at(BattlerSlot::character(position)).spells;
        list = SpellList{};
        for (std::size_t place = 0; place < magic.size(); ++place) {
            if (magic[place]) {
                list.magic[place] = entryFor(*magic[place]);
            }
        }
        list.lores = lores;
    }
}

void loadSpellList(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    SpellList& list = battler.spells;
    const game::CharacterRecord& record = state.party.characters[*battler.record];
    const RelicEffect3Set relics = battler.relicEffects3;

    list.esper = record.esper;
    if (record.esper) {
        const SpellListEntry summon = entryFor(static_cast<AttackId>(*record.esper));
        list.summon = SpellListEntry{.spell = summon.spell,
                                     .targeting = summon.targeting,
                                     .mpCost = mpCostWith(summon.mpCost, relics)};
    }

    const CharacterPropId actor = *battler.actor;
    const SpellsKept kept = actor == CharacterPropId::GOGO ? SpellsKept::All
                            : static_cast<std::size_t>(actor) < game::kSpellLearners ? SpellsKept::OwnSpells
                                                                                       : SpellsKept::None;

    battler.spellCount = 0;
    for (auto& entry : list.magic) {
        if (!entry) {
            continue;
        }
        const bool keep = kept == SpellsKept::All ||
                          (kept == SpellsKept::OwnSpells &&
                           state.party.spells[static_cast<std::size_t>(actor)].knows(entry->spell));
        if (!keep) {
            entry.reset();
            continue;
        }
        ++battler.spellCount;
        entry->mpCost = mpCostWith(entry->mpCost, relics);
    }
    for (auto& entry : list.lores) {
        if (!entry) {
            continue;
        }
        if (kept == SpellsKept::None) {
            entry.reset();
            continue;
        }
        entry->mpCost = mpCostWith(entry->mpCost, relics);
    }
}

void refreshCastableSpells(BattleState& state, BattlerSlot slot) {
    if (slot.isMonster()) {
        return;
    }
    Battler& battler = state.at(slot);
    // A spell is castable when it costs less than this: one more than the battler's MP, at most 255.
    const int limit = std::min<int>(battler.mp + 1, 255);
    const bool imp = battler.status.has(StatusId::IMP);
    const auto castable = [&](const SpellListEntry& entry, bool isSpell) {
        if (imp && !(isSpell && entry.spell == kImpSpell)) {
            return false;
        }
        return entry.mpCost < limit;
    };

    SpellList& list = battler.spells;
    list.summon.castable = list.esper && !state.espersSummoned.has(slot) && castable(list.summon, false);
    for (auto& entry : list.magic) {
        if (entry) entry->castable = castable(*entry, true);
    }
    for (auto& entry : list.lores) {
        if (entry) entry->castable = castable(*entry, false);
    }
}

}  // namespace ostinato::battle
