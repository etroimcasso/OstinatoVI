// The cartridge's level-up and spells learned run over a record, and a traced call on the records
// replayed.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "retropp/memory_region.h"

#include "battle/battle_state.h"
#include "battle/leveling.h"
#include "battle/spell_learning.h"
#include "data/esper_properties.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"

#include "cells/ending/records.h"
#include "oracle/ending/records_shared.h"
#include "oracle/routines.h"
#include "scenarios/common/draws.h"
#include "scenarios/ending/records_shared.h"

namespace ostinato::trace {

using namespace cells;
using namespace routines;
using namespace detail;

namespace {

const TracedRoutine& kLearnGenjuMagicRoutine = kTracedRoutines[7];
const TracedRoutine& kDoLevelUpRoutine = kTracedRoutines[8];
}  // namespace

namespace detail {

std::optional<EsperId> esperOfByte(std::uint8_t value) {
    if (value == kNoEsper) {
        return std::nullopt;
    }
    if (value >= kEspers) {
        throw std::runtime_error("the record's esper byte names no esper: " + std::to_string(value));
    }
    return static_cast<EsperId>(value + static_cast<std::uint8_t>(EsperId::RAMUH));
}
}  // namespace detail

void DamageOracle::writeRecordScenario(const RecordScenario& scenario) {
    if (scenario.recordIndex >= game::kCharacterRecords) {
        throw std::logic_error("a record scenario names one of the sixteen records");
    }
    if (static_cast<std::size_t>(scenario.actor) >= game::kSpellLearners) {
        throw std::logic_error("a record scenario's spells are one of the first twelve characters'");
    }
    const std::array<std::uint8_t, kRecordSize> bytes =
        recordBytes(scenario.record, scenario.hpBoostBits, scenario.mpBoostBits);
    writeBytes(retropp::MemoryRegion{.at = recordAt(scenario.recordIndex), .size = kRecordSize}, bytes);
    writeBytes(retropp::MemoryRegion{.at = spellListAt(scenario.actor),
                                         .size = static_cast<std::uint32_t>(game::kLearnableSpells)},
                   scenario.spells.progress);
}

LevelUpResult DamageOracle::readLevelUpResult(std::size_t recordIndex) {
    const std::uint32_t at = recordAt(recordIndex);
    const std::uint16_t maxHp = readWord(at + kRecordMaxHp);
    const std::uint16_t maxMp = readWord(at + kRecordMaxMp);
    return LevelUpResult{.level = readByte(at + kRecordLevel),
                         .maxHp = static_cast<std::uint16_t>(maxHp & kMaximumBits),
                         .maxMp = static_cast<std::uint16_t>(maxMp & kMaximumBits),
                         .hpBoostBits = static_cast<std::uint8_t>(maxHp >> kBoostShift),
                         .mpBoostBits = static_cast<std::uint8_t>(maxMp >> kBoostShift),
                         .vigor = readByte(at + kRecordVigor),
                         .speed = readByte(at + kRecordSpeed),
                         .stamina = readByte(at + kRecordStamina),
                         .magicPower = readByte(at + kRecordMagicPower)};
}

SpellLearningResult DamageOracle::readSpellLearningResult(CharacterPropId actor) {
    SpellLearningResult result;
    const std::vector<std::uint8_t> bytes = machine_.read(
        retropp::MemoryRegion{.at = spellListAt(actor), .size = static_cast<std::uint32_t>(game::kLearnableSpells)});
    std::copy(bytes.begin(), bytes.end(), result.progress.begin());
    result.progressed = (readByte(kWinFlags) & kMagiciteBit) != 0;
    return result;
}

SpellLearningResult DamageOracle::learnFromEsper(const RecordScenario& scenario) {
    if (!scenario.record.esper) {
        throw std::logic_error("the spells learned from an esper are called for a record with one");
    }
    writeRecordScenario(scenario);
    writeWord(kSpellListPlace, static_cast<std::uint16_t>(spellListAt(scenario.actor) & 0xFFFF));
    writeByte(kMagicPoints, scenario.magicPoints);
    writeByte(kPartyHasMagicite, kMagiciteBit);
    writeByte(kWinFlags, 0);
    call([&] {
        learnGenjuMagic_(esperByte(scenario.record.esper),
                         static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords),
                         kWideIndexStatus, kBattleDataBank, kBattleDirectPage);
    });
    trace_->returnToFrame(kLearnGenjuMagicRoutine);
    return readSpellLearningResult(scenario.actor);
}

LevelUpResult DamageOracle::levelUp(const RecordScenario& scenario) {
    if (scenario.record.level >= kPartyMaxLevel) {
        throw std::logic_error("the game never levels up a character at 99");
    }
    writeRecordScenario(scenario);
    call([&] {
        doLevelUp_(static_cast<std::uint16_t>(recordAt(scenario.recordIndex) - kCharacterRecords), kWideIndexStatus,
                   kBattleDataBank, kBattleDirectPage);
    });
    trace_->returnToFrame(kDoLevelUpRoutine);
    return readLevelUpResult(scenario.recordIndex);
}

