// The port's level-up and spells learned, their differences, and the record sweeps' draws.

#include "scenarios/ending/records.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

#include "battle/battle_state.h"
#include "battle/leveling.h"
#include "battle/spell_learning.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/game_version.h"
#include "ostinato/item_id.h"

#include "cells/ending/records.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/ending/records_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace detail {

std::uint32_t recordAt(std::size_t index) {
    return kCharacterRecords + kRecordSize * static_cast<std::uint32_t>(index);
}

std::uint32_t spellListAt(CharacterPropId actor) {
    return kSpellsKnown + static_cast<std::uint32_t>(game::kLearnableSpells) * static_cast<std::uint8_t>(actor);
}

std::uint8_t esperByte(const std::optional<EsperId>& esper) {
    if (!esper) {
        return kNoEsper;
    }
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(*esper) - static_cast<std::uint8_t>(EsperId::RAMUH));
}
}  // namespace detail

namespace {

std::uint16_t boostedWord(int maximum, std::uint8_t boostBits) {
    return static_cast<std::uint16_t>((maximum & kMaximumBits) | boostBits << kBoostShift);
}
}  // namespace

namespace detail {

std::array<std::uint8_t, kRecordSize> recordBytes(const game::CharacterRecord& record, std::uint8_t hpBoostBits,
                                                  std::uint8_t mpBoostBits) {
    std::array<std::uint8_t, kRecordSize> bytes{};
    bytes[0] = static_cast<std::uint8_t>(record.actor);
    bytes[kRecordGraphics] = static_cast<std::uint8_t>(record.graphics);
    std::copy(record.name.begin(), record.name.end(), bytes.begin() + kRecordName);
    bytes[kRecordLevel] = record.level;
    const auto putWord = [&](std::size_t at, std::uint16_t word) {
        bytes[at] = static_cast<std::uint8_t>(word & 0xFF);
        bytes[at + 1] = static_cast<std::uint8_t>(word >> 8);
    };
    putWord(kRecordHp, record.hp);
    putWord(kRecordMaxHp, boostedWord(record.maxHp, hpBoostBits));
    putWord(kRecordMp, record.mp);
    putWord(kRecordMaxMp, boostedWord(record.maxMp, mpBoostBits));
    bytes[kRecordExperience] = static_cast<std::uint8_t>(record.experience & 0xFF);
    bytes[kRecordExperience + 1] = static_cast<std::uint8_t>(record.experience >> 8 & 0xFF);
    bytes[kRecordExperience + 2] = static_cast<std::uint8_t>(record.experience >> 16 & 0xFF);
    bytes[kRecordFirstStatus] = record.status.bytes[0];
    bytes[kRecordFourthStatus] = record.status.bytes[3];
    for (std::size_t i = 0; i < record.commands.size(); ++i) {
        bytes[kRecordCommands + i] = static_cast<std::uint8_t>(record.commands[i]);
    }
    bytes[kRecordVigor] = record.vigor;
    bytes[kRecordSpeed] = record.speed;
    bytes[kRecordStamina] = record.stamina;
    bytes[kRecordMagicPower] = record.magicPower;
    bytes[kRecordEsper] = esperByte(record.esper);
    const std::array<ItemId, 6> equipment = record.equipment();
    for (std::size_t i = 0; i < equipment.size(); ++i) {
        bytes[kRecordEquipment + i] = static_cast<std::uint8_t>(equipment[i]);
    }
    return bytes;
}
}  // namespace detail

LevelUpResult portLevelUp(const RecordScenario& scenario) {
    game::CharacterRecord record = scenario.record;
    battle::levelUp(record, GameVersion::US_1_1);
    return LevelUpResult{.level = record.level,
                         .maxHp = record.maxHp,
                         .maxMp = record.maxMp,
                         .hpBoostBits = scenario.hpBoostBits,
                         .mpBoostBits = scenario.mpBoostBits,
                         .vigor = record.vigor,
                         .speed = record.speed,
                         .stamina = record.stamina,
                         .magicPower = record.magicPower};
}

std::string differences(const LevelUpResult& port, const LevelUpResult& guest) {
    std::string out;
    addLine(out, "level", port.level, guest.level);
    addLine(out, "maximum HP", port.maxHp, guest.maxHp);
    addLine(out, "maximum MP", port.maxMp, guest.maxMp);
    addLine(out, "HP boost", port.hpBoostBits, guest.hpBoostBits);
    addLine(out, "MP boost", port.mpBoostBits, guest.mpBoostBits);
    addLine(out, "vigor", port.vigor, guest.vigor);
    addLine(out, "speed", port.speed, guest.speed);
    addLine(out, "stamina", port.stamina, guest.stamina);
    addLine(out, "magic power", port.magicPower, guest.magicPower);
    return out;
}

