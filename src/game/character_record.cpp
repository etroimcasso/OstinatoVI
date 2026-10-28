#include "game/character_record.h"

#include <cstdint>

#include "data/character.h"

namespace ostinato::game {

namespace {

// The sprite a new character is drawn with: its own. The fourteen playable characters' graphics
// slots are numbered as the characters are; Banon's and Leo's are named after them.
std::optional<CharacterGfxId> defaultGraphics(CharacterPropId actor) {
    static_assert(static_cast<std::uint8_t>(CharacterGfxId::TERRA) == static_cast<std::uint8_t>(CharacterPropId::TERRA));
    static_assert(static_cast<std::uint8_t>(CharacterGfxId::UMARO) == static_cast<std::uint8_t>(CharacterPropId::UMARO));
    if (actor <= CharacterPropId::UMARO) {
        return static_cast<CharacterGfxId>(actor);
    }
    if (actor == CharacterPropId::BANON) {
        return CharacterGfxId::BANON;
    }
    if (actor == CharacterPropId::LEO) {
        return CharacterGfxId::LEO;
    }
    return std::nullopt;
}

}  // namespace

CharacterRecord startingRecord(CharacterPropId actor) {
    const CharacterBaseStats& base = getCharacterBaseStats(actor);
    CharacterRecord record;
    record.actor = actor;
    if (const std::optional<CharacterGfxId> graphics = defaultGraphics(actor)) {
        record.graphics = *graphics;
    }
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

}  // namespace ostinato::game
