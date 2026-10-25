// The battle loop: whose turn it is, the actions that run, what happens after each, and the
// battle's end.

#include <cstddef>
#include <optional>
#include <utility>
#include <variant>

#include "battle/actions.h"
#include "battle/battle_core.h"
#include "data/battle_commands.h"
#include "data/battle_tables.h"
#include "ostinato/battle_command_flags.h"

namespace ostinato::battle {

namespace {

// How long Palidor keeps a battler in the air, in advance-wait steps: a Jump's wait.
constexpr std::uint8_t kCarriedOffWait = 224;

// The chances, out of 256, that Gau turns up on the Veldt after a win, that Shadow walks off
// after one, and that a Black Belt answers a hit.
constexpr std::uint8_t kGauAppearsBelow = 160;
constexpr std::uint8_t kShadowLeavesBelow = 16;
constexpr std::uint8_t kBlackBeltBelow = 192;

constexpr int kAlliesForShadowOrGau = 2;

const StatusSet kFallen = StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE);
const StatusSet kKeepsTheMenuShut =
    StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE, StatusId::SLEEP, StatusId::CONFUSE,
                  StatusId::BERSERK, StatusId::DANCE, StatusId::HIDE, StatusId::RAGE);
const StatusSet kStopsABlackBelt =
    StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::MAGITEK, StatusId::ZOMBIE, StatusId::SLEEP,
                  StatusId::CONFUSE, StatusId::DANCE, StatusId::STOP, StatusId::FROZEN, StatusId::CHANT,
                  StatusId::HIDE);
const StatusSet kClearedOnJoining =
    StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::IMP, StatusId::ZOMBIE, StatusId::HIDE);

// Every battler from the last slot to the first: the order the original visits them in.
template <typename Visit>
void forEachSlotLastFirst(Visit visit) {
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        visit(BattlerSlot::fromIndex(index));
    }
}

bool held(const Battler& battler) {
    return battler.heldByStatus || battler.heldByControl;
}

std::optional<BattlerSlot> partyMember(const BattleState& state, CharacterId actor) {
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (state.at(slot).actor == actor) {
            return slot;
        }
    }
    return std::nullopt;
}

void waitForScript(BattleState& state, std::vector<BattleScriptCommand> script, ScriptWait wait) {
    state.requests.push_back(RunBattleScript{.commands = std::move(script)});
    state.waitingForScript = wait;
}

// The battler that acted goes back to its place, and the loop moves on once that has played.
void stepBack(BattleState& state) {
    waitForScript(state, {StepBack{}}, ScriptWait{.resume = ScriptResume::NextStep});
}

// --- the moment before an action runs -------------------------------------------------------

// Mimic becomes the last command a character used: its last Jump, or its last command and, for
// X-Magic, a second spell added behind it.
void replaceMimic(BattleState& state, BattlerSlot slot, CommandAction& entry) {
    if (state.mimic.replayJump) {
        entry.command = BattleCommandId::JUMP;
        entry.attack = AttackId{};
        entry.targets = state.mimic.jumpTargets;
        return;
    }
    entry.command = state.mimic.last.command;
    entry.attack = state.mimic.last.attack;
    entry.targets = state.mimic.last.targets;
    if (state.mimic.secondSpell) {
        state.mimicking = true;
        addAction(state, slot, *state.mimic.secondSpell);
    }
}

// The command as it will run. A command whose user's statuses changed since it was chosen loses
// its targets, to be aimed again. An Imp using a command an Imp cannot use fights instead, for no
// MP, at one target chosen at random from those it had, never one on its own side unless that one
// fights for the other side.
Action prepareAction(BattleState& state, std::optional<BattlerSlot> actor, Action action, bool reaimIfChanged) {
    auto* command = std::get_if<CommandAction>(&action);
    if (command == nullptr || !actor) {
        return action;
    }
    if (reaimIfChanged && command->command != BattleCommandId::MAGITEK && state.statusChanged.has(*actor)) {
        state.statusChanged.remove(*actor);
        command->targets = {};
    }
    if (!state.at(*actor).status.has(StatusId::IMP)) {
        return action;
    }
    if (battleCommandProperties(command->command).flags.has(BattleCommandFlags::IMP)) {
        return action;
    }
    command->mpCost = 0;
    BattlerSet ownSideTurned;
    for (int index = 0; index < kBattlerSlots; ++index) {
        const BattlerSlot other = BattlerSlot::fromIndex(index);
        if (other.isCharacter() == actor->isCharacter() && state.at(other).actsAsEnemy) {
            ownSideTurned.add(other);
        }
    }
    const BattlerSet ownSide = actor->isCharacter() ? BattlerSet::allCharacters() : BattlerSet::allMonsters();
    const BattlerSet targets = command->targets.without(ownSide) | (command->targets & ownSideTurned);
    command->command = BattleCommandId::FIGHT;
    command->attack = AttackId{};
    const auto target = state.rng.pickFrom(targets);
    command->targets = target ? BattlerSet::of(*target) : BattlerSet{};
    return action;
}

