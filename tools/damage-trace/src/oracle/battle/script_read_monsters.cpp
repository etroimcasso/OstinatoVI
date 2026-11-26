// A script's monster commands: a battle event, a monster's dialog, a formation change, monsters entering
// or leaving, and a monster animation.

#include <cstdint>

#include "battle/battle_script.h"
#include "ostinato/monster_animation.h"
#include "ostinato/monster_entrance_type.h"

#include "oracle/battle/script_read_shared.h"
#include "scenarios/common/bytes.h"

namespace ostinato::trace::detail {

namespace {

// A byte with a bit per monster slot, as the queue holds the monsters a command names.
battle::BattlerSet monsters(std::uint8_t mask) {
    return setOf(static_cast<std::uint16_t>(mask << 8));
}

}  // namespace

battle::BattleScriptCommand monstersRead(const QueueCommand& command) {
    switch (command[0]) {
    case kBattleEventScriptCommand:
        // Cmd_23 battle_main.asm:13019-13024: the event in byte 1.
        return battle::BattleEventPlays{.event = command[1]};
    case kMonsterDialogScriptCommand:
        // GfxCmd_10 btlgfx_main.asm:22867-22869 reads the dialog as a word.
        return battle::MonsterDialogShown{.dialog = static_cast<std::uint16_t>(command[1] | command[2] << 8)};
    case kFormationScriptCommand:
        // Byte 1 the scroll and the exit, byte 3 the monsters still fighting (Cmd_20 :13237-13244, the final
        // battle's next stage InitBattle :6180-6191). GfxCmd_12 :22125-22127 writes byte 2 over before it
        // reads it.
        return battle::FormationChanges::from(command[1], monsters(command[3]));
    case kEntranceScriptCommand:
        // Byte 1 the entrance, byte 2 the monsters hidden, byte 3 those revived (Cmd_24 :13061-13083).
        return battle::MonstersEnterOrLeave{.type = static_cast<MonsterEntranceType>(command[1]),
                                            .hidden = monsters(command[2]),
                                            .revived = monsters(command[3])};
    default: {
        // Cmd_2b :16393-16401: the animation in byte 1; DoMonsterAnim btlgfx_main.asm:45502-45516 reads byte 2
        // as the monsters it plays on, or for the sound effect as the pan, with the sound in byte 3.
        const auto animation = static_cast<MonsterAnimation>(command[1]);
        if (animation == MonsterAnimation::SoundEffect) {
            return battle::MonsterAnimationPlays{.animation = animation, .soundEffect = command[3], .pan = command[2]};
        }
        return battle::MonsterAnimationPlays{.animation = animation, .monsters = monsters(command[2])};
    }
    }
}

}  // namespace ostinato::trace::detail