std::string DamageOracle::replayRecordCall(const TracedCall& call, const TracedRoutine& routine) {
    const auto input = [&](std::uint32_t cell) -> std::optional<std::uint8_t> {
        const auto found = call.inputs.find(cell);
        if (found == call.inputs.end()) {
            return std::nullopt;
        }
        return found->second;
    };
    const auto overlayWrites = [&] {
        for (const auto& [cell, value] : call.writes) {
            writeByte(cell, value);
        }
    };

    if (routine.entry == kDoLevelUp) {
        // The record is the one the call's record cells fall in.
        std::optional<std::size_t> index;
        for (const auto* cells : {&call.inputs, &call.writes}) {
            for (const auto& [cell, value] : *cells) {
                if (cell < kCharacterRecords || cell >= recordAt(game::kCharacterRecords)) {
                    continue;
                }
                const std::size_t in = (cell - kCharacterRecords) / kRecordSize;
                if (index && *index != in) {
                    throw std::runtime_error("DoLevelUp touched more than one character record");
                }
                index = in;
            }
        }
        if (!index) {
            throw std::runtime_error("DoLevelUp touched no character record");
        }
        const std::uint32_t at = recordAt(*index);
        const auto field = [&](std::size_t offset) { return input(at + static_cast<std::uint32_t>(offset)).value_or(0); };
        RecordScenario s;
        s.recordIndex = *index;
        s.record.level = field(kRecordLevel);
        if (s.record.level >= kPartyMaxLevel) {
            throw std::runtime_error("DoLevelUp was called at level " + std::to_string(s.record.level));
        }
        const auto word = [&](std::size_t offset) {
            return static_cast<std::uint16_t>(field(offset) | field(offset + 1) << 8);
        };
        s.record.maxHp = static_cast<std::uint16_t>(word(kRecordMaxHp) & kMaximumBits);
        s.hpBoostBits = static_cast<std::uint8_t>(word(kRecordMaxHp) >> kBoostShift);
        s.record.maxMp = static_cast<std::uint16_t>(word(kRecordMaxMp) & kMaximumBits);
        s.mpBoostBits = static_cast<std::uint8_t>(word(kRecordMaxMp) >> kBoostShift);
        s.record.vigor = field(kRecordVigor);
        s.record.speed = field(kRecordSpeed);
        s.record.stamina = field(kRecordStamina);
        s.record.magicPower = field(kRecordMagicPower);
        s.record.esper = esperOfByte(input(at + kRecordEsper).value_or(kNoEsper));
        const LevelUpResult port = portLevelUp(s);
        writeRecordScenario(s);
        overlayWrites();
        return differences(port, readLevelUpResult(*index));
    }

    // The spells learned from an esper: the spell cells read name the character and the spells. Each
    // spell the esper teaches is read once, whatever its percentage, so the spells read name the
    // esper — no two espers teach the same spells.
    std::optional<std::size_t> actor;
    std::set<std::size_t> taught;
    for (const auto& [cell, value] : call.inputs) {
        if (cell < kSpellsKnown || cell >= kSpellsKnown + kSpellsKnownSize) {
            continue;
        }
        const std::size_t place = cell - kSpellsKnown;
        const std::size_t of = place / game::kLearnableSpells;
        if (actor && *actor != of) {
            throw std::runtime_error("LearnGenjuMagic read more than one character's spells");
        }
        actor = of;
        taught.insert(place % game::kLearnableSpells);
    }
    if (!actor) {
        throw std::runtime_error("LearnGenjuMagic read no spell");
    }
    std::optional<EsperId> esper;
    for (const EsperPropertiesEntry& entry : kEsperProperties) {
        std::set<std::size_t> spells;
        for (const EsperSpell& slot : entry.record.spells) {
            if (slot.spell != AttackId::NONE) {
                spells.insert(static_cast<std::size_t>(slot.spell));
            }
        }
        if (spells == taught) {
            esper = entry.id;
        }
    }
    if (!esper) {
        throw std::runtime_error("LearnGenjuMagic read spells no esper teaches");
    }
    const std::optional<std::uint8_t> magicPoints = input(kMagicPoints);
    if (!magicPoints) {
        throw std::runtime_error("LearnGenjuMagic did not read the battle's magic points");
    }
    RecordScenario s;
    s.actor = static_cast<CharacterPropId>(*actor);
    s.recordIndex = *actor;
    s.record.actor = s.actor;
    s.record.esper = esper;
    s.magicPoints = *magicPoints;
    for (std::size_t spell = 0; spell < game::kLearnableSpells; ++spell) {
        s.spells.progress[spell] = input(spellListAt(s.actor) + static_cast<std::uint32_t>(spell)).value_or(0);
    }
    const SpellLearningResult port = portLearnFromEsper(s);
    writeRecordScenario(s);
    overlayWrites();
    SpellLearningResult guest = readSpellLearningResult(s.actor);
    // A spell stored ORs the magicite flag into the win's flags, which writes them whether the party
    // has magicite or not.
    guest.progressed = call.writes.contains(kWinFlags);
    return differences(port, guest);
}

}  // namespace ostinato::trace