// --- after every command ----------------------------------------------------------------------

void releaseLinksAndQuick(BattleState& state, ActionExecutor& executor, BattlerSlot slot) {
    executor.releaseLinks(state, slot);
    if (state.quickTarget == slot) {
        state.quickTarget.reset();
        state.quickTurnsLeft.reset();
    }
}

// A battler that died while jumping or held in Seize stays alive until it lands, and dies then.
void survivesInTheAir(Battler& battler) {
    battler.status.clear(StatusId::DEAD);
    battler.pendingUpdates.removeAllActions = false;
}

void checkTheFallen(BattleState& state, ActionExecutor& executor) {
    forEachSlotLastFirst([&](BattlerSlot slot) {
        Battler& battler = state.at(slot);
        if (!battler.present || state.joiningAfterAction.has(slot)) {
            return;
        }
        executor.checkControl(state, slot);
        if (battler.status.has(StatusId::DEAD) || battler.status.has(StatusId::ZOMBIE)) {
            battler.hp = 0;
        }
        if (battler.status.hasAny(kFallen)) {
            if (slot.isMonster()) {
                state.monstersFallen.add(slot);
            }
            releaseLinksAndQuick(state, executor, slot);
        }
        if (!battler.status.has(StatusId::DEAD)) {
            return;
        }
        if (slot.isCharacter() && battler.actor == CharacterId::BANON) {
            state.endOfBattleEvent = EndOfBattleEvent::BanonFell;
        }
        if (state.awayFromTheField.has(slot)) {
            survivesInTheAir(battler);
            battler.airAnchored = true;
        }
        if (battler.status.has(StatusId::ZOMBIE)) {
            survivesInTheAir(battler);
        }
        if (battler.status.has(StatusId::DEAD) && battler.status.has(StatusId::RERAISE)) {
            battler.status.clear(StatusId::RERAISE);
            state.monstersStanding.remove(slot);
            addImmediateAction(state, SpellWithoutCaster{.spell = AttackId::LIFE, .target = slot});
        }
    });
    forEachSlotLastFirst([&](BattlerSlot slot) { executor.clearControl(state, slot); });
}

// Who answers the command just run: a battler hit by another may counter it from its AI script,
// or with Retort, Interceptor or a Black Belt.
void checkCounterattacks(BattleState& state) {
    forEachSlotLastFirst([&](BattlerSlot slot) {
        Battler& battler = state.at(slot);
        if (!battler.present || !state.scratch.countersEnabled) {
            return;
        }
        BattlerSet answerTo;
        if (battler.retaliationPending && battler.lastAttacker && *battler.lastAttacker != slot) {
            answerTo = BattlerSet::of(*battler.lastAttacker);
            state.attackedThisStep.add(slot);
        }
        if (!state.diedAwaitingCounter.has(slot) && (state.counterattackRunning || answerTo.empty())) {
            return;
        }
        if (battler.hasCounterScript && battler.counterActions.empty()) {
            addCounterAction(state, slot, AiTurn{});
            return;
        }
        if (slot.isMonster() || !state.scratch.physical) {
            return;
        }
        if (battler.triggers.retort) {
            addCounterAction(state, slot,
                             CommandAction{.command = BattleCommandId::BUSHIDO, .attack = AttackId::RETORT,
                                           .targets = answerTo});
            return;
        }
        if (answerTo.monstersOnly().empty()) {
            return;
        }
        if (state.scratch.interceptorGuarding == slot) {
            const std::uint8_t roll = state.rng.next();
            if ((roll & 1) != 0) {
                const AttackId attack = (roll & 2) != 0 ? AttackId::WILD_FANG : AttackId::TAKEDOWN;
                addCounterAction(state, slot,
                                 CommandAction{.command = BattleCommandId::MAGIC, .attack = attack,
                                               .targets = answerTo});
                return;
            }
        }
        if (!state.scratch.blackBeltEligible.has(slot) || !battler.blackBelt) {
            return;
        }
        if (state.rng.next() >= kBlackBeltBelow) {
            return;
        }
        if (battler.status.hasAny(kStopsABlackBelt)) {
            return;
        }
        addCounterAction(state, slot, CommandAction{.command = BattleCommandId::FIGHT, .targets = answerTo});
    });
}

