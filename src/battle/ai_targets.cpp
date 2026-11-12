#include "battle/ai_targets.h"

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "battle/targeting.h"
#include "ostinato/ai_target_selector.h"
#include "ostinato/status_id.h"

namespace ostinato::battle {

namespace {

// The battlers in the battle: every party slot that holds a party member and every monster slot
// that holds a monster, on the field or not.
BattlerSet battlersInTheBattle(const BattleState& state) {
    BattlerSet battlers;
    for (int index = 0; index < kBattlerSlots; ++index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        const Battler& battler = state.at(slot);
        if (slot.isCharacter() ? battler.actor.has_value() : battler.monster.has_value()) {
            battlers.add(slot);
        }
    }
    return battlers;
}

// Every battler, in the battle or not, whose statuses hold `status`.
BattlerSet withStatus(const BattleState& state, StatusId status) {
    BattlerSet battlers;
    for (int index = 0; index < kBattlerSlots; ++index) {
        const BattlerSlot slot = BattlerSlot::fromIndex(index);
        if (state.at(slot).status.has(status)) {
            battlers.add(slot);
        }
    }
    return battlers;
}

BattlerSet pickOne(BattleState& state, BattlerSet battlers) {
    const std::optional<BattlerSlot> picked = state.rng.pickFrom(battlers);
    return picked ? BattlerSet::of(*picked) : BattlerSet{};
}

// What a selector works from: the battle, the battler whose script it is, and the side as the
// filter left it.
struct Aim {
    BattleState& state;
    BattlerSlot actor;
    BattlerSet battlers;

