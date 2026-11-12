#include "battle/battle_end.h"

#include "battle/battle_inventory.h"
#include "battle/battle_requests.h"
#include "battle/battle_script.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/morph.h"
#include "battle/party_setup.h"

namespace ostinato::battle {

void wrapUpBattle(BattleState& state, ActionExecutor& executor) {
    saveMorphSupply(state);
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        state.at(slot).morphGauge = 0;
        state.requests.push_back(CloseBattleMenu{.character = slot});
    }
    state.menusDisabled = true;
    state.requests.push_back(RunBattleScript{.commands = {Wait{.frames = kWrapUpWaitFrames}}});
    state.holdingItem = state.holdingItem | BattlerSet::allCharacters();
    putAwayItemsObtained(state);
    mergeItemsObtained(state);
    applyEquipmentChanges(state, executor);
}

}  // namespace ostinato::battle
