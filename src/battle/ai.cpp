#include "battle/ai.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "battle/actions.h"
#include "battle/ai_conditions.h"
#include "battle/ai_targets.h"
#include "battle/attack.h"
#include "battle/commands.h"
#include "data/ai_scripts.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/status_id.h"

namespace ostinato::battle {

namespace {

constexpr std::uint8_t kChoices = 3;

// A step that acts: anything but a condition, a wait, an endIf() or an end(). A restricted run
// skips the block such a step stands in, and the resume rule is tested on it.
bool acts(const AiStep& step) {
    return !step.isCondition() && step.as<ai::Wait>() == nullptr && step.as<ai::EndIf>() == nullptr &&
           step.as<ai::End>() == nullptr;
}

bool endsABlock(const AiStep& step) {
    return step.as<ai::EndIf>() != nullptr || step.as<ai::End>() != nullptr;
}

// A battle command takes its advance wait and its aim as it is queued; the commands only the battle
// queues, the AI's Roulette among them, take neither.
bool takesAdvanceWait(BattleCommandId command) {
    return static_cast<std::uint8_t>(command) < static_cast<std::uint8_t>(kRouletteCommand);
}

// The monster slots a step names, as a battler set; self() is the battler's own slot, none for a
// party member.
BattlerSet monstersNamed(AiMonsters monsters, BattlerSlot actor) {
    if (monsters.isSelf()) {
        return actor.isMonster() ? BattlerSet::of(actor) : BattlerSet{};
    }
    return BattlerSet{BattleSlotMask{}, monsters.slots};
}

// The statuses a battler answers an attack with that keep its counterattack part restricted:
// Sleep, Confuse, Berserk, Stop and Frozen; and Charm.
bool answersRestricted(const Battler& battler) {
    const StatusSet& statuses = battler.statusForCounter;
    return statuses.has(StatusId::SLEEP) || statuses.has(StatusId::CONFUSE) || statuses.has(StatusId::BERSERK) ||
           statuses.has(StatusId::STOP) || statuses.has(StatusId::FROZEN) || battler.charmedBy.has_value();
}

// One run of a script part.
class PartRun {
public:
    PartRun(BattleState& state, ActionExecutor& executor, BattlerSlot actor, AiResume& resume, BattlerSet targets,
            bool restricted)
        : state_(state),
          executor_(executor),
          actor_(actor),
          resume_(resume),
          restrictedFromTheStart_(restricted),
          run_{.targets = targets, .startingTargets = targets, .restricted = restricted} {}

    void read(const AiStep* top) {
        std::uint8_t resumeWhenSkipped = resume_.blocksSkipped;
        const AiStep* place = top;
        for (;;) {
            const AiStep* step = place;
            if (acts(*step) && skipped_ == resumeWhenSkipped) {
                if (resume_.at == nullptr) {
                    throw std::logic_error("a script part resumes with no place to resume from");
                }
                resumeWhenSkipped = kNoResume;
                step = resume_.at;
            }
            place = step + 1;
            bool blockFails = false;
            if (run_.restricted && acts(*step)) {
                blockFails = true;
            } else if (const AiCondition* condition = step->as<AiCondition>()) {
                blockFails = !aiCondition(state_, actor_, *condition, run_);
            } else if (step->as<ai::Wait>() != nullptr) {
                resume_ = AiResume{.blocksSkipped = skipped_, .at = place};
                return;
            } else if (endsABlock(*step)) {
                resume_.blocksSkipped = kNoResume;
                return;
            } else {
                std::visit([this](const auto& kind) { act(kind); }, step->kind);
            }
            if (blockFails) {
                // The block is skipped: the run's targets go back to what it started with, and the
                // run goes on after the block's endIf(), counting it, or ends at the part's end().
                run_.targets = run_.startingTargets;
                while (!endsABlock(*place)) {
                    ++place;
                }
                if (place->as<ai::End>() != nullptr) {
                    resume_.blocksSkipped = kNoResume;
                    return;
                }
                ++place;
                ++skipped_;
                run_.restricted = restrictedFromTheStart_;
            }
        }
    }

private:
    template <typename T>
    std::optional<T> drawn(const std::array<std::optional<T>, kChoices>& choices) {
        return choices[state_.rng.below(kChoices)];
    }

    void queue(Action action) { addActionOrCounter(state_, actor_, std::move(action)); }

    void queueCommand(BattleCommandId command, AttackId attack) {
        CommandAction action{.command = command, .attack = attack, .targets = run_.targets};
        if (state_.at(actor_).status.has(StatusId::CONFUSE)) {
            action.targets = {};
        }
        if (action.command == BattleCommandId::LORE && action.attack == AttackId::ROULETTE) {
            action.command = kRouletteCommand;
        }
        if (takesAdvanceWait(action.command)) {
            addAdvanceWait(state_, executor_, actor_, action);
        }
        queue(action);
    }

