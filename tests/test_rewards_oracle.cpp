// What a won battle gives, on the oracle (docs/contracts/battle_rewards.md): the five routines and the
// graphics program's entry are where the US cartridges hold them, their calls return where the win
// makes them, and an image without any of them is refused; the graphics program's calls are recorded
// and answered, its table of commands read from the cartridge, its item-list commands run as the
// cartridge holds them; a party written into its cells reads back unchanged; each rule's hand-worked
// cases run on the cartridge's own routine, the port matching both; every actor at every level through
// the ability routine; then 500 drawn scenarios through each of the other four.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/battle_script.h"
#include "damage_oracle.h"
#include "data/item_properties.h"
#include "data/level_up.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/game_limits.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using game::SpellProgress;
using trace::DamageOracle;
using trace::MessageShown;
using trace::RewardResult;
using trace::RewardScenario;

constexpr std::uint32_t kAddExp = 0xC26235;
constexpr std::uint32_t kLearnItemMagic = 0xC25FEF;
constexpr std::uint32_t kCheckLevelUp = 0xC2606D;
constexpr std::uint32_t kLearnAbilities = 0xC261B6;
constexpr std::uint32_t kLearnRage = 0xC24A07;
constexpr std::uint32_t kExecBtlGfx = 0xC26411;
constexpr std::uint32_t kExecBtlGfxExt = 0xC10000;
constexpr std::uint32_t kGraphicsCommandTable = 0xC10008;

constexpr int kDraws = 500;

// A HiROM image holds bank $Cn from offset $n0000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

class RewardsOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    template <typename Body>
    static void withOracle(Body body) {
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            body(oracle);
        });
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* RewardsOracleTest::image_ = nullptr;

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

// --- where the routines are ----------------------------------------------------------------------

TEST_F(RewardsOracleTest, TheRoutinesAreWhereTheUsCartridgesHoldThem) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto bytes = [&](std::uint32_t at) {
            return oracle.machine().read(retropp::MemoryRegion{.at = at, .size = 4});
        };
        EXPECT_EQ(bytes(kAddExp), (std::vector<std::uint8_t>{0x08, 0xC2, 0x21, 0xAD}));          // php; rep #$21
        EXPECT_EQ(bytes(kLearnItemMagic), (std::vector<std::uint8_t>{0xDA, 0xA0, 0x06, 0x00}));  // phx; ldy #6
        EXPECT_EQ(bytes(kCheckLevelUp), (std::vector<std::uint8_t>{0x64, 0xF8, 0x7B, 0xBD}));    // stz $f8; tdc
        EXPECT_EQ(bytes(kLearnAbilities), (std::vector<std::uint8_t>{0xA2, 0x00, 0x00, 0xC9}));  // ldx #0; cmp
        EXPECT_EQ(bytes(kLearnRage), (std::vector<std::uint8_t>{0xA2, 0x0A, 0xBD, 0x02}));       // ldx #$0a
        EXPECT_EQ(bytes(kExecBtlGfx), (std::vector<std::uint8_t>{0xDA, 0x5A, 0x08, 0xE2}));      // phx; phy; php
        EXPECT_EQ(bytes(kExecBtlGfxExt), (std::vector<std::uint8_t>{0x48, 0x7B, 0x68, 0x0A}));   // pha; tdc; pla
    });
}

TEST_F(RewardsOracleTest, TheirCallsReturnWhereTheWinMakesThem) {
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        const auto callBefore = [&](std::uint32_t at) {
            return oracle.machine().read(retropp::MemoryRegion{.at = at - 3, .size = 3});
        };
        EXPECT_EQ(callBefore(0xC25E4F), (std::vector<std::uint8_t>{0x20, 0x35, 0x62}));  // jsr AddExp
        EXPECT_EQ(callBefore(0xC25E59), (std::vector<std::uint8_t>{0x20, 0x35, 0x62}));  // the Exp. Egg's
        EXPECT_EQ(callBefore(0xC25E6A), (std::vector<std::uint8_t>{0x20, 0xEF, 0x5F}));  // jsr LearnItemMagic
        EXPECT_EQ(callBefore(0xC25EAF), (std::vector<std::uint8_t>{0x20, 0x6D, 0x60}));  // jsr CheckLevelUp
        EXPECT_EQ(callBefore(0xC260BF), (std::vector<std::uint8_t>{0x20, 0xB6, 0x61}));  // jsr LearnAbilities
        EXPECT_EQ(callBefore(0xC248EE), (std::vector<std::uint8_t>{0x20, 0x07, 0x4A}));  // jsr LearnRage
    });
}

