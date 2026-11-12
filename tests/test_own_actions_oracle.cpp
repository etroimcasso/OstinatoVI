// The battle's own actions on the oracle: damage over time, a spell with no caster, status timers
// running out and Seize's drain are where the US cartridges hold them and an image without any of
// them is refused; they read no cell a scenario leaves unwritten; and the port's actions
// (CommandExecutor::execute()) leave every cell as the cartridge's own commands do, by hand and over
// drawn battles, a script's SetStatus and ClearStatus among them.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ios>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "battle/action_queue.h"
#include "battle/attack.h"
#include "battle/battle_state.h"
#include "damage_oracle.h"
#include "ostinato/attack_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::AttackResult;
using trace::DamageOracle;
using trace::DamageScenario;

class OwnActionsOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // `body(oracle)` with the oracle started on the cartridge.
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

std::vector<std::uint8_t>* OwnActionsOracleTest::image_ = nullptr;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

// Damage over time, a spell with no caster, status timers running out, Seize's drain.
constexpr std::array<std::uint32_t, 4> kCommands{0xC2500B, 0xC24F5F, 0xC25161, 0xC251B2};

constexpr BattlerSlot kHero = BattlerSlot::character(0);
constexpr BattlerSlot kFoe = BattlerSlot::monster(0);

// A party member and a monster on the field, 1,000 HP of 1,000 and 100 MP each, level 10, stamina 40,
// every status able to take hold, one enemy left; the monster acting.
DamageScenario ownBattle() {
    DamageScenario s;
    for (const BattlerSlot slot : {kHero, kFoe}) {
        battle::Battler& battler = s.state.at(slot);
        battler.present = true;
        battler.hp = 1000;
        battler.maxHp = 1000;
        battler.mp = 100;
        battler.maxMp = 100;
        battler.level = 10;
        battler.stamina = 40;
        battler.inflictableStatuses = StatusSet{{0xFF, 0xFF, 0xFF, 0xFF}};
        s.state.targetableOrPresent.add(slot);
        s.state.countedInBattle.add(slot);
        s.state.alive.add(slot);
    }
    s.state.enemyCount = 1;
    s.attacker = kFoe;
    return s;
}

// The port's action over `s`, with the cartridge's command over the same battle leaving every cell
// the same.
AttackResult checked(DamageOracle& oracle, const DamageScenario& s, const battle::Action& action) {
    const AttackResult port = trace::portOwnAction(s, action);
    EXPECT_EQ(trace::differences(port, oracle.ownAction(s, action)), "");
    return port;
}

const std::string& cell(const AttackResult& result, const std::string& name) {
    return result.cells.at(name);
}

int hpOf(const AttackResult& result, BattlerSlot slot) {
    return std::stoi(cell(result, "HP of battler " + std::to_string(slot.index())));
}

std::string statusBytes(const StatusSet& set) {
    return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
           "/" + std::to_string(set.bytes[3]);
}

// Whether any attack in the script hit someone: a params block whose battlers hit are not none.
bool hitSomeone(const std::string& script) {
    const std::string hit = " hit ";
    for (std::size_t at = script.find(hit); at != std::string::npos; at = script.find(hit, at + hit.size())) {
        if (script[at + hit.size()] != '0') {
            return true;
        }
    }
    return false;
}

int countOf(const std::string& text, const std::string& what) {
    int count = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) {
        ++count;
    }
    return count;
}

TEST_F(OwnActionsOracleTest, AnImageWithoutAnyOfTheFourCommandsIsRefused) {
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        for (const std::uint32_t at : kCommands) {
            std::vector<std::uint8_t> other(original.begin(), original.end());
            other[offsetOf(at)] ^= 0xFF;
            EXPECT_THROW(DamageOracle{other}, std::runtime_error) << std::hex << at;
        }
    });
}

// Every cell the commands read came from the scenario or from the commands themselves, but the cells
// left as the machine has them.
TEST_F(OwnActionsOracleTest, TheBattlesOwnActionsReadNothingTheScenarioDoesNotWrite) {
    std::mt19937 rng{trace::kSweepSeed};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < 50; ++draw) {
            const trace::OwnActionCase c = trace::drawOwnActionCase(rng);
            (void)oracle.ownAction(c.scenario, c.action);
            std::string text;
            for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
                if (!trace::leftAsTheMachineHasIt(read.at)) {
                    char line[24];
                    std::snprintf(line, sizeof line, "$%02X:%04X ", read.at >> 16 & 0xFF, read.at & 0xFFFF);
                    text += line;
                }
            }
            EXPECT_EQ(text, "") << "draw " << draw;
        }
    });
}

