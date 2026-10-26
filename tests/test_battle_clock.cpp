// Battle time: the two-frame update, the ATB rate, the gauges and advance waits, and the sixteen
// timer jobs. The expected values come from docs/contracts/battle_core.md, which cites the line of
// the original each one is read from.

#include <gtest/gtest.h>

#include <variant>

#include "battle/actions.h"
#include "battle/battle_core.h"
#include "battle_test_support.h"
#include "data/rng_table.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;

const BattlerSlot kHero = BattlerSlot::character(0);
const BattlerSlot kMonster = BattlerSlot::monster(0);

template <typename T>
int countImmediate(const BattleState& state) {
    int n = 0;
    for (const auto& action : state.immediateActions) {
        n += std::holds_alternative<T>(action) ? 1 : 0;
    }
    return n;
}

// --- the two-frame update ------------------------------------------------------------------------

TEST(BattleTime, AdvancesOnEverySecondFrame) {
    // UpdateBattleTime, :2675-2681: no update while the frame counter is the last update's or
    // one past it.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    for (int frame = 1; frame <= 10; ++frame) {
        battle::tickBattleTime(state, true, executor);
        EXPECT_EQ(state.turnCounter, frame / 2) << "frame " << frame;
    }
}

TEST(BattleTime, ResumesAtOnceAfterTimeWasStopped) {
    // While the presentation stops time it does not call the update; the frame counter keeps
    // going, so the first running frame afterwards is two or more frames on.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::tickBattleTime(state, true, executor);
    battle::tickBattleTime(state, true, executor);
    ASSERT_EQ(state.turnCounter, 1);
    for (int frame = 0; frame < 5; ++frame) {
        battle::tickBattleTime(state, false, executor);
    }
    EXPECT_EQ(state.turnCounter, 1);
    battle::tickBattleTime(state, true, executor);
    EXPECT_EQ(state.turnCounter, 2);
}

TEST(BattleTime, AnOpenSubMenuStopsTimeOnlyInWaitMode) {
    // :2672-2674: the menu's stop is honored only in wait mode, and each stopped frame counts as
    // the last update.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.timeStoppedByMenu = true;
    for (int frame = 0; frame < 6; ++frame) {
        battle::tickBattleTime(state, true, executor);
    }
    EXPECT_EQ(state.turnCounter, 0);
    state.timeStoppedByMenu = false;
    battle::tickBattleTime(state, true, executor);
    EXPECT_EQ(state.turnCounter, 0);
    battle::tickBattleTime(state, true, executor);
    EXPECT_EQ(state.turnCounter, 1);

    BattleState active = makeBattle();
    active.config.waitMode = false;
    active.timeStoppedByMenu = true;
    battle::tickBattleTime(active, true, executor);
    battle::tickBattleTime(active, true, executor);
    EXPECT_EQ(active.turnCounter, 1);
}

// --- the ATB rate -------------------------------------------------------------------------------

TEST(AtbRate, PartyMemberAtSpeedForty) {
    // CalcSpeed, :1539-1571: ((speed + 20) * (step * 1.5)) >> 4, step 64 normally, 32 under Slow,
    // 84 under Haste.
    BattleState state = makeBattle();
    battle::recalcAtbRate(state, kHero);
    EXPECT_EQ(state.at(kHero).atbRate, 360);
    EXPECT_EQ(state.at(kHero).statusTimerStep, 64);
    state.at(kHero).status.set(StatusId::HASTE);
    battle::recalcAtbRate(state, kHero);
    EXPECT_EQ(state.at(kHero).atbRate, 472);
    EXPECT_EQ(state.at(kHero).statusTimerStep, 84);
    state.at(kHero).status.set(StatusId::SLOW);
    battle::recalcAtbRate(state, kHero);
    EXPECT_EQ(state.at(kHero).atbRate, 180);  // Slow wins over Haste
    EXPECT_EQ(state.at(kHero).statusTimerStep, 32);
}

