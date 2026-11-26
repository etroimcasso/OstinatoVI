// A script's names and messages: the window at the top, and the wide window's battle messages.

#include <cstdint>
#include <optional>
#include <span>

#include "battle/battle_script.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"

#include "cells/damage/frame.h"
#include "oracle/battle/script_read_shared.h"

namespace ostinato::trace::detail {

namespace {

// The name kinds the queue holds (ShowAttackName battle_main.asm:8685-8701; Joker Doom's and a Blitz's
// are written as kind 0, :8760-8761 and :8770-8771). Kind 3 has no writer, and kind 6 is written as a
// command of its own ($11, :8742-8748).
enum : std::uint8_t { AttackName = 0, ItemName = 1, EsperName = 2, BushidoName = 4, CommandName = 5 };

// An esper is named by its index among the espers' attacks (ShowAttackName_02 :8724-8728).
constexpr std::uint8_t kFirstEsperAttack = static_cast<std::uint8_t>(EsperId::RAMUH);

std::uint32_t variable(std::span<const std::uint8_t, 6> variables, std::size_t from) {
    return static_cast<std::uint32_t>(variables[from] | variables[from + 1] << 8 | variables[from + 2] << 16);
}

}  // namespace

std::optional<battle::BattleScriptCommand> nameRead(const QueueCommand& command) {
    if (command[0] == kMonsterNameScriptCommand) {
        // GfxCmd_11 btlgfx_main.asm:22722-22724 reads the monster as a word.
        const auto monster = static_cast<std::uint16_t>(command[1] | command[2] << 8);
        if (monster == cells::kNoMonsterNamed) {
            return battle::MonsterSpecialAttackNameShown{};
        }
        return battle::MonsterSpecialAttackNameShown{.monster = static_cast<MonsterId>(monster)};
    }
    // GfxCmd_01 :22785-22792: the kind, then the number.
    const std::uint8_t number = command[2];
    switch (command[1]) {
    case AttackName: return battle::AttackNameShown{.attack = static_cast<AttackId>(number)};
    case ItemName: return battle::ItemNameShown{.item = static_cast<ItemId>(number)};
    case EsperName:
        return battle::EsperNameShown{.esper = static_cast<EsperId>(static_cast<std::uint8_t>(number + kFirstEsperAttack))};
    case BushidoName: return battle::BushidoNameShown{.technique = static_cast<AttackId>(number)};
    case CommandName: return battle::CommandNameShown{.command = static_cast<BattleCommandId>(number)};
    default: return std::nullopt;
    }
}

battle::BattleScriptCommand messageRead(const QueueCommand& command, std::span<const std::uint8_t, 6> variables) {
    constexpr std::size_t kSecondVariable = 3;
    return battle::BattleMessage{.message = command[1],
                                 .firstNumber = variable(variables, 0),
                                 .secondNumber = variable(variables, kSecondVariable)};
}

}  // namespace ostinato::trace::detail
