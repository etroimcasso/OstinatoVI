// What a won battle gives, worked out by hand (docs/contracts/battle_rewards.md): experience added up
// to its cap, the Cursed Shield's dispelling and what equipment teaches, levels and techniques told of
// once, a level-up that teaches nothing, level 99, Celes's Muddle and the rages, each run by the port
// and, where the run asks it (tests/with_cartridge.h), on the cartridge's own routine with every cell
// the same; and a machine without a cartridge failing, naming what it lacks. The routines on the
// oracle, the graphics program's commands and the drawn scenarios are in
// tests/oracle/test_rewards_oracle.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "battle/battle_script.h"
#include "data/item_properties.h"
#include "data/level_up.h"
#include "game/character_record.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "rewards_oracle_cases.h"
#include "vanilla_rom.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using game::SpellProgress;
using trace::DamageOracle;
using trace::MessageShown;
using trace::RewardResult;
using trace::RewardScenario;

// A scenario with an empty party but for `record`, all six monster slots empty.
RewardScenario scenarioFor(std::size_t recordIndex, const game::CharacterRecord& record) {
    RewardScenario s;
    s.recordIndex = recordIndex;
    s.cells.party.characters[recordIndex] = record;
    return s;
}

game::CharacterRecord recordOf(CharacterPropId actor, std::uint8_t level, std::uint32_t experience) {
    game::CharacterRecord record;
    record.actor = actor;
    record.level = level;
    record.experience = experience;
    record.maxHp = 500;
    record.maxMp = 50;
    return record;
}

std::uint8_t& percent(SpellProgress& spells, AttackId spell) {
    return spells.progress[static_cast<std::size_t>(spell)];
}

TEST(RewardsCartridge, AMissingCartridgeFailsNamingTheVariable) {
    constexpr const char* kVariable = "FF6_VANILLA_ROM";
    const char* was = std::getenv(kVariable);
    const std::optional<std::string> saved = was != nullptr ? std::optional<std::string>{was} : std::nullopt;
#ifdef _WIN32
    _putenv_s(kVariable, "");
#else
    unsetenv(kVariable);
#endif
    std::string message;
    try {
        (void)vanillaRom();
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
#ifdef _WIN32
    _putenv_s(kVariable, saved ? saved->c_str() : "");
#else
    if (saved) {
        setenv(kVariable, saved->c_str(), 1);
    }
#endif
    EXPECT_NE(message.find(kVariable), std::string::npos) << message;
}

// --- each rule by hand, on both sides -------------------------------------------------------------

// Whether the cartridge's answer by `ask` is the port's: asked where the run asks it, and agreeing
// where it is not asked.
template <typename Ask>
::testing::AssertionResult theCartridgeAgrees(const RewardResult& port, Ask ask) {
    ::testing::AssertionResult agreed = ::testing::AssertionSuccess();
    withCartridge(RewardsOracleTest::image(), [&](const Cartridge& cartridge) {
        if (!cartridge) {
            return;
        }
        const std::string apart = trace::differences(port, ask(cartridge.oracle()));
        if (!apart.empty()) {
            agreed = ::testing::AssertionFailure() << apart;
        }
    });
    return agreed;
}

TEST_F(RewardsOracleTest, ExperienceAddedByHand) {
    struct Case {
        std::uint32_t experience;
        std::uint32_t share;
        std::uint32_t after;
    };
    for (const Case c : {Case{1000, 234, 1234}, Case{kMaxExperience - 1000, 1000, kMaxExperience},
                         Case{kMaxExperience - 5, 10'000, kMaxExperience}, Case{kMaxExperience, 0x300000, 1'368'512},
                         Case{0xFFFFFF, 1, 0}}) {
        const RewardScenario s = scenarioFor(4, recordOf(CharacterPropId::EDGAR, 99, c.experience));
        RewardScenario shared = s;
        shared.share = c.share;
        const RewardResult port = trace::portAddExperience(shared);
        EXPECT_EQ(port.cells.party.characters[4].experience, c.after) << c.experience << " + " << c.share;
        EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.addExperience(shared); }))
            << c.experience << " + " << c.share;
    }
}

TEST_F(RewardsOracleTest, TheCursedShieldByHand) {
    game::CharacterRecord record = recordOf(CharacterPropId::CELES, 40, 0);
    record.shield = ItemId::CURSED_SHLD;
    record.helmet = ItemId::CURSED_SHLD;
    RewardScenario s = scenarioFor(6, record);
    s.magicPoints = 3;
    s.flags.partyHasMagicite = true;
    s.switches.setCursedShieldBattles(254);
    const RewardResult port = trace::portLearnFromEquipment(s);
    EXPECT_EQ(port.switches.cursedShieldBattles(), 0);
    EXPECT_EQ(port.cells.party.characters[6].shield, ItemId::CURSED_SHLD);
    EXPECT_EQ(port.cells.party.characters[6].helmet, ItemId::PALADIN_SHLD);
    EXPECT_TRUE(port.flags.curseDispelled);
    EXPECT_TRUE(port.flags.magicPointsEarned);  // the Paladin Shield's spell was stored
    const ItemProperties& paladin = getItemProperties(ItemId::PALADIN_SHLD);
    EXPECT_EQ(port.cells.party.spells[6].progress[static_cast<std::size_t>(paladin.spellLearned)],
              static_cast<std::uint8_t>(paladin.spellLearnRate * 3));
    EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.learnFromEquipment(s); }));
}

