// A script's party poses: a character stepping forward for its command, stepping back, and standing
// normally again.

#include <cstdint>
#include <span>

#include "battle/battle_script.h"
#include "ostinato/battle_command_id.h"

#include "oracle/battle/script_read_shared.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace::detail {

battle::BattleScriptCommand partyRead(const QueueCommand& command, std::span<const std::uint8_t> block) {
    if (command[0] == kReadyStanceScriptCommand) {
        // The command in the queue (:21847) and the block InitGfxParams wrote for it (battle_main.asm
        // :5776-5787, :14428-14443): the actor in byte 1 (:21845) and its targets in bytes 2-3.
        return battle::ReadyStance{.actor = battle::BattlerSlot::fromIndex(block[1] % battle::kBattlerSlots),
                                   .command = static_cast<BattleCommandId>(command[1]),
                                   .targets = setOf(static_cast<std::uint16_t>(block[2] | block[3] << 8))};
    }
    if (command[0] == kStepBackScriptCommand) {
        return battle::StepBack{};
    }
    // A pose reset names the party member by index in byte 1 (Cmd_2c :13375-13379, _c2192e :4036-4040).
    return battle::PoseReset{.battler = battle::BattlerSlot::fromIndex(command[1] % battle::kBattlerSlots)};
}

}  // namespace ostinato::trace::detail
