// The preview's player: it answers every battle menu with Fight at the first monster still
// standing, and confirms a message that waits for a key once it has been up for the message time.
// Nothing else.
#pragma once

#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "battle/battle_requests.h"
#include "battle/battle_state.h"
#include "battle/player_intake.h"
#include "battle/presentation.h"
#include "ostinato/battle_command_id.h"

namespace ostinato::preview {

class ScriptedPlayer {
public:
    // `confirmAfter` is how many frames a message waiting for a key stays up before the player
    // confirms it.
    explicit ScriptedPlayer(int confirmAfter) : confirmAfter_(confirmAfter) {}

    // Answer the battle's menus among `requests`: Fight, aimed at the first monster standing.
    std::vector<battle::PlayerAction> answer(std::span<const battle::BattleRequest> requests,
                                             const battle::BattleState& state) const {
        std::vector<battle::PlayerAction> actions;
        for (const battle::BattleRequest& request : requests) {
            const auto* menu = std::get_if<battle::OpenBattleMenu>(&request);
            if (menu == nullptr) {
                continue;
            }
            if (const std::optional<battle::BattlerSlot> target = firstMonsterStanding(state)) {
                actions.push_back({.character = menu->character,
                                   .command = BattleCommandId::FIGHT,
                                   .targets = battle::BattlerSet::of(*target)});
            }
        }
        return actions;
    }

    // Whether to confirm this frame: the message shown has waited for a key long enough.
    bool confirms(const battle::BattlePresentationState& shown) {
        if (!battle::awaitingKey(shown)) {
            waited_ = 0;
            return false;
        }
        if (++waited_ < confirmAfter_) {
            return false;
        }
        waited_ = 0;
        return true;
    }

private:
    static std::optional<battle::BattlerSlot> firstMonsterStanding(const battle::BattleState& state) {
        for (int position = 0; position < battle::kMonsterSlots; ++position) {
            const battle::BattlerSlot slot = battle::BattlerSlot::monster(position);
            if (state.monstersStanding.has(slot)) {
                return slot;
            }
        }
        return std::nullopt;
    }

    int confirmAfter_;
    int waited_ = 0;
};

}  // namespace ostinato::preview
