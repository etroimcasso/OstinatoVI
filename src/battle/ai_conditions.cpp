#include "battle/ai_conditions.h"

#include <cstdint>
#include <optional>
#include <variant>

#include "ostinato/ai_target_selector.h"
#include "ostinato/status_id.h"

namespace ostinato::battle {

namespace {

// The battler a target test reads stats from when it names more than one: the one in the highest
// slot, the sixth monster first and the first party member last.
std::optional<BattlerSlot> lastIn(BattlerSet battlers) {
    for (int index = kBattlerSlots - 1; index >= 0; --index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (battlers.has(slot)) {
            return slot;
        }
    }
    return std::nullopt;
}

// A monster's own slot as a monster mask; nothing for a party member.
BattleSlotMask ownMonsterSlot(BattlerSlot actor) {
    return actor.isMonster() ? BattleSlotMask(static_cast<std::uint8_t>(1u << actor.position())) : BattleSlotMask();
}

// The monster slots that hold a monster of the formation, on the field or not.
BattleSlotMask monsterSlotsHeld(const BattleState& state) {
    BattleSlotMask held;
    for (int position = 0; position < kMonsterSlots; ++position) {
        if (state.at(BattlerSlot::monster(position)).monster) {
            held.bits = static_cast<std::uint8_t>(held.bits | (1u << position));
        }
    }
    return held;
}

// The conditions, one rule each, in the order the game numbers them; Never stands for the two
// numbers that never hold, and the party and monsters counts are one number taking either form.
struct ConditionRules {
    BattleState& state;
    BattlerSlot actor;
    AiRun& run;

    // The target test: the battlers `target` names, every side whole. When it names any, they
    // become the targets and the one in the highest slot is returned for its stats. A test on the
    // monster itself then takes the party's side out of the targets, which leaves a monster as its
    // own target and a party member's script with none.
    std::optional<BattlerSlot> testTarget(AiTarget target) {
        const BattlerSet named = aiTargets(state, actor, target, false);
        std::optional<BattlerSlot> battler;
        if (!named.empty()) {
            run.targets = named;
            battler = lastIn(named);
        }
        if (!target.isCharacter() && target.selector() == AiTargetSelector::SELF) {
            run.targets = run.targets.monstersOnly();
        }
        return battler;
    }

    template <typename T>
    bool hitBy(const std::optional<Hit<T>>& hit, T first, T second) {
        if (!hit || (hit->with != first && hit->with != second)) {
            return false;
        }
        run.targets = BattlerSet::of(hit->by);
        return true;
    }

    // One of the battlers the target names has `status`: one of them is drawn as the target. A
    // status from Dance on is looked for only among the battlers still fighting.
    bool anyHasStatus(AiTarget target, StatusId status) {
        if (!testTarget(target)) {
            return false;
        }
        BattlerSet holders = run.targets;
        if (status >= StatusId::DANCE) {
            holders = holders & state.alive;
        }
        for (int index = 0; index < kBattlerSlots; ++index) {
            const BattlerSlot slot = BattlerSlot::fromIndex(index);
            if (!state.at(slot).status.has(status)) {
                holders.remove(slot);
            }
        }
        const std::optional<BattlerSlot> picked = state.rng.pickFrom(holders);
        run.targets = picked ? BattlerSet::of(*picked) : BattlerSet{};
        return picked.has_value();
    }

    std::uint8_t value(AiVariable variable) const {
        return variable.isOwn() ? state.at(actor).aiVariable : state.battleVariables[variable.index()];
    }

    bool isSet(AiSwitch which) const {
        if (which.isBattleSwitch()) {
            return state.switches.isSet(which.number());
        }
        return (value(which.variable()) & (1u << which.bit())) != 0;
    }

    bool levelAtLeast(AiTarget target, std::uint8_t level) {
        const std::optional<BattlerSlot> battler = testTarget(target);
        return battler && state.at(*battler).level >= level;
    }

    // Every one of `monsters` is among `slots`; the monster's own slot for AiMonsters::self().
    bool allOf(AiMonsters monsters, BattleSlotMask slots) const {
        const std::uint8_t named = monsters.isSelf() ? ownMonsterSlot(actor).bits : monsters.slots.bits;
        return (slots.bits & named) == named;
    }

