// The script reader's families, one function each, that readScript() walks the queue with, in namespace
// detail. Each takes one queue command and what it plays with, and gives the port's command it shows,
// or none for a command of the family the port has no kind for.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "battle/battle_script.h"

namespace ostinato::trace::detail {

using QueueCommand = std::array<std::uint8_t, 4>;

// The script commands readScript() reads, as the graphics program's table numbers them (GfxCmdTbl
// btlgfx_main.asm:21884-21911).
inline constexpr std::uint8_t kNameScriptCommand = 0x01;
inline constexpr std::uint8_t kMessageScriptCommand = 0x02;
inline constexpr std::uint8_t kNumeralsTogetherScriptCommand = 0x03;
inline constexpr std::uint8_t kAnimationScriptCommand = 0x06;
inline constexpr std::uint8_t kParamsStepScriptCommand = 0x07;
inline constexpr std::uint8_t kReflectedScriptCommand = 0x09;
inline constexpr std::uint8_t kOneNumeralScriptCommand = 0x0B;
inline constexpr std::uint8_t kReadyStanceScriptCommand = 0x0C;
inline constexpr std::uint8_t kStepBackScriptCommand = 0x0D;
inline constexpr std::uint8_t kPoseResetScriptCommand = 0x0E;
inline constexpr std::uint8_t kBattleEventScriptCommand = 0x0F;
inline constexpr std::uint8_t kMonsterDialogScriptCommand = 0x10;
inline constexpr std::uint8_t kMonsterNameScriptCommand = 0x11;
inline constexpr std::uint8_t kFormationScriptCommand = 0x12;
inline constexpr std::uint8_t kEntranceScriptCommand = 0x13;
inline constexpr std::uint8_t kMonsterAnimationScriptCommand = 0x14;
inline constexpr std::uint8_t kScatteredScriptCommand = 0x15;
inline constexpr std::uint8_t kScriptEnd = 0xFF;

// A name in the window at the top: $01 and $11 (script_read_text.cpp).
std::optional<battle::BattleScriptCommand> nameRead(const QueueCommand& command);

// A battle message, $02, with the first two message variables (script_read_text.cpp).
battle::BattleScriptCommand messageRead(const QueueCommand& command, std::span<const std::uint8_t, 6> variables);

// An animation with its block, $06; a bounce off Reflect or a Launcher's hits with theirs, $09 and $15
// (script_read_attack.cpp).
battle::BattleScriptCommand animationRead(const QueueCommand& command, std::span<const std::uint8_t> block);
battle::BattleScriptCommand bounceRead(const QueueCommand& command, std::span<const std::uint8_t> block);

// A numeral shown one at a time, $0B; every battler's at once from its buffer, $03 (script_read_attack.cpp).
battle::BattleScriptCommand numeralRead(const QueueCommand& command);
battle::BattleScriptCommand numeralsTogetherRead(std::span<const std::uint8_t> buffer);

// The ready stance with the block it reads, $0C; the step back, $0D; a pose reset, $0E
// (script_read_party.cpp).
battle::BattleScriptCommand partyRead(const QueueCommand& command, std::span<const std::uint8_t> block);

// A battle event, a monster's dialog, a formation change, monsters entering or leaving and a monster
// animation: $0F, $10, $12, $13 and $14 (script_read_monsters.cpp).
battle::BattleScriptCommand monstersRead(const QueueCommand& command);

}  // namespace ostinato::trace::detail
