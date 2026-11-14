// The port's setting up of the animation and its cover, their differences, and the cover sweep's
// draw.

#include "scenarios/damage/cover.h"

#include <random>
#include <string>
#include <utility>

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/targeting.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"

#include "scenarios/common/bytes.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "scenarios/damage/damage.h"
#include "scenarios/status/attacker_effects_shared.h"
#include "scenarios/status/target_effects.h"

namespace ostinato::trace {

using namespace detail;

ParamsInitResult portInitParams(DamageScenario scenario) {
    battle::AttackInFlight& attack = scenario.attack;
    battle::initParams(attack, scenario.attacker);
    EffectCells out;
    out.put("params", paramsText(attack.params));
    out.put("random target", attack.rules.randomTarget);
    out.put("fall back to earlier targets", attack.rules.fallBackToEarlierTargets);
    out.put("other side only", attack.rules.opposingSideOnly);
    out.put("swirl suppressed", attack.castingSwirlSuppressed);
    return ParamsInitResult{.cells = std::move(out.cells)};
}

std::string differences(const ParamsInitResult& port, const ParamsInitResult& guest) {
    return cellDifferences(port.cells, guest.cells);
}

CoverResult portCoverTarget(DamageScenario scenario) {
    const battle::AttackInFlight& attack = scenario.attack;
    battle::coverTarget(scenario.state, scenario.attack, scenario.attacker);
    EffectCells out;
    out.put("targets", std::to_string(maskOf(attack.targets)));
    out.put("covered ally", slotText(attack.params.coveredAlly));
    out.put("stepping in", std::to_string(maskOf(attack.params.reflectedOff | attack.params.steppingIn)));
    return CoverResult{.cells = std::move(out.cells)};
}

std::string differences(const CoverResult& port, const CoverResult& guest) {
    return cellDifferences(port.cells, guest.cells);
}

DamageScenario drawCoverScenario(std::mt19937& rng) {
    DamageScenario s = drawTargetEffectScenario(rng).scenario;
    battle::BattleState& state = s.state;
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        battle::Battler& battler = state.at(slot);
        if (oneIn(rng, 2)) {
            battler.status = {};
        }
        if (oneIn(rng, 2)) {
            battler.status.set(StatusId::NEAR_FATAL);
        }
        if (oneIn(rng, 2)) {
            battler.relicEffects4.set(RelicEffect4::PROTECT_WEAK_ALLIES);
        }
        // A Control link or a Seize hold on this battler, its other end on the battler holding it.
        const battle::BattlerSlot other = anySlot(rng);
        if (other != slot && oneIn(rng, 6)) {
            const bool ending = oneIn(rng, 2);
            battler.controlledBy = other;
            battler.controlEnding = ending;
            state.at(other).controlling = slot;
            state.at(other).controlEnding = ending;
        } else if (other != slot && oneIn(rng, 6)) {
            battler.seizedBy = other;
            state.at(other).seizing = slot;
        }
    }
    battle::BattlerSet targets;
    if (!oneIn(rng, 4)) {
        targets = anyBattlers(rng);
        if (targets.empty()) {
            targets.add(anySlot(rng));
        }
    }
    s.attack.targets = targets;
    s.attack.params.reflectedOff = {};
    s.attack.params.steppingIn = {};
    s.attack.params.coveredAlly.reset();
    return s;
}

}  // namespace ostinato::trace