TEST_F(RewardsOracleTest, AnImageWithoutAnyOfThemIsRefused) {
    for (const std::uint32_t routine :
         {kAddExp, kLearnItemMagic, kCheckLevelUp, kLearnAbilities, kLearnRage, kExecBtlGfx, kExecBtlGfxExt}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(routine)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << routine;
        });
    }
}

TEST_F(RewardsOracleTest, TheGraphicsCommandsAreFoundThroughTheCartridgesOwnTable) {
    for (const std::size_t command : {std::size_t{4}, std::size_t{5}, std::size_t{10}}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(kGraphicsCommandTable) + command * 2] ^= 0x01;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << command;
        });
    }
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

// --- the graphics program ------------------------------------------------------------------------

TEST_F(RewardsOracleTest, ACallIsRecordedWithTheCellsTheGraphicsProgramReads) {
    withOracle([](DamageOracle& oracle) {
        retropp::Vm& machine = oracle.machine();
        const std::vector<std::uint8_t> script{0x02, 0x2E, 0x00, 0x00, 0xFF};
        machine.write(retropp::MemoryRegion{.at = 0x7E2D6E, .size = 5}, script);
        const std::vector<std::uint8_t> variables{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        machine.write(retropp::MemoryRegion{.at = 0x7E2F35, .size = 12}, variables);
        const std::vector<std::uint8_t> slot{3};
        machine.write(retropp::MemoryRegion{.at = 0x7E0010, .size = 1}, slot);
        const std::vector<trace::GraphicsCall> calls = oracle.graphicsCommand(trace::kPlayBattleScript);
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(calls[0].command, trace::kPlayBattleScript);
        EXPECT_EQ(calls[0].slot, 3);
        EXPECT_EQ(std::vector<std::uint8_t>(calls[0].script.begin(), calls[0].script.end()), script);
        EXPECT_EQ(std::vector<std::uint8_t>(calls[0].messageVariables.begin(), calls[0].messageVariables.end()),
                  variables);
        // "{name} gained a level" reads the second variable's low byte.
        EXPECT_EQ(trace::messageShown(calls[0]),
                  (MessageShown{.message = battle::kMessageGainedALevel, .second = 4}));
    });
}

TEST_F(RewardsOracleTest, AnItemObtainedIsAddedToTheListAsTheCartridgeDoesIt) {
    withOracle([](DamageOracle& oracle) {
        retropp::Vm& machine = oracle.machine();
        const std::vector<std::uint8_t> record{0x10, 0x20, 0x30, 0x02, 0x40};
        machine.write(retropp::MemoryRegion{.at = 0x7E2E72, .size = 5}, record);
        const std::vector<std::uint8_t> added{17};  // the list's place is the count's low four bits: 1
        machine.write(retropp::MemoryRegion{.at = 0x7E64DA, .size = 1}, added);
        const std::vector<trace::GraphicsCall> calls = oracle.graphicsCommand(0x05);
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(std::vector<std::uint8_t>(calls[0].item.begin(), calls[0].item.end()), record);
        EXPECT_EQ(machine.read(retropp::MemoryRegion{.at = 0x7E602D + 5, .size = 5}), record);
        EXPECT_EQ(machine.read(retropp::MemoryRegion{.at = 0x7E64DA, .size = 1}), (std::vector<std::uint8_t>{18}));
    });
}

TEST_F(RewardsOracleTest, TheListIsMergedIntoTheInventoryAsTheCartridgeDoesIt) {
    withOracle([](DamageOracle& oracle) {
        retropp::Vm& machine = oracle.machine();
        // The inventory: Potion in the first slot with 98, the second slot empty, the rest full.
        std::vector<std::uint8_t> inventory(256 * 5, 0);
        for (std::size_t slot = 0; slot < 256; ++slot) {
            inventory[slot * 5] = static_cast<std::uint8_t>(0x80 + slot % 0x60);
            inventory[slot * 5 + 3] = 1;
        }
        inventory[0] = 0xE8;
        inventory[3] = 98;
        inventory[5] = 0xFF;
        machine.write(retropp::MemoryRegion{.at = 0x7E2686, .size = 256 * 5}, inventory);
        // The list: two more of the first item, then an item not held, then the end.
        std::vector<std::uint8_t> list(16 * 5, 0xFF);
        const std::vector<std::uint8_t> held{0xE8, 0x00, 0x00, 0x02, 0x00};
        const std::vector<std::uint8_t> fresh{0xFE, 0x01, 0x02, 0x01, 0x03};
        std::copy(held.begin(), held.end(), list.begin());
        std::copy(fresh.begin(), fresh.end(), list.begin() + 5);
        machine.write(retropp::MemoryRegion{.at = 0x7E602D, .size = 16 * 5}, list);
        const std::vector<std::uint8_t> merged{0};
        machine.write(retropp::MemoryRegion{.at = 0x7E64DB, .size = 1}, merged);
        (void)oracle.graphicsCommand(0x0A);
        EXPECT_EQ(machine.read(retropp::MemoryRegion{.at = 0x7E2686 + 3, .size = 1}),
                  (std::vector<std::uint8_t>{99}));  // 98 + 2, held to 99
        EXPECT_EQ(machine.read(retropp::MemoryRegion{.at = 0x7E2686 + 5, .size = 5}), fresh);
        EXPECT_EQ(machine.read(retropp::MemoryRegion{.at = 0x7E64DB, .size = 1}), (std::vector<std::uint8_t>{2}));
        EXPECT_EQ(machine.read(retropp::MemoryRegion{.at = 0x7E602D, .size = 1}), (std::vector<std::uint8_t>{0xFF}));
    });
}

TEST_F(RewardsOracleTest, APartyWrittenIntoItsCellsReadsBackUnchanged) {
    std::mt19937 rng{trace::kSweepSeed};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < 20; ++draw) {
            RewardScenario s = trace::drawExperienceScenario(rng);
            s.share = 0;
            game::CharacterRecord& record = s.cells.party.characters[s.recordIndex];
            record.experience = std::min(record.experience, kMaxExperience);
            const RewardResult guest = oracle.addExperience(s);
            RewardResult unchanged{.cells = s.cells, .switches = s.switches, .flags = s.flags,
                                   .levelMessageToTell = s.levelMessageToTell};
            EXPECT_EQ(trace::differences(unchanged, guest), "") << trace::describe(s);
        }
    });
}

