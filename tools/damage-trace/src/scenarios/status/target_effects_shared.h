// An action waiting, an interrupting action and a party member's control menu as text: shared with
// the cartridge's side of the target effect, in namespace detail.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "battle/battle_state.h"

#include "cells/status/effects.h"

namespace ostinato::trace::detail {

using namespace cells;

std::string actionText(std::uint8_t command, std::uint8_t attack, std::uint16_t targets, std::uint8_t mp);

std::string immediateText(std::uint8_t command, std::uint8_t attack, std::uint16_t targets);

std::string controlMenuText(const std::array<battle::ControlMenuEntry, 4>& menu);

}  // namespace ostinato::trace::detail