    void act(const ai::Attack& step) { queueCommand(commandForAttack(step.attack), step.attack); }

    void act(const ai::AttackOneOf& step) {
        if (const std::optional<AttackId> attack = drawn(step.choices)) {
            queueCommand(commandForAttack(*attack), *attack);
        }
    }

    void act(const ai::CommandOneOf& step) {
        if (const std::optional<BattleCommandId> command = drawn(step.choices)) {
            queueCommand(*command, AttackId{});
        }
    }

    void act(const ai::ItemOneOf& step) {
        if (const std::optional<ItemId> item = drawn<ItemId>({step.twoInThree, step.twoInThree, step.oneInThree})) {
            queueCommand(step.thrown ? BattleCommandId::THROW : BattleCommandId::ITEM, static_cast<AttackId>(*item));
        }
    }

    void act(const ai::SetTarget& step) { run_.targets = aiTargets(state_, actor_, step.target, true); }

    void act(const ai::ChangeBattle& step) {
        queue(FormationChange{.formation = step.formation,
                              .restoreMonsters = step.restoreMonsters,
                              .scrollBackground = step.scrollBackground,
                              .exit = step.exit});
    }

    void act(const ai::Dialog& step) { queue(MonsterDialogAction{.dialog = step.dialog}); }

    void act(const ai::ShowOrHideMonsters& step) {
        queue(MonsterVisibilityAction{.mode = step.mode, .monsters = monstersNamed(step.monsters, actor_), .type = step.type});
    }

    void act(const ai::BattleEvent& step) { queue(BattleEventAction{.event = step.event}); }

    void act(const ai::ChangeVariable& step) {
        queue(BattleVariableAction{.variable = step.variable, .change = step.change, .amount = step.amount});
    }

    void act(const ai::ChangeSwitch& step) { queue(BattleSwitchAction{.which = step.which, .change = step.change}); }

    void act(const ai::Animate& step) {
        queue(MonsterAnimationAction{.animation = step.animation,
                                     .monsters = BattlerSet{BattleSlotMask{}, step.monsters.slots},
                                     .soundEffect = step.sound,
                                     .pan = step.pan});
    }

    void act(const ai::Effect& step) {
        queue(AiEffectAction{.effect = step.effect, .target = step.target, .status = step.status});
    }

    // Conditions, waits and the ends are handled before a step acts.
    void act(const AiCondition&) {}
    void act(const ai::Wait&) {}
    void act(const ai::EndIf&) {}
    void act(const ai::End&) {}

    BattleState& state_;
    ActionExecutor& executor_;
    BattlerSlot actor_;
    AiResume& resume_;
    bool restrictedFromTheStart_;
    AiRun run_;
    std::uint8_t skipped_ = 0;
};

}  // namespace

void readyAi(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (!battler.aiScript) {
        return;
    }
    const AiScript& script = ostinato::aiScript(*battler.aiScript);
    battler.turnScript = script.turn;
    if (script.hasCounter()) {
        battler.counterScript = script.counter;
    }
    battler.hasCounterScript = !battler.counterScript.empty();
}

void runAiTurn(BattleState& state, ActionExecutor& executor, BattlerSlot actor) {
    Battler& battler = state.at(actor);
    if (battler.turnScript.empty()) {
        return;
    }
    PartRun(state, executor, actor, battler.turnResume, BattlerSet{}, false).read(battler.turnScript.data());
}

void runAiCounter(BattleState& state, ActionExecutor& executor, BattlerSlot actor, BattlerSet answering) {
    Battler& battler = state.at(actor);
    if (battler.counterScript.empty() || state.counterAiRanThisStep.has(actor)) {
        return;
    }
    state.counterAiRanThisStep.add(actor);
    bool restricted = false;
    if (state.diedAwaitingCounter.has(actor)) {
        // A battler answering its own death answers freely only if another attacked it this step.
        state.diedAwaitingCounter.remove(actor);
        const bool attacked = state.attackedThisStep.has(actor);
        state.attackedThisStep.remove(actor);
        restricted = !attacked || answersRestricted(battler);
    } else if (state.quickTarget && *state.quickTarget != actor) {
        restricted = true;
    } else {
        restricted = answersRestricted(battler);
    }
    PartRun(state, executor, actor, battler.counterResume, answering, restricted).read(battler.counterScript.data());
}

}  // namespace ostinato::battle
