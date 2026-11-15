// What a won battle gives, on the oracle (docs/contracts/battle_rewards.md): the five routines and the
// graphics program's entry are where the US cartridges hold them, their calls return where the win
// makes them, and an image without any of them is refused; the graphics program's calls are recorded
// and answered, its table of commands read from the cartridge, its item-list commands run as the
// cartridge holds them; a party written into its cells reads back unchanged; every actor at every
// level through the ability routine; then 500 drawn scenarios through each of the other four.
//
// Every test here asks the cartridge on the engine's machine, so a brief run of the tests
// (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h) skips them all.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cartridge.h"
#include "battle/battle_script.h"
#include "game/character_record.h"
#include "oracle/damage_oracle.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/game_limits.h"
#include "rewards_oracle_cases.h"
#include "vanilla_rom.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

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

// --- where the routines are ----------------------------------------------------------------------

TEST_F(RewardsOracleTest, TheRoutinesAreWhereTheUsCartridgesHoldThem) {
    OSTINATO_SKIP_WHEN_BRIEF();
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
    OSTINATO_SKIP_WHEN_BRIEF();
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
    OSTINATO_SKIP_WHEN_BRIEF();
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
    OSTINATO_SKIP_WHEN_BRIEF();
    for (const std::size_t command : {std::size_t{4}, std::size_t{5}, std::size_t{10}}) {
        trace::runOnLoop([&] {
            const std::span<const std::uint8_t> original = image();
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(kGraphicsCommandTable) + command * 2] ^= 0x01;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << command;
        });
    }
}

// --- the graphics program ------------------------------------------------------------------------

TEST_F(RewardsOracleTest, ACallIsRecordedWithTheCellsTheGraphicsProgramReads) {
    OSTINATO_SKIP_WHEN_BRIEF();
    withStartedOracle(image(), [](DamageOracle& oracle) {
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
    OSTINATO_SKIP_WHEN_BRIEF();
    withStartedOracle(image(), [](DamageOracle& oracle) {
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
    OSTINATO_SKIP_WHEN_BRIEF();
    withStartedOracle(image(), [](DamageOracle& oracle) {
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
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    withStartedOracle(image(), [&](DamageOracle& oracle) {
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

// --- the walk and the sweeps ---------------------------------------------------------------------

TEST_F(RewardsOracleTest, EveryActorAtEveryLevelLearnsWhatTheCartridgeTeaches) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::mt19937 rng{trace::kSweepSeed};
    int disagreements = 0;
    withStartedOracle(image(), [&](DamageOracle& oracle) {
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
    OSTINATO_SKIP_WHEN_BRIEF();
    EXPECT_EQ(sweep(trace::drawExperienceScenario, trace::portAddExperience,
                    [](DamageOracle& o, const RewardScenario& s) { return o.addExperience(s); }),
              0);
}

TEST_F(RewardsOracleTest, EquipmentSweep) {
    OSTINATO_SKIP_WHEN_BRIEF();
    EXPECT_EQ(sweep(trace::drawEquipmentScenario, trace::portLearnFromEquipment,
                    [](DamageOracle& o, const RewardScenario& s) { return o.learnFromEquipment(s); }),
              0);
}

TEST_F(RewardsOracleTest, LevelSweep) {
    OSTINATO_SKIP_WHEN_BRIEF();
    EXPECT_EQ(sweep(trace::drawLevelScenario, trace::portLevelUps,
                    [](DamageOracle& o, const RewardScenario& s) { return o.levelUps(s); }),
              0);
}

TEST_F(RewardsOracleTest, RageSweep) {
    OSTINATO_SKIP_WHEN_BRIEF();
    EXPECT_EQ(sweep(trace::drawRageScenario, trace::portLearnRages,
                    [](DamageOracle& o, const RewardScenario& s) { return o.learnRages(s); }),
              0);
}

}  // namespace
}  // namespace ostinato::test
