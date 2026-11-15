// The battle's own actions worked out by hand: damage over time, a spell with no caster, status
// timers running out and Seize's drain, each run by the port (CommandExecutor::execute()) and, where
// the run asks it (tests/with_cartridge.h), by the cartridge's own command over the same battle,
// every cell left the same. The commands on the oracle and the drawn battles are in
// tests/oracle/test_own_actions_oracle.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "battle/action_queue.h"
#include "battle/attack.h"
#include "battle/battle_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/attack_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "own_actions_oracle_cases.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::AttackResult;
using trace::DamageScenario;

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

// The port's action over `s`, with the cartridge's command over the same battle, where the run asks
// it, leaving every cell the same.
AttackResult checked(const Cartridge& cartridge, const DamageScenario& s, const battle::Action& action) {
    const AttackResult port = trace::portOwnAction(s, action);
    if (cartridge) {
        EXPECT_EQ(trace::differences(port, cartridge.oracle().ownAction(s, action)), "");
    }
    return port;
}

int hpOf(const AttackResult& result, BattlerSlot slot) {
    return std::stoi(cell(result, "HP of battler " + std::to_string(slot.index())));
}

std::string statusBytes(const StatusSet& set) {
    return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
           "/" + std::to_string(set.bytes[3]);
}

// Poison's beat: an attack on the monster itself, its power the maximum HP times the stamina over
// 1,024 plus 2; the multiplier it starts from grows by two for the next beat.
TEST_F(OwnActionsOracleTest, APoisonBeatHurtsAndRaisesTheMultiplier) {
    DamageScenario s = ownBattle();
    s.state.at(kFoe).status.set(StatusId::POISON);
    s.state.at(kFoe).dotActionPending = true;
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::DamageOverTime{.kind = battle::DamageOverTimeKind::Poison});
        EXPECT_LT(hpOf(port, kFoe), 1000);
        EXPECT_EQ(cell(port, "poison multiplier of battler 4"), "2");
        EXPECT_EQ(cell(port, "damage over time waiting of battler 4"), "0");
    });
}

TEST_F(OwnActionsOracleTest, ARegenBeatHeals) {
    DamageScenario s = ownBattle();
    s.state.at(kFoe).status.set(StatusId::REGEN);
    s.state.at(kFoe).hp = 500;
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::DamageOverTime{.kind = battle::DamageOverTimeKind::Regen});
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
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::DamageOverTime{.kind = battle::DamageOverTimeKind::Poison});
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
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::SpellWithoutCaster{.spell = AttackId::DOOM, .target = kHero});
        EXPECT_EQ(cell(port, "script"), "");
        EXPECT_EQ(cell(port, "to work out again of battler 0"), "16");
        EXPECT_EQ(cell(port, "statuses of battler 0"), "0/0/0/0");
    });
}

TEST_F(OwnActionsOracleTest, DoomKillsTheCondemned) {
    const DamageScenario s = ownBattle();
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::SpellWithoutCaster{.spell = AttackId::DOOM, .target = kHero});
        EXPECT_EQ(cell(port, "statuses of battler 0"), statusBytes(StatusSet::of(StatusId::DEAD)));
    });
}

TEST_F(OwnActionsOracleTest, AStopTimerRunningOutEndsStop) {
    DamageScenario s = ownBattle();
    s.state.at(kHero).status.set(StatusId::STOP);
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::StatusTimersExpired{.battler = kHero, .stop = true});
        EXPECT_EQ(cell(port, "statuses of battler 0"), "0/0/0/0");
    });
}

// Sleep's timer wakes a sleeper with Tapir's animation, shown as Magic; on a battler already awake it
// takes nothing off and is shown as nothing.
TEST_F(OwnActionsOracleTest, ASleepTimerWakesTheSleeperWithTapir) {
    DamageScenario s = ownBattle();
    s.state.at(kHero).status.set(StatusId::SLEEP);
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::StatusTimersExpired{.battler = kHero, .sleep = true});
        EXPECT_EQ(cell(port, "statuses of battler 0"), "0/0/0/0");
        EXPECT_EQ(countOf(cell(port, "script"), "animation 2 120 "), 1);
    });
}

TEST_F(OwnActionsOracleTest, ASleepTimerOnAnAwakeBattlerTakesNothingOff) {
    DamageScenario s = ownBattle();
    s.state.at(kHero).status.set(StatusId::POISON);
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::StatusTimersExpired{.battler = kHero, .sleep = true});
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
    withCartridge(OwnActionsOracleTest::image(), [&](const Cartridge& cartridge) {
        const AttackResult port = checked(cartridge, s, battle::SeizeDrain{.seized = kHero});
        EXPECT_LT(hpOf(port, kHero), 1000);
        EXPECT_GT(hpOf(port, kFoe), 500);
        EXPECT_EQ(cell(port, "damage over time waiting of battler 4"), "0");
    });
}

}  // namespace
}  // namespace ostinato::test