// --- each rule by hand, on both sides -------------------------------------------------------------

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
        withOracle([&](DamageOracle& oracle) {
            EXPECT_EQ(trace::differences(port, oracle.addExperience(shared)), "") << c.experience << " + " << c.share;
        });
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
    withOracle([&](DamageOracle& oracle) { EXPECT_EQ(trace::differences(port, oracle.learnFromEquipment(s)), ""); });
}

TEST_F(RewardsOracleTest, WithoutMagiciteASpellStoredEarnsNoMagicPoints) {
    game::CharacterRecord record = recordOf(CharacterPropId::CELES, 40, 0);
    record.shield = ItemId::PALADIN_SHLD;
    RewardScenario s = scenarioFor(6, record);
    s.magicPoints = 3;
    const RewardResult port = trace::portLearnFromEquipment(s);
    EXPECT_FALSE(port.flags.magicPointsEarned);
    withOracle([&](DamageOracle& oracle) { EXPECT_EQ(trace::differences(port, oracle.learnFromEquipment(s)), ""); });
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
    withOracle([&](DamageOracle& oracle) { EXPECT_EQ(trace::differences(port, oracle.levelUps(s)), ""); });
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
    withOracle([&](DamageOracle& oracle) { EXPECT_EQ(trace::differences(port, oracle.levelUps(s)), ""); });
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
    withOracle([&](DamageOracle& oracle) { EXPECT_EQ(trace::differences(port, oracle.levelUps(s)), ""); });
}