// A monster that died leaves the field at once unless it still has a counterattack to make.
void markFallenMonsters(BattleState& state) {
    for (int position = kMonsterSlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::monster(position);
        if (!state.monstersFallen.has(slot)) {
            continue;
        }
        const Battler& battler = state.at(slot);
        if (battler.status.has(StatusId::HIDE) || battler.triggers.piranha || battler.counterActions.empty()) {
            state.monstersStanding.remove(slot);
        }
    }
}

void joinTheField(BattleState& state) {
    forEachSlotLastFirst([&](BattlerSlot slot) {
        if (!state.joiningAfterAction.has(slot)) {
            return;
        }
        state.joiningAfterAction.remove(slot);
        Battler& battler = state.at(slot);
        if (slot.isMonster()) {
            state.monstersStanding.add(slot);
        }
        battler.present = true;
        battler.status = battler.status.without(kClearedOnJoining);
        if (battler.firstStrike && battler.present) {
            battler.atbStopped = true;
            battler.gauge.setMeter(0);
            battler.advanceWaitCounter = static_cast<std::uint16_t>(0xFF00 | (battler.advanceWaitCounter & 0x00FF));
            joinAdvanceWaitLine(state, slot);
        }
    });
}

void leaveTheField(BattleState& state, ActionExecutor& executor) {
    forEachSlotLastFirst([&](BattlerSlot slot) {
        if (!state.leavingAfterAction.has(slot)) {
            return;
        }
        state.leavingAfterAction.remove(slot);
        if (slot.isMonster()) {
            state.monstersStanding.remove(slot);
        }
        Battler& battler = state.at(slot);
        battler.present = false;
        battler.status.set(StatusId::HIDE);
        releaseLinksAndQuick(state, executor, slot);
    });
}

// A battler whose menu cannot open: its menu closes, and if the menu was going to open, the
// battler acts on its own instead.
void cancelMenu(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    const bool leaving = slot.isCharacter() && state.leavingAfterAction.has(slot);
    if (!battler.status.has(StatusId::HIDE) && !leaving && battler.menuMayOpen) {
        battler.menuMayOpen = false;
        joinAdvanceWaitLine(state, slot);
    }
    battler.takesMenuInput = false;
    battler.inBattleMenu = false;
    if (slot.isCharacter()) {
        state.requests.push_back(CloseBattleMenu{.character = slot});
    }
}

void emptyGaugeAndCancel(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    battler.inBattleMenu = false;
    battler.atbStopped = false;
    battler.actionQueued = false;
    battler.emptyGaugeAfterAction = false;
    battler.gauge.empty();
    cancelMenu(state, slot);
}

// Palidor carries the battler off: its gauge stops, its action leaves the line, and it comes down
// after a Jump's wait.
void carryOff(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    state.awayFromTheField.add(slot);
    battler.atbStopped = true;
    battler.inBattleMenu = false;
    battler.actionQueued = false;
    battler.heldByControl = false;
    state.actionQueue.erase(slot);
    battler.mayActAfterJump = false;
    battler.advanceWaitCounter &= 0x00FF;
    battler.advanceWait = kCarriedOffWait;
}

void startCondemnedCount(BattleState& state, BattlerSlot slot) {
    const int level = state.condemnedLevel;
    const int rolled = level + state.rng.below(state.condemnedLevel);
    state.at(slot).condemnedNumber = static_cast<std::uint8_t>(20 + std::max(0, 60 - rolled));
}