SpellLearningResult portLearnFromEsper(const RecordScenario& scenario) {
    SpellLearningResult result;
    game::SpellProgress spells = scenario.spells;
    result.progressed = battle::learnFromEsper(spells, *scenario.record.esper, scenario.magicPoints);
    result.progress = spells.progress;
    return result;
}

std::string differences(const SpellLearningResult& port, const SpellLearningResult& guest) {
    std::string out;
    for (std::size_t spell = 0; spell < port.progress.size(); ++spell) {
        const std::string field = "spell " + std::to_string(spell);
        addLine(out, field.c_str(), port.progress[spell], guest.progress[spell]);
    }
    addLine(out, "progressed", port.progressed, guest.progressed);
    return out;
}

RecordScenario drawLevelUpScenario(std::mt19937& rng) {
    RecordScenario s;
    s.recordIndex = rng() % game::kCharacterRecords;
    game::CharacterRecord& record = s.record;
    record.actor = static_cast<CharacterPropId>(s.recordIndex);
    record.level = static_cast<std::uint8_t>(1 + upTo(rng, kPartyMaxLevel - 2));
    // Often within a level's largest gain of the cap, doubled by the largest bonus.
    record.maxHp = static_cast<std::uint16_t>(oneIn(rng, 3) ? kPartyMaxHp - upTo(rng, 400) : upTo(rng, kPartyMaxHp));
    record.maxMp = static_cast<std::uint16_t>(oneIn(rng, 3) ? kPartyMaxMp - upTo(rng, 30) : upTo(rng, kPartyMaxMp));
    record.hp = static_cast<std::uint16_t>(upTo(rng, record.maxHp));
    record.mp = static_cast<std::uint16_t>(upTo(rng, record.maxMp));
    record.experience = static_cast<std::uint32_t>(rng() & 0xFFFFFF);
    // Often near the limit, and now and then at the top of the byte, where the bonus comes round.
    for (std::uint8_t* stat : {&record.vigor, &record.speed, &record.stamina, &record.magicPower}) {
        *stat = static_cast<std::uint8_t>(oneIn(rng, 8)   ? 252 + upTo(rng, 3)
                                          : oneIn(rng, 3) ? 126 + upTo(rng, 2)
                                                          : upTo(rng, 128));
    }
    s.hpBoostBits = static_cast<std::uint8_t>(upTo(rng, 3));
    s.mpBoostBits = static_cast<std::uint8_t>(upTo(rng, 3));
    if (!oneIn(rng, 5)) {
        record.esper = static_cast<EsperId>(static_cast<std::uint8_t>(EsperId::RAMUH) + rng() % kEspers);
    }
    return s;
}

RecordScenario drawLearningScenario(std::mt19937& rng) {
    RecordScenario s;
    s.recordIndex = rng() % game::kSpellLearners;
    s.actor = static_cast<CharacterPropId>(s.recordIndex);
    s.record.actor = s.actor;
    s.record.esper = static_cast<EsperId>(static_cast<std::uint8_t>(EsperId::RAMUH) + rng() % kEspers);
    // A battle's 0 to 10, and one time in four any byte, so the product passes a byte.
    s.magicPoints = static_cast<std::uint8_t>(oneIn(rng, 4) ? byte(rng) : upTo(rng, 10));
    for (std::uint8_t& learned : s.spells.progress) {
        switch (rng() % 8) {
            case 0:
                learned = game::SpellProgress::kSpellJustLearned;
                break;
            case 1:
                learned = game::SpellProgress::kSpellLearned;
                break;
            case 2:
                learned = static_cast<std::uint8_t>(90 + upTo(rng, 9));
                break;
            default:
                learned = static_cast<std::uint8_t>(upTo(rng, 99));
                break;
        }
    }
    return s;
}

std::string describe(const RecordScenario& s) {
    const game::CharacterRecord& r = s.record;
    return "record " + std::to_string(s.recordIndex) + " actor " + std::to_string(static_cast<int>(s.actor)) +
           " level " + std::to_string(r.level) + " max hp " + std::to_string(r.maxHp) + " max mp " +
           std::to_string(r.maxMp) + " boosts " + std::to_string(s.hpBoostBits) + "/" + std::to_string(s.mpBoostBits) +
           " stats " + std::to_string(r.vigor) + "/" + std::to_string(r.speed) + "/" + std::to_string(r.stamina) + "/" +
           std::to_string(r.magicPower) + " esper " + std::to_string(esperByte(r.esper)) + " magic points " +
           std::to_string(s.magicPoints);
}

}  // namespace ostinato::trace
