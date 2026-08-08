// The monster vertical-alignment value space (monster_align.dat, ROM
// EC/E800): where a monster sprite anchors vertically when the colosseum
// places it. The names are the upstream comment block's own
// (btlgfx_main.asm:2824-2829); CEILING is special-cased to the top of the
// screen before the per-alignment y-offset table applies
// (btlgfx_main.asm:2874-2877).
#pragma once

#include <cstdint>

namespace ostinato {

enum class MonsterVerticalAlignment : std::uint8_t {
    CEILING  = 0,
    GROUND   = 1,
    BURIED   = 2,
    FLOATING = 3,
    FLYING   = 4,
};

static_assert(sizeof(MonsterVerticalAlignment) == 1,
              "MonsterVerticalAlignment must be byte-identical to a ROM "
              "monster_align byte");

}  // namespace ostinato
