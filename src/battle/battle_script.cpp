#include "battle/battle_script.h"

#include <utility>
#include <variant>

namespace ostinato::battle {

void BattleScriptBuilder::add(BattleScriptCommand command) {
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
}

std::optional<BattlerSlot> BattleScriptBuilder::lastParamsAttacker() const {
    if (params_.empty()) {
        return std::nullopt;
    }
    return params_.back().attacker;
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
    return std::exchange(commands_, {});
}

}  // namespace ostinato::battle