// A party member whose menu may open opens it if nothing stops it.
void checkMenuOpens(BattleState& state, BattlerSlot slot) {
    if (slot.isMonster()) {
        return;
    }
    Battler& battler = state.at(slot);
    if (battler.actor == CharacterId::UMARO || battler.aiTakesTurns || state.colosseum) {
        return;
    }
    battler.takesMenuInput = true;
    if (state.quickTarget == slot) {
        battler.atbStopped = true;
        battler.menuMayOpen = true;
    }
    if (state.leavingAfterAction.has(slot) || battler.seizedBy || battler.charmedBy ||
        battler.status.hasAny(kKeepsTheMenuShut)) {
        cancelMenu(state, slot);
        return;
    }
    if (!battler.menuMayOpen || !battler.actions.empty()) {
        return;
    }
    battler.atbStopped = true;
    openMenuOrJoinLine(state, slot);
}

// Whether the battler can take a turn, and if it can, its held-back actions go back in line.
void checkHeld(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    battler.heldByStatus = true;
    battler.heldByControl = true;
    if (state.quickTarget && *state.quickTarget != slot) {
        return;
    }
    if (battler.status.has(StatusId::DEAD) || battler.status.has(StatusId::PETRIFY) ||
        battler.status.has(StatusId::STOP)) {
        return;
    }
    battler.heldByStatus = false;
    if (battler.controlledBy || battler.status.has(StatusId::SLEEP) || battler.status.has(StatusId::FROZEN) ||
        battler.seizedBy) {
        return;
    }
    battler.heldByControl = false;
    if (!std::exchange(battler.requeuePending, false) || battler.actions.empty()) {
        return;
    }
    if (battler.skipAdvanceWait) {
        state.actionQueue.push(slot);
    } else {
        joinAdvanceWaitLine(state, slot);
    }
}

void settleAfterAction(BattleState& state, ActionExecutor& executor) {
    forEachSlotLastFirst([&](BattlerSlot slot) {
        Battler& battler = state.at(slot);
        if (!battler.present) {
            return;
        }
        battler.retaliationPending = false;
        if (battler.status.has(StatusId::DEAD) || battler.emptyGaugeAfterAction) {
            emptyGaugeAndCancel(state, slot);
        }
        const PendingUpdates pending = std::exchange(battler.pendingUpdates, PendingUpdates{});
        if (pending.leaveTheField) carryOff(state, slot);
        if (pending.enabledMagic) executor.refreshEnabledMagic(state, slot);
        if (pending.removeAllActions) {
            removeAllActions(state, slot);
            battler.requeuePending = true;
        }
        if (pending.startCondemned) startCondemnedCount(state, slot);
        if (pending.stopCondemned) battler.condemnedNumber = 0;
        if (pending.commandList && slot.isCharacter()) executor.refreshCommandList(state, slot);
        if (pending.speed) recalcAtbRate(state, slot);
        if (pending.morph) executor.refreshMorph(state, slot);
        checkMenuOpens(state, slot);
        checkHeld(state, slot);
        if (held(battler)) {
            battler.defending = false;
        }
        executor.checkNearFatal(state, slot);
    });
}

// Run a command and wait for its script. Everything after the script resumes in finishCommand().
void runCommand(BattleState& state, ActionExecutor& executor, std::optional<BattlerSlot> actor, const Action& action,
                ScriptResume resume) {
    state.scratch = ActionScratch{};
    std::optional<CommandAction> command;
    if (const auto* asCommand = std::get_if<CommandAction>(&action)) {
        command = *asCommand;
        state.scratch.finalTargets = asCommand->targets;
    }
    std::vector<BattleScriptCommand> script = executor.execute(state, actor, action);
    checkTheFallen(state, executor);
    checkCounterattacks(state);
    markFallenMonsters(state);
    waitForScript(state, std::move(script), ScriptWait{.resume = resume, .actor = actor, .command = command});
}

// Mimic remembers a character's command, if it is one Mimic can copy.
void rememberForMimic(BattleState& state, BattlerSlot actor, const std::optional<CommandAction>& command) {
    if (actor.isMonster() || !command) {
        return;
    }
    if (!battleCommandProperties(command->command).flags.has(BattleCommandFlags::MIMIC)) {
        return;
    }
    CommandAction remembered{.command = command->command, .attack = command->attack,
                             .targets = state.scratch.finalTargets};
    state.mimic.replayJump = false;
    if (command->command == BattleCommandId::X_MAGIC) {
        state.mimic.secondSpell = remembered;
    } else {
        state.mimic.secondSpell.reset();
        state.mimic.last = remembered;
    }
}