TEST(AtbRate, MonstersAreScaledByTheSpeedSetting) {
    // :1560-1563: a monster's speed + 20 is first scaled by (255 - 24 * setting) / 256.
    // Speed 40: (60 * factor) >> 8 = 59, 54, 48, 42, 37, 31, then * 96 >> 4.
    const int expected[] = {354, 324, 288, 252, 222, 186};
    for (std::uint8_t setting = 0; setting < 6; ++setting) {
        BattleState state = makeBattle();
        state.config.speedSetting = setting;
        battle::recalcAtbRate(state, kMonster);
        EXPECT_EQ(state.at(kMonster).atbRate, expected[setting]) << "setting " << int(setting);
    }
    BattleState state = makeBattle();
    state.config.speedSetting = 0;
    battle::recalcAtbRate(state, kHero);
    EXPECT_EQ(state.at(kHero).atbRate, 360);  // the party is not scaled
}

// --- the gauge ----------------------------------------------------------------------------------

TEST(AtbGaugeFill, AMonsterFillsAfter182UpdatesAndJoinsTheAdvanceWaitLine) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& monster = state.at(kMonster);
    monster.atbRate = 360;
    monster.gauge.counter = 0x0100;
    for (int update = 1; update <= 181; ++update) {
        runTimeUpdate(state, executor);
    }
    EXPECT_FALSE(monster.gauge.isFull());
    runTimeUpdate(state, executor);
    EXPECT_TRUE(monster.gauge.isFull());
    EXPECT_EQ(monster.gauge.counter, 0x00F0);  // 256 + 182 * 360 - 65536 = 240 kept
    EXPECT_TRUE(monster.atbStopped);
    EXPECT_FALSE(monster.advanceWait.has_value());
    EXPECT_TRUE(state.advanceWaitQueue.contains(kMonster));
}

TEST(AtbGaugeFill, APartyMemberTakingCommandsOpensItsMenu) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& hero = state.at(kHero);
    hero.takesMenuInput = true;
    hero.gauge.counter = 0xFFFF;
    runTimeUpdate(state, executor);
    EXPECT_TRUE(hero.gauge.isFull());
    EXPECT_TRUE(hero.menuMayOpen);
    EXPECT_TRUE(hero.inBattleMenu);
    ASSERT_EQ(state.requests.size(), 1u);
    const auto* open = std::get_if<battle::OpenBattleMenu>(&state.requests[0]);
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->character, kHero);
    EXPECT_FALSE(open->controllingMonster);
    EXPECT_FALSE(state.advanceWaitQueue.contains(kHero));
}

TEST(AtbGaugeFill, TheMenuStaysShutWhenMenusAreDisabledOrInTheAir) {
    // _inputwindowopen, :2789-2792.
    for (int variant = 0; variant < 2; ++variant) {
        BattleState state = makeBattle();
        battle::NullExecutor executor;
        battle::Battler& hero = state.at(kHero);
        hero.takesMenuInput = true;
        hero.gauge.counter = 0xFFFF;
        if (variant == 0) {
            state.menusDisabled = true;
        } else {
            hero.mayActAfterJump = false;
        }
        runTimeUpdate(state, executor);
        EXPECT_TRUE(hero.menuMayOpen);
        EXPECT_FALSE(hero.inBattleMenu);
        EXPECT_TRUE(state.requests.empty());
    }
}

TEST(AtbGaugeFill, TheControlMenuFlagDependsOnTheVersion) {
    // :2795-2801: the English versions read the control-menu flag, the Japanese the Chant status.
    for (GameVersion version : {GameVersion::US_1_1, GameVersion::JP_1_0}) {
        BattleState state = makeBattle();
        state.version = version;
        battle::NullExecutor executor;
        battle::Battler& hero = state.at(kHero);
        hero.takesMenuInput = true;
        hero.gauge.counter = 0xFFFF;
        hero.triggers.usesControlMenu = true;
        runTimeUpdate(state, executor);
        const auto& open = std::get<battle::OpenBattleMenu>(state.requests.at(0));
        EXPECT_EQ(open.controllingMonster, version != GameVersion::JP_1_0);
    }
    BattleState state = makeBattle();
    state.version = GameVersion::JP_1_0;
    battle::NullExecutor executor;
    state.at(kHero).takesMenuInput = true;
    state.at(kHero).gauge.counter = 0xFFFF;
    state.at(kHero).status.set(StatusId::CHANT);
    runTimeUpdate(state, executor);
    EXPECT_TRUE(std::get<battle::OpenBattleMenu>(state.requests.at(0)).controllingMonster);
}

