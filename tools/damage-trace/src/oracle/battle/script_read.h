// The cartridge's battle script read into the port's commands, as the graphics program reads it when the
// battle asks for the script to play (battle graphics command $04, BtlGfx_04 btlgfx_main.asm:22386-22419).
//
//     const ScriptBytes bytes{.queue = queue, .paramsBlocks = blocks, .numeralBuffers = numerals,
//                             .messageVariables = variables};
//     const std::vector<ScriptCommandRead> script = readScript(bytes);
//     // script[i].command is the port's command the queue's i-th command shows, or none.
//
// The queue is four bytes a command from $7E:2D6E, up to the command $FF. An animation ($06), a bounce
// off Reflect ($09), a Launcher's hits ($15) and the pointer step ($07) each take the next sixteen-byte
// params block from $7E:2C6E (NextGfxCmdData :22434-22440, from :27078, :25243 and :22595); the ready
// stance ($0C) reads the next block and leaves it (GfxCmd_0c :21841-21873). Every numerals-at-once
// command ($03) takes the next twenty-byte buffer from $7E:2BCE, one word a battler
// (ShowDmgNumeralsMulti battle_main.asm:16301-16319). A message's two numbers are the first two of
// the four message variables, three bytes each from $7E:2F35.
//
// Each command carries what its handler reads, and every other field of the port's command is left at
// its default: an animation's item, its Chain Saw kill and whether it shows its attack are fields the
// port works out beside the bytes, and scriptShown() shows the bytes alone.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "scenarios/battle/script_shown.h"

namespace ostinato::trace {

struct ScriptBytes {
    std::span<const std::uint8_t> queue;
    std::span<const std::uint8_t> paramsBlocks;
    std::span<const std::uint8_t> numeralBuffers;
    std::array<std::uint8_t, 6> messageVariables{};
};

[[nodiscard]] std::vector<ScriptCommandRead> readScript(const ScriptBytes& bytes);

}  // namespace ostinato::trace