// --- a battler's turn ---------------------------------------------------------------------------

void endTurn(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    state.monsterListChanged = true;
    state.actorStillActing = true;
    const bool endNow = std::exchange(state.endTurnAfterAction, false);
    if (!endNow && !battler.actions.empty()) {
        state.actingBattler = slot;
        return;
    }
    if (!battler.atbStopped) {
        battler.gauge.stepBackAfterTurn();
    }
    battler.advanceWait.reset();
    battler.advanceWaitCounter &= 0x00FF;
    state.actorStillActing = false;
    stepBack(state);
}

void endCounter(BattleState& state, BattlerSlot slot) {
    if (!state.at(slot).counterActions.empty()) {
        state.counteringBattler = slot;
        return;
    }
    if (state.actorStillActing) {
        return;
    }
    stepBack(state);
}

// The battler at the front of the action line takes its turn.
void takeTurn(BattleState& state, ActionExecutor& executor, BattlerSlot slot) {
    state.actingBattler.reset();
    Battler& battler = state.at(slot);
    Action action = NoAction{};
    while (!battler.actions.empty()) {
        if (auto* mimic = std::get_if<CommandAction>(&battler.actions.front());
            mimic != nullptr && mimic->command == BattleCommandId::MIMIC) {
            replaceMimic(state, slot, *mimic);
        }
        // The turn's AI and Jump rules go by the command as chosen, before an Imp's is changed.
        const Action chosen = battler.actions.front();
        action = prepareAction(state, slot, chosen, true);
        if (std::holds_alternative<AiTurn>(chosen)) {
            removeAction(state, slot);
            executor.runAi(state, slot);
            action = NoAction{};
            continue;
        }
        if (const auto* jump = std::get_if<CommandAction>(&chosen);
            jump != nullptr && jump->command == BattleCommandId::JUMP) {
            if (!state.awayFromTheField.has(slot)) {
                state.awayFromTheField.add(slot);
                battler.atbStopped = true;
                battler.actionQueued = false;
                battler.advanceWaitCounter &= 0x00FF;
                joinAdvanceWaitLine(state, slot);
                return;
            }
            state.awayFromTheField.remove(slot);
        }
        if (battler.actions.size() == 1) {
            state.menusDisabled = false;
            if (state.quickTarget == slot && state.quickTurnsLeft && --*state.quickTurnsLeft == 0) {
                state.quickTarget.reset();
            }
        }
        if (held(battler)) {
            battler.requeuePending = true;
            return;
        }
        battler.actions.pop_front();
        break;
    }
    battler.atbStopped = false;
    battler.actionQueued = false;
    battler.heldByControl = true;
    if (battler.present) {
        battler.pendingUpdates.speed = true;
        battler.mayActAfterJump = true;
        runCommand(state, executor, slot, action, ScriptResume::FinishAction);
        return;
    }
    endTurn(state, slot);
}

// The battler at the front of the counterattack line makes its counterattack.
void counterattack(BattleState& state, ActionExecutor& executor, BattlerSlot slot) {
    state.counteringBattler.reset();
    state.counterattackRunning = true;
    Battler& battler = state.at(slot);
    while (!battler.counterActions.empty()) {
        Action action = prepareAction(state, slot, battler.counterActions.front(), false);
        removeCounterAction(state, slot);
        if (std::holds_alternative<AiTurn>(action)) {
            executor.runCounterAi(state, slot);
            continue;
        }
        bool runs = true;
        if (std::holds_alternative<CommandAction>(action) && slot.isCharacter() && !battler.actsAsEnemy) {
            runs = !state.partyLeft.has(slot) && state.enemyCount != 0 && !held(battler);
        } else if (std::holds_alternative<CommandAction>(action) && slot.isCharacter()) {
            runs = !state.partyLeft.has(slot);
        }
        if (runs) {
            battler.pendingUpdates.speed = true;
            runCommand(state, executor, slot, action, ScriptResume::FinishCounter);
            return;
        }
        endCounter(state, slot);
        return;
    }
}

