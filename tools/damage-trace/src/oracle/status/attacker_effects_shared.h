// A params block read back from the cartridge's bytes: shared with the target effect, the cover,
// the whole attack and the special effects' replay, in namespace detail.
#pragma once

#include <cstdint>
#include <span>

#include "battle/battle_script.h"

#include "scenarios/common/bytes.h"

namespace ostinato::trace::detail {

battle::AttackAnimationParams paramsFromBytes(std::span<const std::uint8_t> block);

}  // namespace ostinato::trace::detail
