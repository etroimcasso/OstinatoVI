#include "data/ai_scripts.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The steps of every script, one array per part.
#include "data/generated/ai_script_steps_data.inc"

// The scripts in MonsterId order.
constexpr std::array<AiScript, kAiScriptCount> kAiScripts = {{
#include "data/generated/ai_scripts_data.inc"
}};

constexpr bool monsterMatchesPosition() {
    for (std::size_t i = 0; i < kAiScripts.size(); ++i) {
        if (static_cast<std::size_t>(kAiScripts[i].monster) != i) {
            return false;
        }
    }
    return true;
}

static_assert(monsterMatchesPosition(), "kAiScripts monster fields must match array positions");

}  // namespace

const AiScript& aiScript(std::uint16_t index) {
    assert(index < kAiScripts.size() && "not a script index");
    return kAiScripts[index];
}

const AiScript& aiScript(MonsterId monster) {
    return aiScript(static_cast<std::uint16_t>(monster));
}

std::span<const AiScript> aiScripts() { return kAiScripts; }

}  // namespace ostinato
