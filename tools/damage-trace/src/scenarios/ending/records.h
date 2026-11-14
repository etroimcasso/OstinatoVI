// One character's record and spells as a won battle's end reads them, a level-up and an esper's
// spells learned: what each leaves, the port's answer, the comparison and the sweeps' draws.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

#include "battle/battle_state.h"
#include "game/character_record.h"
#include "ostinato/character_prop_id.h"

namespace ostinato::trace {

// One character's record and spells as a won battle's end reads them: the record and which of the
// sixteen it is, whose spells are being learned (one of the first twelve characters), the battle's
// magic points, and the two-bit HP and MP boosts the cartridge keeps in the top of the record's
// maximum HP and MP words (0-3 each), which the port keeps no field for.
struct RecordScenario {
    game::CharacterRecord record;
    std::size_t recordIndex = 0;
    CharacterPropId actor = CharacterPropId::TERRA;
    game::SpellProgress spells;
    std::uint8_t magicPoints = 0;
    std::uint8_t hpBoostBits = 0;
    std::uint8_t mpBoostBits = 0;
};

// What a level-up leaves of the record, as the same fields on both sides.
struct LevelUpResult {
    std::uint8_t level = 0;
    std::uint16_t maxHp = 0;
    std::uint16_t maxMp = 0;
    std::uint8_t hpBoostBits = 0;
    std::uint8_t mpBoostBits = 0;
    std::uint8_t vigor = 0;
    std::uint8_t speed = 0;
    std::uint8_t stamina = 0;
    std::uint8_t magicPower = 0;
};

// The port's level-up of the scenario's record (battle::levelUp), on the US cartridges' MP curve,
// with the boosts carried through as the scenario has them.
[[nodiscard]] LevelUpResult portLevelUp(const RecordScenario& scenario);

[[nodiscard]] std::string differences(const LevelUpResult& port, const LevelUpResult& guest);

// What the esper's spells learned leave, as the same fields on both sides: the learned percentage of
// each of the 54 spells, and whether any of them was stored.
struct SpellLearningResult {
    std::array<std::uint8_t, game::kLearnableSpells> progress{};
    bool progressed = false;
};

// The port's spells learned from the scenario record's esper (battle::learnFromEsper).
[[nodiscard]] SpellLearningResult portLearnFromEsper(const RecordScenario& scenario);

[[nodiscard]] std::string differences(const SpellLearningResult& port, const SpellLearningResult& guest);

// The scenarios the level-up sweep draws, from the same seed: one of the sixteen records at a level
// from 1 to 98, its maximum HP up to 9,999 and maximum MP up to 999 (each often within a level's gain
// of its cap), its vigor, speed, stamina and magic power up to 128 (often 126 to 128, and now and then
// 252 to 255, where the bonus comes round in the stat's byte), the two boosts 0-3 each, and any of the
// 27 espers or none.
[[nodiscard]] RecordScenario drawLevelUpScenario(std::mt19937& rng);

// The scenarios the esper-learning sweep draws, from the same seed: one of the first twelve
// characters with any of the 27 espers, 0 to 10 magic points or one time in four any byte (so the
// rate times the magic points passes a byte), and each of the 54 spells at a
// percentage below 100, just learned or learned.
[[nodiscard]] RecordScenario drawLearningScenario(std::mt19937& rng);

// One line naming a drawn record scenario's inputs, for a report.
[[nodiscard]] std::string describe(const RecordScenario& scenario);

}  // namespace ostinato::trace
