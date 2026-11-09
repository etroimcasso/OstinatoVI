// The animations a monster's AI script can play on monsters or the party, outside any attack.
//
// A script step names one of these with the monsters it plays on (data/ai_scripts.h); the battle
// shows it as a MonsterAnimationPlays script command (battle/battle_script.h).
#pragma once

#include <cstdint>

namespace ostinato {

enum class MonsterAnimation : std::uint8_t {
    FlashRed = 0,
    MoveBackSlowly = 1,             // 8 pixels
    MoveForwardSlowly = 2,
    MoveBackQuickly = 3,
    MoveForwardQuickly = 4,
    PartyRunsLeftToRight = 5,       // the party runs across the screen
    PartyRunsRightToLeft = 6,
    MoveBackFar = 7,                // 64 pixels
    MoveForwardFar = 8,
    SoundEffect = 9,                // a sound effect, panned
    KefkaHead = 10,
    LongGlow = 11,
    ShortGlow = 12,
    KefkaDeath = 13,
};

}  // namespace ostinato