// The first action of the line that runs before anything else.
void runImmediate(BattleState& state, ActionExecutor& executor) {
    Action action = std::move(state.immediateActions.front());
    state.immediateActions.pop_front();
    state.counterattackRunning = true;
    runCommand(state, executor, std::nullopt, action, ScriptResume::FinishImmediate);
}

// The battler's action is set up when its gauge fills: an automatic choice, a re-timed wait for
// what it already chose, or its AI's turn.
void prepareTurn(BattleState& state, ActionExecutor& executor, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (!battler.advanceWait) {
        battler.advanceWait = 0;
    }
    battler.defending = false;
    battler.triggers.runic = false;
    battler.triggers.retort = false;
    if (slot.isMonster()) {
        if (battler.actions.empty()) {
            addAction(state, slot, AiTurn{});
            return;
        }
    } else {
        state.statusChanged.remove(slot);
        if (battler.aiTakesTurns) {
            executor.runAi(state, slot);
            return;
        }
        if (battler.actions.empty()) {
            executor.chooseAutomaticAction(state, slot);
            return;
        }
    }
    for (Action& action : battler.actions) {
        if (auto* command = std::get_if<CommandAction>(&action)) {
            addAdvanceWait(state, executor, slot, *command);
        }
    }
}

// The battler at the front of the advance-wait line starts its advance wait, and a character steps
// forward for its command.
void startAdvanceWait(BattleState& state, ActionExecutor& executor, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    battler.requeuePending = true;
    if (held(battler)) {
        return;
    }
    battler.requeuePending = false;
    battler.skipAdvanceWait = true;
    prepareTurn(state, executor, slot);
    if (battler.actions.empty()) {
        return;
    }
    const auto* command = std::get_if<CommandAction>(&battler.actions.front());
    if (command == nullptr) {
        return;
    }
    if (command->command == BattleCommandId::JUMP) {
        if (!battler.mayActAfterJump) {
            return;
        }
        if (slot.isCharacter()) {
            state.mimic.replayJump = true;
            state.mimic.jumpTargets = command->targets;
        }
        state.awayFromTheField.add(slot);
        battler.status.set(StatusId::HIDE);
    } else if (slot.isMonster()) {
        return;
    }
    waitForScript(state,
                  {ReadyStance{.actor = slot, .command = command->command, .targets = command->targets}},
                  ScriptWait{.resume = ScriptResume::NextStep});
}

// Gau has turned up on the Veldt: every living party member on the ground drops what it had chosen.
void dropChoicesForGau(BattleState& state) {
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (state.alive.has(slot) && !state.awayFromTheField.has(slot)) {
            removeAllActions(state, slot);
        }
    }
}

// --- the battle's end --------------------------------------------------------------------------

enum class EndCheck {
    Continue,
    Ended,
    Interrupted,  // something happens first; the loop step stops here
};

EndCheck end(BattleState& state, BattleOutcome outcome) {
    state.outcome = outcome;
    return EndCheck::Ended;
}

EndCheck endByEvent(BattleState& state, EndOfBattleEvent event) {
    switch (event) {
        case EndOfBattleEvent::RanOutOfTime:
            return end(state, BattleEnded{.reason = BattleEndReason::RanOutOfTime});
        case EndOfBattleEvent::GauLeapt:
            return end(state, BattleEnded{.reason = BattleEndReason::GauLeapt});
        case EndOfBattleEvent::BanonFell:
            return end(state, BattleLost{.message = kMessageBanonFell});
        case EndOfBattleEvent::EndNow:
            return end(state, BattleEnded{.reason = BattleEndReason::EndNow});
        case EndOfBattleEvent::RagesLearned:
            return end(state, BattleEnded{.reason = BattleEndReason::RagesLearned});
        case EndOfBattleEvent::FinalBattleNextStage:
            return end(state, BattleEnded{.reason = BattleEndReason::FinalBattleNextStage});
    }
    return EndCheck::Continue;
}

// The final battle's stages follow one another: each won stage but the last moves on to the next.
std::optional<FormationId> nextFinalBattleStage(const BattleState& state) {
    for (std::size_t stage = kFinalBattleFormations.size() - 1; stage-- > 0;) {
        if (kFinalBattleFormations[stage] == state.formation) {
            return kFinalBattleFormations[stage + 1];
        }
    }
    return std::nullopt;
}

