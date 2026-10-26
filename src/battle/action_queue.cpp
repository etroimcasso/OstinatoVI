#include "battle/action_queue.h"

#include <algorithm>

namespace ostinato::battle {

std::optional<BattlerSlot> BattlerQueue::pop() {
    if (slots_.empty()) {
        return std::nullopt;
    }
    const BattlerSlot front = slots_.front();
    slots_.pop_front();
    return front;
}

void BattlerQueue::erase(BattlerSlot slot) {
    slots_.erase(std::remove(slots_.begin(), slots_.end(), slot), slots_.end());
}

bool BattlerQueue::contains(BattlerSlot slot) const {
    return std::find(slots_.begin(), slots_.end(), slot) != slots_.end();
}

}  // namespace ostinato::battle
