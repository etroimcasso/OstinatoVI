// Shared helpers for the party-setup tests: character records with a character's starting stats,
// and a battle whose party is a given line-up.
#pragma once

#include <cstddef>
#include <initializer_list>

#include "battle/battle_state.h"
#include "data/character.h"
#include "game/character_record.h"

namespace ostinato::test {

// A level-1 record for `actor` with its starting HP, MP, commands, stats and equipment.
inline game::CharacterRecord startingRecord(CharacterPropId actor) {
    const CharacterBaseStats& base = getCharacterBaseStats(actor);
    game::CharacterRecord record;
    record.actor = actor;
    record.level = 1;
    record.hp = base.hp;
    record.maxHp = base.hp;
    record.mp = base.mp;
    record.maxMp = base.mp;
    record.commands = base.commands;
    record.vigor = base.strength;
    record.speed = base.agility;
    record.stamina = base.stamina;
    record.magicPower = base.magicPower;
    record.weapon = base.weapon;
    record.shield = base.shield;
    record.helmet = base.helmet;
    record.armor = base.armor;
    record.relic1 = base.relic1;
    record.relic2 = base.relic2;
    return record;
}

// A record with nothing equipped.
inline game::CharacterRecord unequippedRecord(CharacterPropId actor) {
    game::CharacterRecord record = startingRecord(actor);
    record.weapon = record.shield = record.helmet = record.armor = record.relic1 = record.relic2 = ItemId::EMPTY;
    return record;
}

// A battle whose current party (party 1) is `actors`, record n standing in slot n.
inline battle::BattleState battleWithParty(std::initializer_list<CharacterPropId> actors) {
    battle::BattleState state;
    std::size_t record = 0;
    for (CharacterPropId actor : actors) {
        state.party.characters[record] = startingRecord(actor);
        state.party.membership[record] = {.party = 1, .slot = static_cast<std::uint8_t>(record)};
        ++record;
    }
    state.party.currentParty = 1;
    return state;
}

}  // namespace ostinato::test