    bool operator()(const ai::Never&) { return false; }

    bool operator()(const ai::HitByCommand& c) { return hitBy(state.at(actor).lastHit.command, c.first, c.second); }

    bool operator()(const ai::HitByAttack& c) { return hitBy(state.at(actor).lastHit.spell, c.first, c.second); }

    bool operator()(const ai::HitByItem& c) { return hitBy(state.at(actor).lastHit.item, c.first, c.second); }

    bool operator()(const ai::HitByElement& c) {
        const std::optional<Hit<ElementSet>>& hit = state.at(actor).lastHit.element;
        if (!hit || (hit->with.bits & c.elements.bits) == 0) {
            return false;
        }
        run.targets = BattlerSet::of(hit->by);
        return true;
    }

    bool operator()(const ai::Damaged&) {
        const std::optional<BattlerSlot> by = state.at(actor).lastDamagedBy;
        if (!by) {
            return false;
        }
        run.targets = BattlerSet::of(*by);
        return true;
    }

    bool operator()(const ai::HpAtMost& c) {
        const std::optional<BattlerSlot> battler = testTarget(c.target);
        return battler && c.hp >= state.at(*battler).hp;
    }

    bool operator()(const ai::MpAtMost& c) {
        const std::optional<BattlerSlot> battler = testTarget(c.target);
        return battler && c.mp >= state.at(*battler).mp;
    }

    bool operator()(const ai::HasStatus& c) { return anyHasStatus(c.target, c.status); }

    bool operator()(const ai::NoStatus& c) { return !anyHasStatus(c.target, c.status); }

    bool operator()(const ai::HalfOwnTimerAtLeast& c) { return state.at(actor).turnTimer / 2 >= c.value; }

    bool operator()(const ai::VariableBelow& c) { return value(c.variable) < c.value; }

    bool operator()(const ai::VariableAtLeast& c) { return value(c.variable) >= c.value; }

    bool operator()(const ai::LevelBelow& c) { return !levelAtLeast(c.target, c.level); }

    bool operator()(const ai::LevelAtLeast& c) { return levelAtLeast(c.target, c.level); }

    bool operator()(const ai::OneKindOfMonster&) { return state.monsterNamesListed <= 1; }

    bool operator()(const ai::MonstersAlive& c) { return allOf(c.monsters, state.alive.monsters); }

    // A monster of the formation that is not alive: held in its slot, and not among the alive.
    bool operator()(const ai::MonstersDead& c) {
        run.restricted = false;
        const BattleSlotMask dead(static_cast<std::uint8_t>(monsterSlotsHeld(state).bits ^ state.alive.monsters.bits));
        return allOf(c.monsters, dead);
    }

    bool operator()(const ai::PartyAtLeast& c) { return state.allyCount >= c.count; }

    bool operator()(const ai::MonstersAtMost& c) { return c.count >= state.enemyCount; }

    bool operator()(const ai::SwitchSet& c) { return isSet(c.which); }

    bool operator()(const ai::SwitchClear& c) { return !isSet(c.which); }

    bool operator()(const ai::HalfBattleTimerAtLeast& c) { return state.battleCounter / 2 >= c.value; }

    bool operator()(const ai::TargetValid& c) { return testTarget(c.target).has_value(); }

    bool operator()(const ai::GauNotRecruited&) { return !state.gauRecruited; }

    bool operator()(const ai::InSlot& c) { return (ownMonsterSlot(actor).bits & c.slots.slots.bits) != 0; }

    bool operator()(const ai::WeakTo& c) {
        const std::optional<BattlerSlot> battler = testTarget(c.target);
        return battler && (state.at(*battler).elementsWeak.bits & c.elements.bits) != 0;
    }

    bool operator()(const ai::FormationIs& c) { return state.formation == c.formation; }

    bool operator()(const ai::Always&) {
        run.restricted = false;
        return true;
    }
};

}  // namespace

bool aiCondition(BattleState& state, BattlerSlot actor, const AiCondition& condition, AiRun& run) {
    return std::visit(ConditionRules{state, actor, run}, condition);
}

}  // namespace ostinato::battle
