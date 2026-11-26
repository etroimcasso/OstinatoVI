// The cartridge's battle script walked command by command, each read by its family.

#include "oracle/battle/script_read.h"

#include <cstddef>
#include <optional>

#include "cells/damage/per_target.h"
#include "oracle/battle/script_read_shared.h"

namespace ostinato::trace {

using namespace detail;

std::vector<ScriptCommandRead> readScript(const ScriptBytes& bytes) {
    std::vector<ScriptCommandRead> script;
    std::size_t nextBlock = 0;
    std::size_t nextBuffer = 0;
    const auto block = [&](std::size_t index) -> std::optional<std::span<const std::uint8_t>> {
        const std::size_t at = index * cells::kParamsBlockSize;
        if (at + cells::kParamsBlockSize > bytes.paramsBlocks.size()) {
            return std::nullopt;
        }
        return bytes.paramsBlocks.subspan(at, cells::kParamsBlockSize);
    };
    const auto buffer = [&](std::size_t index) -> std::optional<std::span<const std::uint8_t>> {
        const std::size_t at = index * cells::kNumeralBufferSize;
        if (at + cells::kNumeralBufferSize > bytes.numeralBuffers.size()) {
            return std::nullopt;
        }
        return bytes.numeralBuffers.subspan(at, cells::kNumeralBufferSize);
    };
    const std::span<const std::uint8_t, 6> variables{bytes.messageVariables};
    for (std::size_t at = 0; at + cells::kQueueCommandSize <= bytes.queue.size(); at += cells::kQueueCommandSize) {
        ScriptCommandRead read;
        for (std::size_t i = 0; i < cells::kQueueCommandSize; ++i) {
            read.bytes[i] = bytes.queue[at + i];
        }
        const QueueCommand& command = read.bytes;
        if (command[0] == kScriptEnd) {
            break;
        }
        switch (command[0]) {
        case kNameScriptCommand:
        case kMonsterNameScriptCommand:
            read.command = nameRead(command);
            break;
        case kMessageScriptCommand:
            read.command = messageRead(command, variables);
            break;
        case kNumeralsTogetherScriptCommand:
            if (const auto numerals = buffer(nextBuffer++)) {
                read.command = numeralsTogetherRead(*numerals);
            }
            break;
        case kOneNumeralScriptCommand:
            read.command = numeralRead(command);
            break;
        case kAnimationScriptCommand:
            if (const auto params = block(nextBlock++)) {
                read.command = animationRead(command, *params);
            }
            break;
        case kReflectedScriptCommand:
        case kScatteredScriptCommand:
            if (const auto params = block(nextBlock++)) {
                read.command = bounceRead(command, *params);
            }
            break;
        case kParamsStepScriptCommand:
            ++nextBlock;
            break;
        case kReadyStanceScriptCommand:
            if (const auto params = block(nextBlock)) {
                read.command = partyRead(command, *params);
            }
            break;
        case kStepBackScriptCommand:
        case kPoseResetScriptCommand:
            read.command = partyRead(command, {});
            break;
        case kBattleEventScriptCommand:
        case kMonsterDialogScriptCommand:
        case kFormationScriptCommand:
        case kEntranceScriptCommand:
        case kMonsterAnimationScriptCommand:
            read.command = monstersRead(command);
            break;
        default:
            break;
        }
        script.push_back(read);
    }
    return script;
}

}  // namespace ostinato::trace