TEST(AtbGaugeFill, BattlersThatCannotTakeATurnDoNotFill) {
    // :2690-2696: not present, held by Zinger, in the menu, held, or already queued.
    for (int variant = 0; variant < 6; ++variant) {
        BattleState state = makeBattle();
        battle::NullExecutor executor;
        battle::Battler& monster = state.at(kMonster);
        monster.gauge.counter = 0x1000;
        switch (variant) {
            case 0: monster.present = false; break;
            case 1: state.heldByZinger = kMonster; break;
            case 2: monster.inBattleMenu = true; break;
            case 3: monster.heldByStatus = true; break;
            case 4: monster.heldByControl = true; break;
            case 5: monster.actionQueued = true; break;
        }
        runTimeUpdate(state, executor);
        EXPECT_EQ(monster.gauge.counter, 0x1000) << "variant " << variant;
    }
}

// --- the advance wait ----------------------------------------------------------------------------

TEST(AdvanceWait, RunsAtHalfTheAtbRateUntilItReachesTheWait) {
    // gaugefull, :2728-2749: rate / 2 per update, ready when the high byte reaches the wait.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& hero = state.at(kHero);
    hero.atbStopped = true;
    hero.gauge.counter = 0x0000;
    hero.advanceWait = 16;
    hero.advanceWaitCounter = 0;
    for (int update = 1; update <= 22; ++update) {
        runTimeUpdate(state, executor);
    }
    EXPECT_FALSE(hero.actionQueued);
    EXPECT_EQ(hero.advanceWaitCounter, 22 * 180);
    runTimeUpdate(state, executor);
    EXPECT_TRUE(hero.actionQueued);
    EXPECT_TRUE(state.actionQueue.contains(kHero));
    EXPECT_FALSE(hero.advanceWait.has_value());
}

TEST(AdvanceWait, AWaitNotYetSetRunsToTheLongest) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& hero = state.at(kHero);
    hero.atbStopped = true;
    hero.gauge.counter = 0x0000;
    hero.advanceWaitCounter = 0xFE00;
    runTimeUpdate(state, executor);
    EXPECT_FALSE(hero.actionQueued);
    runTimeUpdate(state, executor);
    EXPECT_TRUE(hero.actionQueued);
}

TEST(AdvanceWait, AFleeingPartyHoldsItsCharactersWaits) {
    // :2697-2710: while the party runs, a character on its side, on the ground and not a zombie
    // does not advance its wait.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.partyRunning = true;
    state.runDifficulty = 50;
    state.at(kHero).atbStopped = true;
    state.at(kHero).gauge.counter = 0;
    runTimeUpdate(state, executor);
    EXPECT_EQ(state.at(kHero).advanceWaitCounter, 0);

    state.at(kHero).status.set(StatusId::ZOMBIE);
    runTimeUpdate(state, executor);
    EXPECT_EQ(state.at(kHero).advanceWaitCounter, 180);

    state.at(kMonster).atbStopped = true;
    state.at(kMonster).gauge.counter = 0;
    runTimeUpdate(state, executor);
    EXPECT_EQ(state.at(kMonster).advanceWaitCounter, 288 / 2);
}

// --- the timer jobs ------------------------------------------------------------------------------