TEST_F(RewardsOracleTest, WithoutMagiciteASpellStoredEarnsNoMagicPoints) {
    game::CharacterRecord record = recordOf(CharacterPropId::CELES, 40, 0);
    record.shield = ItemId::PALADIN_SHLD;
    RewardScenario s = scenarioFor(6, record);
    s.magicPoints = 3;
    const RewardResult port = trace::portLearnFromEquipment(s);
    EXPECT_FALSE(port.flags.magicPointsEarned);
    EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.learnFromEquipment(s); }));
}

TEST_F(RewardsOracleTest, TwoLevelsAreToldOfOnceAndTeachTheirSpells) {
    // Terra at 21 with the experience for 23: Fire 2 is learned at 22, nothing at 23.
    RewardScenario s = scenarioFor(0, recordOf(CharacterPropId::TERRA, 21, 0));
    std::uint32_t steps = 0;
    for (int at = 2; at <= 23; ++at) {
        steps += levelUpExp(static_cast<std::uint8_t>(at));
    }
    s.cells.party.characters[0].experience = steps * 8;
    s.partySlot = 2;
    const RewardResult port = trace::portLevelUps(s);
    EXPECT_EQ(port.cells.party.characters[0].level, 23);
    EXPECT_EQ(port.messages, (std::vector<MessageShown>{{.message = battle::kMessageGainedALevel, .second = 2}}));
    EXPECT_FALSE(port.levelMessageToTell);
    SpellProgress spells = port.cells.party.spells[0];
    EXPECT_EQ(percent(spells, AttackId::FIRE_2), SpellProgress::kSpellJustLearned);
    EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.levelUps(s); }));
}

TEST_F(RewardsOracleTest, TwoTechniquesInOneWinAreToldOfOnce) {
    // Cyan from 11 to 15: Slash at 12 and Quadra Slam at 15, one "mastered a new technique".
    RewardScenario s = scenarioFor(2, recordOf(CharacterPropId::CYAN, 11, 0));
    std::uint32_t steps = 0;
    for (int at = 2; at <= 15; ++at) {
        steps += levelUpExp(static_cast<std::uint8_t>(at));
    }
    s.cells.party.characters[2].experience = steps * 8;
    s.levelMessageToTell = false;  // told already this win
    const RewardResult port = trace::portLevelUps(s);
    EXPECT_EQ(port.cells.party.characters[2].level, 15);
    EXPECT_EQ(port.cells.party.swdtechsKnown.bits, 0x0C);
    EXPECT_EQ(port.messages, (std::vector<MessageShown>{{.message = battle::kMessageMasteredATechnique}}));
    EXPECT_TRUE(port.flags.techniqueMessageShown);
    EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.levelUps(s); }));
}

TEST_F(RewardsOracleTest, GogoGainsALevelAndLearnsNothing) {
    RewardScenario s = scenarioFor(12, recordOf(CharacterPropId::GOGO, 5, 0));
    std::uint32_t steps = 0;
    for (int at = 2; at <= 6; ++at) {
        steps += levelUpExp(static_cast<std::uint8_t>(at));
    }
    s.cells.party.characters[12].experience = steps * 8;
    const RewardResult port = trace::portLevelUps(s);
    EXPECT_EQ(port.cells.party.characters[12].level, 6);
    EXPECT_EQ(port.messages.size(), 1u);
    EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.levelUps(s); }));
}

TEST_F(RewardsOracleTest, Level99IsNotLookedPast) {
    RewardScenario s = scenarioFor(1, recordOf(CharacterPropId::LOCKE, 99, kMaxExperience));
    const RewardResult port = trace::portLevelUps(s);
    EXPECT_EQ(port.cells.party.characters[1].level, 99);
    EXPECT_TRUE(port.messages.empty());
    EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.levelUps(s); }));
}

TEST_F(RewardsOracleTest, CelesLearnsMuddleAt32) {
    RewardScenario s;
    s.actor = CharacterPropId::CELES;
    s.level = 32;
    const RewardResult port = trace::portLearnAbilities(s);
    EXPECT_EQ(port.cells.party.spells[6].progress[static_cast<std::size_t>(AttackId::MUDDLE)],
              SpellProgress::kSpellJustLearned);
    EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.learnAbilities(s); }));
}

TEST_F(RewardsOracleTest, RagesByHand) {
    RewardScenario s;
    s.monsters = {static_cast<MonsterId>(12), std::nullopt, static_cast<MonsterId>(255),
                  static_cast<MonsterId>(256), static_cast<MonsterId>(300), static_cast<MonsterId>(0)};
    const RewardResult port = trace::portLearnRages(s);
    EXPECT_TRUE(port.cells.party.ragesKnown.test(0));
    EXPECT_TRUE(port.cells.party.ragesKnown.test(12));
    EXPECT_TRUE(port.cells.party.ragesKnown.test(255));
    EXPECT_EQ(port.cells.party.ragesKnown.count(), 3u);
    EXPECT_TRUE(theCartridgeAgrees(port, [&](DamageOracle& oracle) { return oracle.learnRages(s); }));
}

}  // namespace
}  // namespace ostinato::test
