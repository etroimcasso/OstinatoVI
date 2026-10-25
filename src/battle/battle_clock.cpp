// Battle time: the ATB gauges, the advance waits and the status timers.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "battle/actions.h"
#include "battle/battle_core.h"

namespace ostinato::battle {

namespace {

constexpr int kFramesPerTimeUpdate = 2;

// The status-timer steps: a battler's status timers take a step each time the timer passes 255.
constexpr std::uint8_t kSlowTimerStep = 32;
constexpr std::uint8_t kNormalTimerStep = 64;
constexpr std::uint8_t kHasteTimerStep = 84;

constexpr int kSpeedBase = 20;

// The sixteen timer jobs a time update cycles through: one for each battler, then six for the
// battle as a whole.
constexpr int kTimerJobs = 16;
constexpr int kRouletteJob = 10;
constexpr int kBattleCounterJob = 11;
constexpr int kEscapeJob = 15;

// How often the escape job retries when nothing stops the party running: one pass in eight.
constexpr std::uint8_t kEscapeRetryPhases = 0x70;

const StatusSet kOutOfTheFight = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY);

// The advance wait: the battler, gauge full, waits out the pause before its action is ready.
void advanceTheWait(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    const unsigned sum = static_cast<unsigned>(battler.advanceWaitCounter) + battler.atbRate / 2;
    battler.advanceWaitCounter = static_cast<std::uint16_t>(sum);
    const bool overflowed = sum > 0xFFFF;
    const int waited = battler.advanceWaitCounter >> 8;
    if (!overflowed && waited < battler.advanceWait.value_or(0xFF)) {
        return;
    }
    battler.advanceWait.reset();
    state.actionQueue.push(slot);
    battler.actionQueued = true;
}

// The ATB gauge fills; when it fills, the battler's turn comes up.
void fillTheGauge(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (!battler.gauge.fill(battler.atbRate)) {
        return;
    }
    if (slot.isCharacter()) {
        startPartyEscape(state);
    }
    battler.advanceWaitCounter &= 0x00FF;
    battler.advanceWait.reset();
    if (!battler.atbStopped) {
        battler.atbStopped = true;
        if (!battler.takesMenuInput) {
            joinAdvanceWaitLine(state, slot);
            return;
        }
    }
    battler.menuMayOpen = true;
    openMenuOrJoinLine(state, slot);
}

// Whether a fleeing party holds this battler's advance wait: a character running with the party,
// on the ground, on the party's side and not a zombie.
bool fleeingWithTheParty(const BattleState& state, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    return state.partyRunning && !battler.status.has(StatusId::ZOMBIE) && slot.isCharacter() &&
           !state.awayFromTheField.has(slot) && !battler.actsAsEnemy;
}

// The status timers that run out on their own: Reflect, Freeze and Sleep.
void tickStatusTimers(BattleState& state, BattlerSlot slot, bool stopRanOut) {
    Battler& battler = state.at(slot);
    auto step = [](std::uint8_t& timer) {
        if (timer == 0) {
            return false;
        }
        return --timer == 0;
    };
    StatusTimersExpired expired{.battler = slot, .stop = stopRanOut};
    expired.reflect = step(battler.reflectTimer);
    expired.freeze = step(battler.freezeTimer);
    expired.sleep = step(battler.sleepTimer);
    if (expired.stop || expired.reflect || expired.freeze || expired.sleep) {
        addImmediateAction(state, expired);
    }
}

// The character tries to get away: at once when nothing makes running hard; otherwise each try adds
// 1 plus a random amount below its run factor to its progress, and it gets away when the progress
// reaches the run difficulty.
void tryToEscape(BattleState& state, BattlerSlot slot) {
    if (slot.isMonster() || !state.partyRunning) {
        return;
    }
    Battler& battler = state.at(slot);
    if (state.partyLeft.has(slot) || battler.actsAsEnemy) {
        return;
    }
    if (state.runDifficulty == 0) {
        state.partyJustEscaped.add(slot);
        startPartyEscape(state);
        return;
    }
    battler.runProgress += 1 + state.rng.below(battler.runFactor);
    if (battler.runProgress >= state.runDifficulty) {
        state.partyJustEscaped.add(slot);
    }
}

// Poison, Regen and Sap, and Seize's drain, each become an action when their beat comes.
void queueDamageOverTime(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (battler.dotActionPending) {
        return;
    }
    if (battler.triggers.poisonDue) {
        battler.triggers.poisonDue = false;
        if (!battler.status.has(StatusId::POISON)) {
            return;
        }
        addCounterAction(state, slot, DamageOverTime{.kind = DamageOverTimeKind::Poison});
    } else if (battler.triggers.regenDue) {
        battler.triggers.regenDue = false;
        if (battler.status.has(StatusId::SAP) || battler.triggers.phantasm) {
            addCounterAction(state, slot, DamageOverTime{.kind = DamageOverTimeKind::Sap});
        } else if (battler.status.has(StatusId::REGEN)) {
            addCounterAction(state, slot, DamageOverTime{.kind = DamageOverTimeKind::Regen});
        } else {
            return;
        }
    } else if (battler.seizing) {
        addCounterAction(state, slot, SeizeDrain{.seized = *battler.seizing});
    } else {
        return;
    }
    battler.dotActionPending = true;
}

// One battler's turn of the timer jobs.
void runBattlerTimers(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (!battler.present) {
        return;
    }
    const unsigned sum = static_cast<unsigned>(battler.statusTimer) + battler.statusTimerStep;
    battler.statusTimer = static_cast<std::uint8_t>(sum);
    if (sum <= 0xFF) {
        return;
    }
    if (battler.stopTimer != 0) {
        if (--battler.stopTimer == 0) {
            tickStatusTimers(state, slot, true);
        }
        return;
    }
    if (battler.heldByStatus) {
        return;
    }
    if (battler.condemnedNumber >= 2) {
        if (--battler.condemnedNumber == 1) {
            condemnedCountRunsOut(state, slot);
        }
    }
    tryToEscape(state, slot);
    queueDamageOverTime(state, slot);
    tickStatusTimers(state, slot, false);
    switch (++battler.damageOverTimePhase % 8) {
        case 0:
        case 4:
            battler.triggers.regenDue = true;
            break;
        case 2:
            battler.triggers.poisonDue = true;
            break;
        default:
            break;
    }
}

// The battle-wide timer jobs.
void runBattleTimers(BattleState& state, int job) {
    switch (job) {
        case kRouletteJob:
            if (const auto target = std::exchange(state.rouletteTarget, std::nullopt)) {
                condemnedCountRunsOut(state, *target);
            }
            break;
        case kBattleCounterJob:
            ++state.battleCounter;
            for (int index = kBattlerSlots - 1; index >= 0; --index) {
                Battler& battler = state.battlers[static_cast<std::size_t>(index)];
                if (battler.present && !battler.status.hasAny(kOutOfTheFight)) {
                    ++battler.turnTimer;
                }
            }
            break;
        case kEscapeJob:
            if (!state.partyRunning) {
                break;
            }
            if (state.cannotRun) {
                queuePartyEscape(state);
            } else if ((state.counterPhase & kEscapeRetryPhases) == 0) {
                startPartyEscape(state);
            }
            break;
        default:
            break;
    }
}

void runTimerJob(BattleState& state) {
    const int job = state.counterPhase++ % kTimerJobs;
    if (job < kBattlerSlots) {
        runBattlerTimers(state, BattlerSlot::fromIndex(job));
    } else {
        runBattleTimers(state, job);
    }
}

// One time update: every gauge, advance wait and timer moves on once.
void updateBattleTime(BattleState& state, ActionExecutor& executor) {
    ++state.turnCounter;
    runTimerJob(state);
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        Battler& battler = state.at(slot);
        if (state.morphedBattler == slot) {
            executor.tickMorph(state, slot);
        }
        if (!battler.present || state.heldByZinger == slot) {
            continue;
        }
        if (battler.inBattleMenu || battler.heldByStatus || battler.actionQueued || battler.heldByControl) {
            continue;
        }
        if (battler.atbStopped && !fleeingWithTheParty(state, slot)) {
            advanceTheWait(state, slot);
        }
        if (!battler.gauge.isFull()) {
            fillTheGauge(state, slot);
        }
    }
}

}  // namespace

void tickBattleTime(BattleState& state, bool timeRunning, ActionExecutor& executor) {
    ++state.framesSinceTimeUpdate;
    if (!timeRunning) {
        return;
    }
    if (state.config.waitMode && state.timeStoppedByMenu) {
        state.framesSinceTimeUpdate = 0;
        return;
    }
    if (state.framesSinceTimeUpdate < kFramesPerTimeUpdate) {
        return;
    }
    state.framesSinceTimeUpdate = 0;
    updateBattleTime(state, executor);
}

void recalcAtbRate(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    std::uint8_t step = kNormalTimerStep;
    if (battler.status.has(StatusId::SLOW)) {
        step = kSlowTimerStep;
    } else if (battler.status.has(StatusId::HASTE)) {
        step = kHasteTimerStep;
    }
    battler.statusTimerStep = step;
    int speed = battler.speed + kSpeedBase;
    if (slot.isMonster()) {
        speed = (speed * state.config.monsterSpeedFactor()) >> 8;
    }
    battler.atbRate = static_cast<std::uint16_t>((speed * (step + step / 2)) >> 4);
}

}  // namespace ostinato::battle