EndCheck checkBattleEnd(BattleState& state) {
    if (!state.finalBattleChain && state.endOfBattleEvent) {
        return endByEvent(state, *state.endOfBattleEvent);
    }
    if (state.eventTimerExpired) {
        return end(state, BattleEnded{.reason = BattleEndReason::TimerExpired});
    }
    if (state.battleMayNotEnd) {
        return EndCheck::Continue;
    }
    if (state.alive.charactersOnly().empty()) {
        if (!state.engulfed.empty() && state.engulfed == state.partyPresentAtStart) {
            return end(state, BattleEnded{.reason = BattleEndReason::EatenByZoneEater});
        }
        if (!state.partyLeft.empty()) {
            return endByEvent(state, EndOfBattleEvent::RanOutOfTime);
        }
        return end(state, BattleLost{.message = state.colosseum ? std::nullopt
                                                                 : std::optional<std::uint8_t>(kMessageAnnihilated)});
    }
    if (state.enemyCount != 0) {
        return EndCheck::Continue;
    }
    if (state.finalBattleChain) {
        if (const auto next = nextFinalBattleStage(state)) {
            return end(state, BattleEnded{.reason = BattleEndReason::FinalBattleNextStage, .nextFormation = next});
        }
    }
    if (const auto gau = partyMember(state, CharacterId::GAU); gau && std::exchange(state.gauMayAppear, false)) {
        if (state.rng.next() < kGauAppearsBelow && (state.gauRecruited || state.allyCount >= kAlliesForShadowOrGau)) {
            state.joiningAfterAction.add(*gau);
            state.at(*gau).actsAsEnemy = true;
            state.gauJoining = true;
            addImmediateAction(state, BattleEventAction{.event = kGauJoinsEvent});
            return EndCheck::Interrupted;
        }
    }
    if (const auto shadow = partyMember(state, CharacterId::SHADOW)) {
        if (state.rng.next() < kShadowLeavesBelow && state.battleType == BattleType::Front &&
            state.allyCount >= kAlliesForShadowOrGau && !state.at(*shadow).status.hasAny(kFallen) &&
            !state.shadowStays && state.shadowAvailable) {
            state.shadowAvailable = false;
            state.shadowLeftParty = true;
            state.at(*shadow).present = false;
            state.noVictoryFanfare = true;
            addImmediateAction(state, BattleEventAction{.event = kShadowLeavesEvent});
            return EndCheck::Interrupted;
        }
    }
    return end(state, BattleWon{});
}

// --- the loop -----------------------------------------------------------------------------------

// What runs as one loop step after the frame boundary. Each branch that starts something ends
// the step.
void runLoopStep(BattleState& state, ActionExecutor& executor) {
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (state.menusToRefresh.has(slot)) {
            state.menusToRefresh.remove(slot);
            state.requests.push_back(RefreshBattleMenu{.character = slot});
        }
    }
    if (!state.immediateActions.empty()) {
        runImmediate(state, executor);
        return;
    }
    if (std::exchange(state.gauJoining, false)) {
        dropChoicesForGau(state);
        return;
    }
    if (state.counteringBattler) {
        counterattack(state, executor, *state.counteringBattler);
        return;
    }
    if (const auto slot = state.counterQueue.pop()) {
        counterattack(state, executor, *slot);
        return;
    }
    if (const BattlerSet newlyFallen = state.monstersFallen & state.monstersStanding; !newlyFallen.empty()) {
        state.monstersStanding = state.monstersStanding.without(newlyFallen);
        return;
    }
    if (std::exchange(state.monsterListChanged, false)) {
        refreshRunDifficulty(state);
        state.requests.push_back(RefreshMonsterNames{});
    }
    state.runActionQueued = false;
    if (checkBattleEnd(state) != EndCheck::Continue) {
        return;
    }
    state.attackedThisStep = {};
    state.counterAiRanThisStep = {};
    state.counterattackRunning = false;
    if (const auto slot = state.advanceWaitQueue.pop()) {
        startAdvanceWait(state, executor, *slot);
        return;
    }
    if (state.actingBattler) {
        takeTurn(state, executor, *state.actingBattler);
        return;
    }
    if (const auto slot = state.actionQueue.pop()) {
        takeTurn(state, executor, *slot);
        return;
    }
    state.battleMayNotEnd = false;
}