TEST(TimerJobs, TheSixteenJobsTakeTurns) {
    // DecCounters, :14839-14891: the phase before it moves on picks the job — battler 0-9, then
    // the battle's own six.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.at(kMonster).statusTimer = 255;
    state.at(kMonster).statusTimerStep = 1;
    state.at(kMonster).condemnedNumber = 3;
    for (int update = 0; update < 4; ++update) {
        runTimeUpdate(state, executor);
    }
    EXPECT_EQ(state.at(kMonster).condemnedNumber, 3);  // slot 4's job has not come round
    runTimeUpdate(state, executor);
    EXPECT_EQ(state.at(kMonster).condemnedNumber, 2);
    EXPECT_EQ(state.counterPhase, 5);
    for (int update = 0; update < 6; ++update) {
        runTimeUpdate(state, executor);
    }
    EXPECT_EQ(state.battleCounter, 0);
    runTimeUpdate(state, executor);  // job 11
    EXPECT_EQ(state.battleCounter, 1);
}

TEST(TimerJobs, ABattlersTimersStepWhenTheirSumPasses255) {
    // :14853-14857: step 64 carries on every fourth turn of the battler's job.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.at(kHero).condemnedNumber = 10;
    for (int turn = 1; turn <= 3; ++turn) {
        runTimerJobFor(state, executor, kHero);
    }
    EXPECT_EQ(state.at(kHero).condemnedNumber, 10);
    runTimerJobFor(state, executor, kHero);
    EXPECT_EQ(state.at(kHero).condemnedNumber, 9);
}

TEST(TimerJobs, StopRunsOutAndHoldsTheOtherTimers) {
    // :14858-14863: a running Stop counter takes the step; the others wait. When Stop runs out,
    // Reflect, Freeze and Sleep take their step too.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& hero = state.at(kHero);
    hero.statusTimerStep = 255;
    hero.statusTimer = 1;
    hero.stopTimer = 2;
    hero.condemnedNumber = 5;
    hero.reflectTimer = 1;
    runTimerJobFor(state, executor, kHero);
    EXPECT_EQ(hero.stopTimer, 1);
    EXPECT_EQ(hero.condemnedNumber, 5);
    EXPECT_EQ(hero.reflectTimer, 1);
    EXPECT_TRUE(state.immediateActions.empty());
    hero.statusTimer = 1;
    runTimerJobFor(state, executor, kHero);
    ASSERT_EQ(state.immediateActions.size(), 1u);
    const auto& expired = std::get<battle::StatusTimersExpired>(state.immediateActions[0]);
    EXPECT_EQ(expired.battler, kHero);
    EXPECT_TRUE(expired.stop);
    EXPECT_TRUE(expired.reflect);
    EXPECT_FALSE(expired.freeze);
    EXPECT_EQ(hero.condemnedNumber, 5);
}

TEST(TimerJobs, TheCondemnedCountStrikesWithDoomAtOne) {
    // :14867-14874: a count of 2 or more steps down; reaching 1 casts Doom.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& hero = state.at(kHero);
    hero.statusTimerStep = 255;
    hero.condemnedNumber = 2;
    hero.statusTimer = 1;
    runTimerJobFor(state, executor, kHero);
    EXPECT_EQ(hero.condemnedNumber, 1);
    ASSERT_EQ(state.immediateActions.size(), 1u);
    const auto& doom = std::get<battle::SpellWithoutCaster>(state.immediateActions[0]);
    EXPECT_EQ(doom.spell, AttackId::DOOM);
    EXPECT_EQ(doom.target, kHero);
    hero.statusTimer = 1;
    runTimerJobFor(state, executor, kHero);
    EXPECT_EQ(hero.condemnedNumber, 1);  // 1 stays: the count has run out
    EXPECT_EQ(state.immediateActions.size(), 1u);
}

TEST(TimerJobs, ABattlerHeldByStatusSkipsItsTimers) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& hero = state.at(kHero);
    hero.statusTimerStep = 255;
    hero.statusTimer = 1;
    hero.heldByStatus = true;
    hero.condemnedNumber = 4;
    runTimerJobFor(state, executor, kHero);
    EXPECT_EQ(hero.condemnedNumber, 4);
    EXPECT_EQ(hero.damageOverTimePhase, 0);
}

