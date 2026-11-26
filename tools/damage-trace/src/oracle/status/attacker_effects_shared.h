// A params block read back from the cartridge's bytes: shared with the target effect, the cover,
// the whole attack, the special effects' replay and a whole battle's scripts, in namespace detail.
#pragma once

#include <cstdint>
#include <span>

#include "battle/battle_script.h"

#include "scenarios/common/bytes.h"

namespace ostinato::trace::detail {

// The flags, the attacker and the three battler sets a block's first eight bytes hold.
battle::AttackAnimationParams paramsFromBytes(std::span<const std::uint8_t> block);

// The whole block as an animation reads it: paramsFromBytes()'s fields with the critical flash and a
// monster's special attack from the flags, the covered ally from byte 8 (no one at 0, as every block
// starts cleared, ClearGfxParams battle_main.asm:16352-16363) and each party slot's block from bytes
// 10-13.
battle::AttackAnimationParams frameParamsFromBytes(std::span<const std::uint8_t> block);

}  // namespace ostinato::trace::detail
