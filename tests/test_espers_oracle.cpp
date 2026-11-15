// The esper's spells learned and the level-up worked out by hand (docs/contracts/battle_espers.md):
// every esper's level-up and every esper's spells, the caps, the stat limit and the learning edges,
// each worked out by hand from the original and also run on the cartridge's own routine
// (tools/damage-trace/src/oracle/damage_oracle.h) where the run asks it (tests/with_cartridge.h), the
// port matching both; and no two espers teaching the same spells. Both routines on the oracle, the
// drawn records, the traced calls and the recorded win are in tests/oracle/test_espers_oracle.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>

#include "data/esper_properties.h"
#include "espers_oracle_cases.h"
#include "game/character_record.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using game::SpellProgress;
using trace::DamageOracle;
using trace::LevelUpResult;
using trace::RecordScenario;
using trace::SpellLearningResult;

// --- the level-up --------------------------------------------------------------------------------

// The port's level-up over `scenario`, checked against the cartridge's own answer where the run asks
// it.
LevelUpResult checkedLevelUp(const RecordScenario& scenario) {
    const LevelUpResult port = trace::portLevelUp(scenario);
    withCartridge(EspersOracleTest::image(), [&](const Cartridge& cartridge) {
        if (!cartridge) {
            return;
        }
        EXPECT_EQ(trace::differences(port, cartridge.oracle().levelUp(scenario)), "") << trace::describe(scenario);
    });
    return port;
}

// The port's spells learned over `scenario`, checked against the cartridge's own answer where the run
// asks it.
SpellLearningResult checkedLearning(const RecordScenario& scenario) {
    const SpellLearningResult port = trace::portLearnFromEsper(scenario);
    withCartridge(EspersOracleTest::image(), [&](const Cartridge& cartridge) {
        if (!cartridge) {
            return;
        }
        EXPECT_EQ(trace::differences(port, cartridge.oracle().learnFromEsper(scenario)), "")
            << trace::describe(scenario);
    });
    return port;
}

// Every esper's answer to `scenarioFor(esper)` by `port` is the cartridge's by `ask`, where the run
// asks it.
template <typename ScenarioFor, typename Port, typename Ask>
void expectEveryEsperTheCartridges(const Cartridge& cartridge, ScenarioFor scenarioFor, Port port, Ask ask) {
    if (!cartridge) {
        return;
    }
    for (const EsperPropertiesEntry& entry : kEsperProperties) {
        const RecordScenario s = scenarioFor(entry.id);
        EXPECT_EQ(trace::differences(port(s), ask(cartridge.oracle(), s)), "") << static_cast<int>(entry.id);
    }
}


// Record 3 at level 69 with 1,000 HP and 100 MP, every stat at 50, the HP boost 3 and the MP boost 1.
RecordScenario level69(std::optional<EsperId> esper) {
    RecordScenario s;
    s.recordIndex = 3;
    s.record.actor = CharacterPropId::SHADOW;
    s.record.level = 69;
    s.record.maxHp = 1000;
    s.record.maxMp = 100;
    s.record.vigor = 50;
    s.record.speed = 50;
    s.record.stamina = 50;
    s.record.magicPower = 50;
    s.record.esper = esper;
    s.hpBoostBits = 3;
    s.mpBoostBits = 1;
    return s;
}

TEST_F(EspersOracleTest, ALevelUpWithNoEsperGainsTheCurvesAndKeepsTheBoosts) {
    const LevelUpResult result = checkedLevelUp(level69(std::nullopt));
    EXPECT_EQ(result.level, 70);
    EXPECT_EQ(result.maxHp, 1162);
    EXPECT_EQ(result.maxMp, 108);
    EXPECT_EQ(result.hpBoostBits, 3);
    EXPECT_EQ(result.mpBoostBits, 1);
}

TEST_F(EspersOracleTest, EveryEspersLevelUpIsTheCartridges) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        expectEveryEsperTheCartridges(
            cartridge, level69, [](const RecordScenario& s) { return trace::portLevelUp(s); },
            [](DamageOracle& oracle, const RecordScenario& s) { return oracle.levelUp(s); });
    });
    // Two by hand: Terrato's +30% (162 + 49) and Bismark's +2 vigor.
    EXPECT_EQ(checkedLevelUp(level69(EsperId::TERRATO)).maxHp, 1211);
    EXPECT_EQ(checkedLevelUp(level69(EsperId::BISMARK)).vigor, 52);
}

TEST_F(EspersOracleTest, TheCapsAreTheCartridges) {
    RecordScenario s = level69(EsperId::BAHAMUT);
    s.record.maxHp = 9900;
    s.record.maxMp = 995;
    const LevelUpResult capped = checkedLevelUp(s);
    EXPECT_EQ(capped.maxHp, 9999);
    EXPECT_EQ(capped.maxMp, 999);
    EXPECT_EQ(capped.hpBoostBits, 3);
    s.record.maxHp = 9999;
    s.record.maxMp = 999;
    EXPECT_EQ(checkedLevelUp(s).maxHp, 9999);
}

TEST_F(EspersOracleTest, TheStatLimitIsTheCartridges) {
    RecordScenario s = level69(EsperId::BISMARK);  // +2 vigor
    s.record.vigor = 127;
    EXPECT_EQ(checkedLevelUp(s).vigor, 128);
    s.record.vigor = 128;
    EXPECT_EQ(checkedLevelUp(s).vigor, 128);
    s = level69(EsperId::RAMUH);  // +1 stamina
    s.record.stamina = 127;
    EXPECT_EQ(checkedLevelUp(s).stamina, 128);
    s.record.stamina = 128;
    EXPECT_EQ(checkedLevelUp(s).stamina, 128);
}