TEST(TimerJobs, PoisonAndRegenHaveTheirBeats) {
    // :14879-14884 and StatusCounterTbl: after the step, beats 0 and 4 of eight make regen due,
    // beat 2 poison.
    const struct {
        std::uint8_t before;
        bool regen;
        bool poison;
    } beats[] = {{7, true, false}, {0, false, false}, {1, false, true}, {3, true, false}, {4, false, false}};
    for (const auto& beat : beats) {
        BattleState state = makeBattle();
        battle::NullExecutor executor;
        battle::Battler& hero = state.at(kHero);
        hero.statusTimerStep = 255;
        hero.statusTimer = 1;
        hero.damageOverTimePhase = beat.before;
        runTimerJobFor(state, executor, kHero);
        EXPECT_EQ(hero.triggers.regenDue, beat.regen) << int(beat.before);
        EXPECT_EQ(hero.triggers.poisonDue, beat.poison) << int(beat.before);
    }
}

TEST(TimerJobs, ADueBeatBecomesADamageOverTimeCounterAction) {
    // rigenetimer, :14990-15030.
    struct Case {
        bool poisonDue;
        bool regenDue;
        StatusId status;
        bool phantasm;
        std::optional<battle::DamageOverTimeKind> expected;
    };
    const Case cases[] = {
        {true, false, StatusId::POISON, false, battle::DamageOverTimeKind::Poison},
        {true, false, StatusId::REGEN, false, std::nullopt},  // poison due, not poisoned
        {false, true, StatusId::REGEN, false, battle::DamageOverTimeKind::Regen},
        {false, true, StatusId::SAP, false, battle::DamageOverTimeKind::Sap},
        {false, true, StatusId::BLIND, true, battle::DamageOverTimeKind::Sap},
        {false, true, StatusId::BLIND, false, std::nullopt},
    };
    for (const Case& c : cases) {
        BattleState state = makeBattle();
        battle::NullExecutor executor;
        battle::Battler& hero = state.at(kHero);
        hero.statusTimerStep = 255;
        hero.statusTimer = 1;
        hero.triggers.poisonDue = c.poisonDue;
        hero.triggers.regenDue = c.regenDue;
        hero.triggers.phantasm = c.phantasm;
        hero.status.set(c.status);
        runTimerJobFor(state, executor, kHero);
        EXPECT_FALSE(c.poisonDue && hero.triggers.poisonDue);
        if (!c.expected) {
            EXPECT_TRUE(hero.counterActions.empty());
            EXPECT_FALSE(hero.dotActionPending);
            continue;
        }
        ASSERT_EQ(hero.counterActions.size(), 1u);
        EXPECT_EQ(std::get<battle::DamageOverTime>(hero.counterActions[0]).kind, *c.expected);
        EXPECT_TRUE(hero.dotActionPending);
        EXPECT_TRUE(state.counterQueue.contains(kHero));
    }
}

TEST(TimerJobs, SeizeDrainsWhenNoBeatIsDue) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& monster = state.at(kMonster);
    monster.statusTimerStep = 255;
    monster.statusTimer = 1;
    monster.seizing = kHero;
    runTimerJobFor(state, executor, kMonster);
    ASSERT_EQ(monster.counterActions.size(), 1u);
    EXPECT_EQ(std::get<battle::SeizeDrain>(monster.counterActions[0]).seized, kHero);
    EXPECT_TRUE(monster.dotActionPending);
    monster.statusTimer = 1;
    runTimerJobFor(state, executor, kMonster);
    EXPECT_EQ(monster.counterActions.size(), 1u);  // one pending at a time
}

TEST(TimerJobs, ReflectFreezeAndSleepRunOut) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    battle::Battler& hero = state.at(kHero);
    hero.statusTimerStep = 255;
    hero.statusTimer = 1;
    hero.freezeTimer = 1;
    hero.sleepTimer = 1;
    hero.reflectTimer = 3;
    runTimerJobFor(state, executor, kHero);
    ASSERT_EQ(state.immediateActions.size(), 1u);
    const auto& expired = std::get<battle::StatusTimersExpired>(state.immediateActions[0]);
    EXPECT_FALSE(expired.stop);
    EXPECT_FALSE(expired.reflect);
    EXPECT_TRUE(expired.freeze);
    EXPECT_TRUE(expired.sleep);
    EXPECT_EQ(hero.reflectTimer, 2);
}