TEST_F(RewardsOracleTest, Level99IsNotLookedPast) {
    RewardScenario s = scenarioFor(1, recordOf(CharacterPropId::LOCKE, 99, kMaxExperience));
    const RewardResult port = trace::portLevelUps(s);
    EXPECT_EQ(port.cells.party.characters[1].level, 99);
    EXPECT_TRUE(port.messages.empty());
    withOracle([&](DamageOracle& oracle) { EXPECT_EQ(trace::differences(port, oracle.levelUps(s)), ""); });
}

TEST_F(RewardsOracleTest, CelesLearnsMuddleAt32) {
    RewardScenario s;
    s.actor = CharacterPropId::CELES;
    s.level = 32;
    const RewardResult port = trace::portLearnAbilities(s);
    EXPECT_EQ(port.cells.party.spells[6].progress[static_cast<std::size_t>(AttackId::MUDDLE)],
              SpellProgress::kSpellJustLearned);
    withOracle([&](DamageOracle& oracle) { EXPECT_EQ(trace::differences(port, oracle.learnAbilities(s)), ""); });
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
    withOracle([&](DamageOracle& oracle) { EXPECT_EQ(trace::differences(port, oracle.learnRages(s)), ""); });
}

// --- the walk and the sweeps ---------------------------------------------------------------------

TEST_F(RewardsOracleTest, EveryActorAtEveryLevelLearnsWhatTheCartridgeTeaches) {
    std::mt19937 rng{trace::kSweepSeed};
    int disagreements = 0;
    withOracle([&](DamageOracle& oracle) {
        for (int actor = 0; actor < 16; ++actor) {
            for (int level = 1; level <= 99; ++level) {
                for (int mix = 0; mix < 3; ++mix) {
                    const RewardScenario s = trace::abilityCase(static_cast<CharacterPropId>(actor),
                                                                static_cast<std::uint8_t>(level), mix, rng);
                    const std::string differs = trace::differences(trace::portLearnAbilities(s), oracle.learnAbilities(s));
                    if (!differs.empty() && ++disagreements <= 5) {
                        ADD_FAILURE() << trace::describe(s) << "\n" << differs;
                    }
                }
            }
        }
    });
    EXPECT_EQ(disagreements, 0);
}

template <typename Draw, typename Port, typename Guest>
int sweep(Draw draw, Port port, Guest guest) {
    std::mt19937 rng{trace::kSweepSeed};
    int disagreements = 0;
    trace::runOnLoop([&] {
        DamageOracle oracle{assets::stripCopierHeader(vanillaRom())};
        oracle.start();
        for (int i = 0; i < kDraws; ++i) {
            const RewardScenario s = draw(rng);
            const std::string differs = trace::differences(port(s), guest(oracle, s));
            if (!differs.empty() && ++disagreements <= 5) {
                ADD_FAILURE() << trace::describe(s) << "\n" << differs;
            }
        }
    });
    return disagreements;
}

TEST_F(RewardsOracleTest, ExperienceSweep) {
    EXPECT_EQ(sweep(trace::drawExperienceScenario, trace::portAddExperience,
                    [](DamageOracle& o, const RewardScenario& s) { return o.addExperience(s); }),
              0);
}

TEST_F(RewardsOracleTest, EquipmentSweep) {
    EXPECT_EQ(sweep(trace::drawEquipmentScenario, trace::portLearnFromEquipment,
                    [](DamageOracle& o, const RewardScenario& s) { return o.learnFromEquipment(s); }),
              0);
}

TEST_F(RewardsOracleTest, LevelSweep) {
    EXPECT_EQ(sweep(trace::drawLevelScenario, trace::portLevelUps,
                    [](DamageOracle& o, const RewardScenario& s) { return o.levelUps(s); }),
              0);
}

TEST_F(RewardsOracleTest, RageSweep) {
    EXPECT_EQ(sweep(trace::drawRageScenario, trace::portLearnRages,
                    [](DamageOracle& o, const RewardScenario& s) { return o.learnRages(s); }),
              0);
}

}  // namespace
}  // namespace ostinato::test
