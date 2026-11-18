// Every script step's place in the cartridge's script block, an action's cells as text, and the
// bytes a script holds for an action of its own: shared with the cartridge's side of the AI's runs,
// commands and replays, in namespace detail.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "battle/action_queue.h"
#include "battle/attack.h"
#include "data/ai_scripts.h"

namespace ostinato::trace::detail {

// Every step's place in the cartridge's script block, from the fixture's parts and the steps' sizes.
const std::map<const AiStep*, std::uint16_t>& stepPlaces();

// An action's four values as the cartridge's lists hold them, with none for the bytes its command
// never reads.
struct ActionCells {
    std::uint8_t command = 0;
    std::optional<std::uint8_t> attack;
    std::optional<std::uint8_t> targetsLow;
    std::optional<std::uint8_t> targetsHigh;
    std::uint8_t mpCost = 0;
};

std::string cellsText(const ActionCells& cells);

// The cells a command reads, given everything the list holds for it: a dialog and a battle event
// read only the attack byte, a variable change and an effect its low targets byte as well.
ActionCells readCells(std::uint8_t command, std::uint8_t attack, std::uint16_t targets, std::uint8_t mpCost);

// The bytes of the step a script would hold for an action of its own.
std::vector<std::uint8_t> stepBytesFor(const battle::BattleVariableAction& a);

std::vector<std::uint8_t> stepBytesFor(const battle::BattleSwitchAction& a);

std::vector<std::uint8_t> stepBytesFor(const battle::AiEffectAction& a);

// The cells an action a script queues holds in the cartridge's list: a command's own four, or the
// command an AI step goes out as with the step's first operand as the attack and the next two as the
// targets (AICmd_f2-AICmd_fb :4476-4549). None for an action no script queues.
std::optional<ActionCells> listedActionCells(const battle::Action& action);

}  // namespace ostinato::trace::detail