// The rest of a command, once its script has played.
void finishCommand(BattleState& state, ActionExecutor& executor, const ScriptWait& wait) {
    refreshAliveSets(state);
    joinTheField(state);
    settleAfterAction(state, executor);
    leaveTheField(state, executor);
    switch (wait.resume) {
        case ScriptResume::FinishAction:
            rememberForMimic(state, *wait.actor, wait.command);
            endTurn(state, *wait.actor);
            break;
        case ScriptResume::FinishCounter:
            rememberForMimic(state, *wait.actor, wait.command);
            endCounter(state, *wait.actor);
            break;
        case ScriptResume::FinishImmediate:
        case ScriptResume::NextStep:
            break;
    }
}

// The loop comes back round: the random-number position moves on, and a Quick count that ran out
// clears.
void beginNextStep(BattleState& state) {
    state.rng.advance();
    if (state.quickTurnsLeft == 0) {
        state.quickTurnsLeft.reset();
    }
}

}  // namespace

std::optional<BattleOutcome> stepBattle(BattleState& state, ActionExecutor& executor) {
    if (state.outcome) {
        return state.outcome;
    }
    if (const auto wait = std::exchange(state.waitingForScript, std::nullopt)) {
        if (wait->resume != ScriptResume::NextStep) {
            finishCommand(state, executor, *wait);
        }
    } else {
        runLoopStep(state, executor);
    }
    if (!state.waitingForScript && !state.outcome) {
        beginNextStep(state);
    }
    return state.outcome;
}

void refreshAliveSets(BattleState& state) {
    state.joiningAfterAction = state.joiningAfterAction.without(state.leavingAfterAction);
    state.targetableOrPresent = state.joiningAfterAction;
    state.alive = {};
    state.aliveEnemyCharacters = {};
    forEachSlotLastFirst([&](BattlerSlot slot) {
        const Battler& battler = state.at(slot);
        if (state.leavingAfterAction.has(slot)) {
            return;
        }
        if (!state.joiningAfterAction.has(slot)) {
            if (!battler.present) {
                return;
            }
            state.targetableOrPresent.add(slot);
            if (slot.isMonster() && state.monstersFallen.has(slot)) {
                return;
            }
            if (battler.status.hasAny(kFallen)) {
                return;
            }
        }
        if (!state.countedInBattle.has(slot)) {
            return;
        }
        if (slot.isCharacter() && battler.actsAsEnemy) {
            state.aliveEnemyCharacters.add(slot);
        } else {
            state.alive.add(slot);
        }
    });
    state.allyCount = state.alive.charactersOnly().count();
    state.enemyCount = state.alive.monstersOnly().count() + state.aliveEnemyCharacters.count();
}

void refreshRunDifficulty(BattleState& state) {
    state.cannotRun = false;
    state.escapeBlockedByMonster = false;
    if (state.battleType == BattleType::Pincer) {
        const auto standing = [&](BattleSlotMask side) {
            return (side.bits & state.monstersStanding.monsters.bits) != 0;
        };
        if (standing(state.pincerSides[0]) && standing(state.pincerSides[1])) {
            state.cannotRun = true;
        }
    }
    int difficulty = 0;
    for (int position = 0; position < kMonsterSlots; ++position) {
        const BattlerSlot slot = BattlerSlot::monster(position);
        const Battler& battler = state.at(slot);
        if (!battler.present || state.monstersFallen.has(slot) || !state.countedInBattle.has(slot) ||
            battler.status.hasAny(kFallen)) {
            continue;
        }
        if (battler.preventsEscape) {
            state.cannotRun = true;
            state.escapeBlockedByMonster = true;
        }
        difficulty += battler.harderToRunFrom ? 6 : 2;
    }
    if (state.battleType == BattleType::Side || state.preemptive) {
        difficulty = 0;
    }
    state.runDifficulty = static_cast<std::uint8_t>(difficulty);
    if (!state.aliveEnemyCharacters.empty()) {
        state.cannotRun = true;
    }
}

}  // namespace ostinato::battle