// Poison's beat: an attack on the monster itself, its power the maximum HP times the stamina over
// 1,024 plus 2; the multiplier it starts from grows by two for the next beat.
TEST_F(OwnActionsOracleTest, APoisonBeatHurtsAndRaisesTheMultiplier) {
    DamageScenario s = ownBattle();
    s.state.at(kFoe).status.set(StatusId::POISON);
    s.state.at(kFoe).dotActionPending = true;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::DamageOverTime{.kind = battle::DamageOverTimeKind::Poison});
        EXPECT_LT(hpOf(port, kFoe), 1000);
        EXPECT_EQ(cell(port, "poison multiplier of battler 4"), "2");
        EXPECT_EQ(cell(port, "damage over time waiting of battler 4"), "0");
    });
}

TEST_F(OwnActionsOracleTest, ARegenBeatHeals) {
    DamageScenario s = ownBattle();
    s.state.at(kFoe).status.set(StatusId::REGEN);
    s.state.at(kFoe).hp = 500;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::DamageOverTime{.kind = battle::DamageOverTimeKind::Regen});
        EXPECT_GT(hpOf(port, kFoe), 500);
        EXPECT_EQ(cell(port, "poison multiplier of battler 4"), "0");
    });
}

// A battler held by a status takes no beat: the action only stops waiting.
TEST_F(OwnActionsOracleTest, ABeatOnABattlerHeldByAStatusDoesNothing) {
    DamageScenario s = ownBattle();
    s.state.at(kFoe).status.set(StatusId::POISON);
    s.state.at(kFoe).heldByStatus = true;
    s.state.at(kFoe).dotActionPending = true;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::DamageOverTime{.kind = battle::DamageOverTimeKind::Poison});
        EXPECT_EQ(cell(port, "script"), "");
        EXPECT_EQ(hpOf(port, kFoe), 1000);
        EXPECT_EQ(cell(port, "damage over time waiting of battler 4"), "0");
    });
}

// Doom when a condemned count runs out: the count stops either way, and with no enemy left nothing
// else happens.
TEST_F(OwnActionsOracleTest, DoomWithNoEnemyLeftOnlyStopsTheCount) {
    DamageScenario s = ownBattle();
    s.state.enemyCount = 0;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::SpellWithoutCaster{.spell = AttackId::DOOM, .target = kHero});
        EXPECT_EQ(cell(port, "script"), "");
        EXPECT_EQ(cell(port, "to work out again of battler 0"), "16");
        EXPECT_EQ(cell(port, "statuses of battler 0"), "0/0/0/0");
    });
}

TEST_F(OwnActionsOracleTest, DoomKillsTheCondemned) {
    const DamageScenario s = ownBattle();
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::SpellWithoutCaster{.spell = AttackId::DOOM, .target = kHero});
        EXPECT_EQ(cell(port, "statuses of battler 0"), statusBytes(StatusSet::of(StatusId::DEAD)));
    });
}

TEST_F(OwnActionsOracleTest, AStopTimerRunningOutEndsStop) {
    DamageScenario s = ownBattle();
    s.state.at(kHero).status.set(StatusId::STOP);
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::StatusTimersExpired{.battler = kHero, .stop = true});
        EXPECT_EQ(cell(port, "statuses of battler 0"), "0/0/0/0");
    });
}

// Sleep's timer wakes a sleeper with Tapir's animation, shown as Magic; on a battler already awake it
// takes nothing off and is shown as nothing.
TEST_F(OwnActionsOracleTest, ASleepTimerWakesTheSleeperWithTapir) {
    DamageScenario s = ownBattle();
    s.state.at(kHero).status.set(StatusId::SLEEP);
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::StatusTimersExpired{.battler = kHero, .sleep = true});
        EXPECT_EQ(cell(port, "statuses of battler 0"), "0/0/0/0");
        EXPECT_EQ(countOf(cell(port, "script"), "animation 2 120 "), 1);
    });
}