// The bonus adds within the stat's byte: 255 + 1 and 254 + 2 come round to 0, 255 + 2 to 1, and
// 254 + 1 is 255, held to 128.
TEST_F(EspersOracleTest, TheStatsByteWrapsAsTheCartridgesDoes) {
    RecordScenario s = level69(EsperId::RAMUH);  // +1 stamina
    s.record.stamina = 255;
    EXPECT_EQ(checkedLevelUp(s).stamina, 0);
    s.record.stamina = 254;
    EXPECT_EQ(checkedLevelUp(s).stamina, 128);
    s = level69(EsperId::BISMARK);  // +2 vigor
    s.record.vigor = 254;
    EXPECT_EQ(checkedLevelUp(s).vigor, 0);
    s.record.vigor = 255;
    EXPECT_EQ(checkedLevelUp(s).vigor, 1);
}

// --- the esper's spells --------------------------------------------------------------------------

// Locke (record 1) with `esper` after a battle of `magicPoints`, every spell unlearned.
RecordScenario lockeWith(EsperId esper, std::uint8_t magicPoints) {
    RecordScenario s;
    s.recordIndex = 1;
    s.actor = CharacterPropId::LOCKE;
    s.record.actor = CharacterPropId::LOCKE;
    s.record.esper = esper;
    s.magicPoints = magicPoints;
    return s;
}

std::uint8_t& percent(SpellProgress& progress, AttackId spell) {
    return progress.progress[static_cast<std::size_t>(spell)];
}

std::uint8_t percent(const SpellLearningResult& result, AttackId spell) {
    return result.progress[static_cast<std::size_t>(spell)];
}

TEST_F(EspersOracleTest, EveryEspersSpellsAreTheCartridges) {
    withCartridge(image(), [&](const Cartridge& cartridge) {
        expectEveryEsperTheCartridges(
            cartridge, [](EsperId esper) { return lockeWith(esper, 10); },
            [](const RecordScenario& s) { return trace::portLearnFromEsper(s); },
            [](DamageOracle& oracle, const RecordScenario& s) { return oracle.learnFromEsper(s); });
    });
    // By hand: Ramuh with 10 magic points, Bolt at 10 × 10 = just learned, Bolt 2 at 20, Poison at 50.
    const SpellLearningResult ramuh = checkedLearning(lockeWith(EsperId::RAMUH, 10));
    EXPECT_EQ(percent(ramuh, AttackId::BOLT), SpellProgress::kSpellJustLearned);
    EXPECT_EQ(percent(ramuh, AttackId::BOLT_2), 20);
    EXPECT_EQ(percent(ramuh, AttackId::POISON), 50);
    EXPECT_TRUE(ramuh.progressed);
}

TEST_F(EspersOracleTest, TheLearningEdgesAreTheCartridges) {
    // Bolt learned, Bolt 2 at 50 (+ 2 × 25 = 100), Poison just learned already, with 25 magic points
    // as no battle gives — the rule takes any byte.
    RecordScenario s = lockeWith(EsperId::RAMUH, 25);
    percent(s.spells, AttackId::BOLT) = SpellProgress::kSpellLearned;
    percent(s.spells, AttackId::BOLT_2) = 50;
    percent(s.spells, AttackId::POISON) = SpellProgress::kSpellJustLearned;
    const SpellLearningResult edges = checkedLearning(s);
    EXPECT_EQ(percent(edges, AttackId::BOLT), SpellProgress::kSpellLearned);
    EXPECT_EQ(percent(edges, AttackId::BOLT_2), SpellProgress::kSpellJustLearned);
    EXPECT_EQ(percent(edges, AttackId::POISON), SpellProgress::kSpellJustLearned);
    EXPECT_TRUE(edges.progressed);

    // Starlet's Cure from 95 by 25 × 10: past a byte, just learned.
    RecordScenario past = lockeWith(EsperId::STARLET, 10);
    percent(past.spells, AttackId::CURE) = 95;
    EXPECT_EQ(percent(checkedLearning(past), AttackId::CURE), SpellProgress::kSpellJustLearned);

    // No magic points: nothing moves, and a spell stored still counts.
    RecordScenario none = lockeWith(EsperId::RAMUH, 0);
    percent(none.spells, AttackId::BOLT) = 40;
    const SpellLearningResult unchanged = checkedLearning(none);
    EXPECT_EQ(percent(unchanged, AttackId::BOLT), 40);
    EXPECT_TRUE(unchanged.progressed);

    // Starlet's Cure from 50 by 25 × 12 = 300, of which the low byte, 44, is added: 94.
    RecordScenario wide = lockeWith(EsperId::STARLET, 12);
    percent(wide.spells, AttackId::CURE) = 50;
    EXPECT_EQ(percent(checkedLearning(wide), AttackId::CURE), 94);

    // Every spell learned: nothing is stored.
    RecordScenario learned = lockeWith(EsperId::RAMUH, 10);
    for (const AttackId spell : {AttackId::BOLT, AttackId::BOLT_2, AttackId::POISON}) {
        percent(learned.spells, spell) = SpellProgress::kSpellLearned;
    }
    EXPECT_FALSE(checkedLearning(learned).progressed);
}

// The replay tells the esper by the spells it reads, so no two espers may teach the same spells.
TEST(EspersReplay, NoTwoEspersTeachTheSameSpells) {
    std::set<std::set<AttackId>> seen;
    for (const EsperPropertiesEntry& entry : kEsperProperties) {
        std::set<AttackId> spells;
        for (const EsperSpell& slot : entry.record.spells) {
            if (slot.spell != AttackId::NONE) {
                spells.insert(slot.spell);
            }
        }
        EXPECT_TRUE(seen.insert(spells).second) << static_cast<int>(entry.id);
    }
}

}  // namespace
}  // namespace ostinato::test