TEST(TimerJobs, RouletteStrikesItsChoiceWithDoom) {
    // GlobalCounter_00, :15040-15062.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.rouletteTarget = kMonster;
    state.counterPhase = 10;
    runTimeUpdate(state, executor);
    EXPECT_FALSE(state.rouletteTarget.has_value());
    ASSERT_EQ(state.immediateActions.size(), 1u);
    EXPECT_EQ(std::get<battle::SpellWithoutCaster>(state.immediateActions[0]).target, kMonster);
}

TEST(TimerJobs, TheBattleCounterAndTheLivingBattlersTimers) {
    // GlobalCounter_01, :15099-15114: every present battler not dead or petrified.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.at(kMonster).status.set(StatusId::PETRIFY);
    state.counterPhase = 11;
    runTimeUpdate(state, executor);
    EXPECT_EQ(state.battleCounter, 1);
    EXPECT_EQ(state.at(kHero).turnTimer, 1);
    EXPECT_EQ(state.at(kMonster).turnTimer, 0);
    EXPECT_EQ(state.at(BattlerSlot::character(3)).turnTimer, 0);  // not present
}

// --- running away -------------------------------------------------------------------------------

TEST(Escape, NoDifficultyMeansACharacterGetsAwayAtOnce) {
    // CheckRunAway, :15120-15132; CreateRunAwayAction, :15083-15094 — the escape starts once.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.partyRunning = true;
    state.runDifficulty = 0;
    state.at(kHero).statusTimerStep = 255;
    state.at(kHero).statusTimer = 1;
    runTimerJobFor(state, executor, kHero);
    EXPECT_TRUE(state.partyJustEscaped.has(kHero));
    EXPECT_EQ(countImmediate<battle::PartyEscape>(state), 1);
    battle::startPartyEscape(state);
    EXPECT_EQ(countImmediate<battle::PartyEscape>(state), 1);
}

TEST(Escape, EachTryAddsOneToTheRunFactor) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.partyRunning = true;
    state.runDifficulty = 6;
    battle::Battler& hero = state.at(kHero);
    hero.runFactor = 4;
    hero.statusTimerStep = 255;
    int expected = 0;
    while (!state.partyJustEscaped.has(kHero)) {
        hero.statusTimer = 1;
        const std::uint8_t entry = rngByte(static_cast<std::uint8_t>(state.rng.position() + 1));
        expected += 1 + ((entry * 4) >> 8);
        runTimerJobFor(state, executor, kHero);
        ASSERT_EQ(hero.runProgress, expected);
    }
    EXPECT_GE(hero.runProgress, 6);
    EXPECT_EQ(countImmediate<battle::PartyEscape>(state), 0);  // the escape starts from its own job
}

TEST(Escape, TheEscapeJobRetriesOncePerEightPasses) {
    // GlobalCounter_05, :15067-15076.
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.partyRunning = true;
    state.partyJustEscaped.add(kHero);
    for (std::uint8_t before : {std::uint8_t{0x0F}, std::uint8_t{0x1F}, std::uint8_t{0x3F}, std::uint8_t{0x6F}}) {
        state.counterPhase = before;  // after the update the phase reads 0x10, 0x20, 0x40, 0x70
        runTimeUpdate(state, executor);
        EXPECT_EQ(countImmediate<battle::PartyEscape>(state), 0) << int(before);
    }
    state.counterPhase = 0x7F;  // reads 0x80: this pass
    runTimeUpdate(state, executor);
    EXPECT_EQ(countImmediate<battle::PartyEscape>(state), 1);
}

TEST(Escape, WhenThePartyCannotRunTheEscapeJobSaysSo) {
    BattleState state = makeBattle();
    battle::NullExecutor executor;
    state.partyRunning = true;
    state.cannotRun = true;
    state.counterPhase = 15;
    runTimeUpdate(state, executor);
    EXPECT_EQ(countImmediate<battle::PartyEscape>(state), 1);
}

}  // namespace
}  // namespace ostinato::test