TEST_F(OwnActionsOracleTest, ASleepTimerOnAnAwakeBattlerTakesNothingOff) {
    DamageScenario s = ownBattle();
    s.state.at(kHero).status.set(StatusId::POISON);
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::StatusTimersExpired{.battler = kHero, .sleep = true});
        EXPECT_EQ(cell(port, "statuses of battler 0"), statusBytes(StatusSet::of(StatusId::POISON)));
        EXPECT_EQ(countOf(cell(port, "script"), "animation 2 "), 0);
    });
}

// A monster holding a party member in Seize drains its HP.
TEST_F(OwnActionsOracleTest, SeizesDrainTakesTheSeizedsHp) {
    DamageScenario s = ownBattle();
    s.state.at(kFoe).hp = 500;
    s.state.at(kFoe).seizing = kHero;
    s.state.at(kHero).seizedBy = kFoe;
    s.state.at(kFoe).dotActionPending = true;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, battle::SeizeDrain{.seized = kHero});
        EXPECT_LT(hpOf(port, kHero), 1000);
        EXPECT_GT(hpOf(port, kFoe), 500);
        EXPECT_EQ(cell(port, "damage over time waiting of battler 4"), "0");
    });
}

// What the sweep reached, read from the port's side.
struct Reached {
    int differing = 0;
    int poisoned = 0;
    int regenerated = 0;
    int sapped = 0;
    int held = 0;
    int noEnemies = 0;
    int doomStopsTheCount = 0;
    int spell = 0;
    int timers = 0;
    int woken = 0;
    int seized = 0;
    int scriptEffect = 0;
    std::string first;
};

TEST_F(OwnActionsOracleTest, FiveHundredDrawnActionsMatchTheCartridge) {
    Reached reached;
    std::mt19937 rng{trace::kSweepSeed};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < 500; ++draw) {
            const trace::OwnActionCase c = trace::drawOwnActionCase(rng);
            const AttackResult port = trace::portOwnAction(c.scenario, c.action);
            const std::string difference = trace::differences(port, oracle.ownAction(c.scenario, c.action));
            if (!difference.empty() && ++reached.differing == 1) {
                reached.first = "draw " + std::to_string(draw) + " (" + trace::describe(c.scenario) + "):\n" + difference;
            }
            const bool attacked = hitSomeone(cell(port, "script"));
            const battle::BattleState& state = c.scenario.state;
            if (const auto* dot = std::get_if<battle::DamageOverTime>(&c.action)) {
                const battle::Battler& battler = state.at(c.scenario.attacker);
                reached.noEnemies += state.enemyCount == 0 ? 1 : 0;
                reached.held += state.enemyCount != 0 && battler.heldByStatus ? 1 : 0;
                if (attacked) {
                    reached.poisoned += dot->kind == battle::DamageOverTimeKind::Poison ? 1 : 0;
                    reached.regenerated += dot->kind == battle::DamageOverTimeKind::Regen ? 1 : 0;
                    reached.sapped += dot->kind == battle::DamageOverTimeKind::Sap ? 1 : 0;
                }
            } else if (const auto* spell = std::get_if<battle::SpellWithoutCaster>(&c.action)) {
                reached.doomStopsTheCount += spell->spell == AttackId::DOOM && state.enemyCount == 0 ? 1 : 0;
                reached.spell += attacked ? 1 : 0;
            } else if (std::holds_alternative<battle::StatusTimersExpired>(c.action)) {
                reached.timers += attacked ? 1 : 0;
                reached.woken += countOf(cell(port, "script"), "animation 2 120 ") > 0 ? 1 : 0;
            } else if (std::holds_alternative<battle::SeizeDrain>(c.action)) {
                reached.seized += attacked ? 1 : 0;
            } else {
                reached.scriptEffect += attacked ? 1 : 0;
            }
        }
    });
    EXPECT_EQ(reached.differing, 0) << reached.differing << " of 500 differ; the first:\n" << reached.first;
    EXPECT_GT(reached.poisoned, 0);
    EXPECT_GT(reached.regenerated, 0);
    EXPECT_GT(reached.sapped, 0);
    EXPECT_GT(reached.held, 0);
    EXPECT_GT(reached.noEnemies, 0);
    EXPECT_GT(reached.doomStopsTheCount, 0);
    EXPECT_GT(reached.spell, 0);
    EXPECT_GT(reached.timers, 0);
    EXPECT_GT(reached.woken, 0);
    EXPECT_GT(reached.seized, 0);
    EXPECT_GT(reached.scriptEffect, 0);
}

}  // namespace
}  // namespace ostinato::test

