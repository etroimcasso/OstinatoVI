#include "battle/battle_script.h"

#include <cstddef>
#include <utility>
#include <variant>

namespace ostinato::battle {

namespace {

// The party slot whose entry in a bounce's params block sits in the block's attacker's place.
constexpr BattlerSlot kBounceEntryInTheAttackersPlace = BattlerSlot::character(1);

}  // namespace

void BattleScriptBuilder::add(BattleScriptCommand command) {
    if (const auto* bounce = std::get_if<ReflectedAttackShown>(&command)) {
        lastParamsAttacker_ = bounce->bouncedTo[static_cast<std::size_t>(kBounceEntryInTheAttackersPlace.index())];
    }
    commands_.push_back(std::move(command));
}

void BattleScriptBuilder::addAnimation(AttackAnimation animation) {
    animationSlot_ = commands_.size();
    commands_.emplace_back(std::move(animation));
}

void BattleScriptBuilder::replaceAnimation(BattleScriptCommand command) {
    if (animationSlot_) {
        commands_[*animationSlot_] = std::move(command);
    }
}

void BattleScriptBuilder::addParams(const AttackAnimationParams& params) {
    params_.push_back(params);
    lastParamsAttacker_ = params.attacker;
}

std::vector<BattleScriptCommand> BattleScriptBuilder::take() {
    for (BattleScriptCommand& command : commands_) {
        auto* animation = std::get_if<AttackAnimation>(&command);
        if (animation == nullptr || params_.empty()) {
            continue;
        }
        animation->params = params_.front();
        params_.pop_front();
    }
    params_.clear();
    animationSlot_.reset();
    lastParamsAttacker_.reset();
    return std::exchange(commands_, {});
}

}  // namespace ostinato::battle
