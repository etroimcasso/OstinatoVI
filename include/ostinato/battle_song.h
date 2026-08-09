// The battle song selector: the 3-bit field ($38) of a formation aux record's
// last byte (src/data/formations.h). When a battle starts the code reads this
// field, looks it up in BattleSongTbl ($24,$14,$33,$2e,$1a,$3b,$ff,$ff;
// btlgfx_main.asm:41464-41465), and plays that song id — except $ff, the
// "keep the current song" sentinel used by the two unused high values.
//
// The song names are the community-documented titles for those song ids (the
// same mapping FF6Tools' battlePropertiesAux.song string table uses); the
// stored value is the 3-bit index, so the enumerator is byte-identical to the
// field.
#pragma once

#include <cstdint>

namespace ostinato {

enum class BattleSong : std::uint8_t {
    BATTLE_THEME        = 0,  // BattleSongTbl[0] = $24
    THE_DECISIVE_BATTLE = 1,  // $14 — the boss theme
    THE_FIERCE_BATTLE   = 2,  // $33
    RETURNERS           = 3,  // $2e
    SAVE_THEM           = 4,  // $1a
    DANCING_MAD         = 5,  // $3b — the final-battle theme
    NO_CHANGE_6         = 6,  // $ff — keep the current song
    NO_CHANGE_7         = 7,  // $ff — keep the current song
};

static_assert(sizeof(BattleSong) == 1,
              "BattleSong must be byte-identical to the 3-bit song field it "
              "is decoded from");

}  // namespace ostinato
