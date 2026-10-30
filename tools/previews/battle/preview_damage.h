// A stand-in for the battle's damage, so the preview can fight a battle to its end before the damage
// formulas exist: every battler an attack hits loses kPreviewHitDamage HP and falls at 0, and every
// attack ends by showing its numerals — the damage over each battler it hit, "Miss" over each it
// aimed at and missed, all at once. It stands in until the battle works out damage itself, and
// lives here so nothing in the game depends on it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "ostinato/status_id.h"

namespace ostinato::preview {

// How many HP a hit takes.
inline constexpr std::uint16_t kPreviewHitDamage = 25;

struct PreviewDamage : battle::AttackHooks {
    void targetDamage(battle::BattleState& state, battle::AttackInFlight&, battle::BattlerSlot,
                      battle::BattlerSlot target) override {
        battle::Battler& battler = state.at(target);
        battler.hp = static_cast<std::uint16_t>(std::max(0, battler.hp - kPreviewHitDamage));
        if (battler.hp == 0) {
            battler.status.set(StatusId::DEAD);
        }
        hits_.push_back({.target = target, .amount = kPreviewHitDamage});
    }

    void applyDamage(battle::BattleState&, battle::AttackInFlight& attack, battle::BattleScriptBuilder& script) override {
        battle::DamageNumbersShown shown{.numbers = std::move(hits_)};
        hits_.clear();
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
            if (attack.params.targets.has(slot) && !attack.params.targetsHit.has(slot)) {
                shown.numbers.push_back({.target = slot, .miss = true});
            }
        }
        if (!shown.numbers.empty()) {
            script.add(std::move(shown));
        }
    }

private:
    std::vector<battle::DamageNumber> hits_;
};

}  // namespace ostinato::preview