    BattlerSet others() const { return battlers.without(BattlerSet::of(actor)); }
};

using SelectorRule = BattlerSet (*)(Aim&);

struct SelectorRow {
    AiTargetSelector selector;
    SelectorRule rule;
};

BattlerSet partySlot(const Aim& aim, int position) {
    const BattlerSlot slot = BattlerSlot::character(position);
    return aim.state.at(slot).present ? BattlerSet::of(slot) : BattlerSet{};
}

// The selectors from SELF on, in their order.
constexpr std::array<SelectorRow, 24> kSelectorRules{{
    {AiTargetSelector::SELF, [](Aim& aim) { return BattlerSet::of(aim.actor); }},
    {AiTargetSelector::ALL_OTHER_MONSTERS, [](Aim& aim) { return aim.others().monstersOnly(); }},
    {AiTargetSelector::ALL_MONSTERS, [](Aim& aim) { return aim.battlers.monstersOnly(); }},
    {AiTargetSelector::RAND_OTHER_MONSTER, [](Aim& aim) { return pickOne(aim.state, aim.others().monstersOnly()); }},
    {AiTargetSelector::RAND_MONSTER, [](Aim& aim) { return pickOne(aim.state, aim.battlers.monstersOnly()); }},
    {AiTargetSelector::ALL_DEAD_CHARS,
     [](Aim& aim) { return withStatus(aim.state, StatusId::DEAD).charactersOnly(); }},
    {AiTargetSelector::RAND_DEAD_CHAR,
     [](Aim& aim) { return pickOne(aim.state, withStatus(aim.state, StatusId::DEAD).charactersOnly()); }},
    {AiTargetSelector::ALL_DEAD_MONSTERS,
     [](Aim& aim) { return withStatus(aim.state, StatusId::DEAD).monstersOnly(); }},
    {AiTargetSelector::RAND_DEAD_MONSTER,
     [](Aim& aim) { return pickOne(aim.state, withStatus(aim.state, StatusId::DEAD).monstersOnly()); }},
    {AiTargetSelector::ALL_REFLECT_CHARS,
     [](Aim& aim) { return withStatus(aim.state, StatusId::REFLECT).charactersOnly(); }},
    {AiTargetSelector::RAND_REFLECT_CHAR,
     [](Aim& aim) { return pickOne(aim.state, withStatus(aim.state, StatusId::REFLECT).charactersOnly()); }},
    {AiTargetSelector::ALL_REFLECT_MONSTERS,
     [](Aim& aim) { return withStatus(aim.state, StatusId::REFLECT).monstersOnly(); }},
    {AiTargetSelector::RAND_REFLECT_MONSTER,
     [](Aim& aim) { return pickOne(aim.state, withStatus(aim.state, StatusId::REFLECT).monstersOnly()); }},
    {AiTargetSelector::ALL_CHARS, [](Aim& aim) { return aim.battlers.charactersOnly(); }},
    {AiTargetSelector::RAND_CHAR, [](Aim& aim) { return pickOne(aim.state, aim.battlers.charactersOnly()); }},
    // The battler that last aimed at it, unless it is still waiting to be answered.
    {AiTargetSelector::PREV_ATTACKER,
     [](Aim& aim) {
         const Battler& battler = aim.state.at(aim.actor);
         if (!battler.lastAttacker || battler.retaliationPending) {
             return BattlerSet{};
         }
         return BattlerSet::of(*battler.lastAttacker);
     }},
    {AiTargetSelector::ALL_TARGETS, [](Aim& aim) { return aim.battlers; }},
    {AiTargetSelector::NOTHING, [](Aim&) { return BattlerSet{}; }},
    {AiTargetSelector::CHAR_SLOT_1, [](Aim& aim) { return partySlot(aim, 0); }},
    {AiTargetSelector::CHAR_SLOT_2, [](Aim& aim) { return partySlot(aim, 1); }},
    {AiTargetSelector::CHAR_SLOT_3, [](Aim& aim) { return partySlot(aim, 2); }},
    {AiTargetSelector::CHAR_SLOT_4, [](Aim& aim) { return partySlot(aim, 3); }},
    // Everyone else: all of them or, on the coin's other face, one of them.
    {AiTargetSelector::RAND_TARGET,
     [](Aim& aim) {
         const BattlerSet others = aim.others();
         return aim.state.rng.coin() ? others : pickOne(aim.state, others);
     }},
    // The battler Targetting marked, if it is on the field. The mark is used up.
    {AiTargetSelector::TARGETTING_TARGET,
     [](Aim& aim) {
         const std::optional<BattlerSlot> marked = std::exchange(aim.state.at(aim.actor).targetingTarget, std::nullopt);
         return marked ? removeAbsentTargets(aim.state, BattlerSet::of(*marked)) : BattlerSet{};
     }},
}};

constexpr bool rulesInOrder() {
    for (std::size_t row = 0; row < kSelectorRules.size(); ++row) {
        if (static_cast<std::size_t>(kSelectorRules[row].selector) !=
            static_cast<std::size_t>(AiTargetSelector::SELF) + row) {
            return false;
        }
    }
    return true;
}
static_assert(rulesInOrder(), "one rule per selector from SELF on, in the selectors' order");

}  // namespace

BattlerSet aiTargets(BattleState& state, BattlerSlot actor, AiTarget target, bool reachableOnly) {
    BattlerSet battlers = battlersInTheBattle(state);
    if (reachableOnly) {
        battlers = removeAbsentTargets(state, battlers);
        battlers = reachableTargets(state, battlers, state.awayFromTheFieldUnreachable);
    }
    if (target.isCharacter()) {
        for (int position = kPartySlots - 1; position >= 0; --position) {
            const BattlerSlot slot = BattlerSlot::character(position);
            if (state.at(slot).actor == target.character()) {
                return BattlerSet::of(slot);
            }
        }
        return {};
    }
    const AiTargetSelector selector = target.selector();
    if (selector < AiTargetSelector::SELF) {
        const BattlerSlot slot = BattlerSlot::monster(static_cast<int>(selector) -
                                                      static_cast<int>(AiTargetSelector::MONSTER_SLOT_1));
        return state.at(slot).monster ? BattlerSet::of(slot) : BattlerSet{};
    }
    Aim aim{state, actor, battlers};
    const std::size_t row = static_cast<std::size_t>(selector) - static_cast<std::size_t>(AiTargetSelector::SELF);
    return kSelectorRules[row].rule(aim);
}

}  // namespace ostinato::battle
