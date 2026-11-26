// The report text report_text.h declares.

#include "scenarios/common/report_text.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battler_set.h"
#include "data/text_corpus.h"
#include "ostinato/attack_id.h"
#include "ostinato/status_set.h"
#include "ostinato/text_class.h"

#include "cells/damage/per_target.h"
#include "scenarios/common/bytes.h"
#include "scenarios/ending/rewards.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// The bounce's block: a battler per slot, then a block per party slot, then a battler set.
constexpr std::size_t kBlockBlocks = 10;
}  // namespace

namespace detail {

std::string slotText(battle::BattlerSlot slot) {
    return std::to_string(slot.index());
}

std::string shownText(const battle::ReflectedAttackShown& shown) {
    std::string text = "bounced attack " + std::to_string(static_cast<int>(shown.attack)) + ":";
    for (std::size_t i = 0; i < shown.bouncedTo.size(); ++i) {
        if (shown.bouncedTo[i]) {
            text += " " + std::to_string(i) + ">" + slotText(*shown.bouncedTo[i]);
        }
    }
    text += " hit " + std::to_string(maskOf(shown.bouncesThatHit)) + " blocks";
    for (const battle::BlockShown block : shown.blocks) {
        text += " " + std::to_string(static_cast<int>(block));
    }
    return text;
}

std::string shownText(const battle::ScatteredHitsShown& shown) {
    std::string text = "scattered attack " + std::to_string(static_cast<int>(shown.attack)) + " from " +
                       slotText(shown.attacker) + ":";
    for (const battle::BattlerSlot target : shown.targets) {
        text += " " + slotText(target);
    }
    return text;
}

battle::ReflectedAttackShown reflectedShown(std::span<const std::uint8_t> command, std::span<const std::uint8_t> block) {
    battle::ReflectedAttackShown shown{.attack = static_cast<AttackId>(command[2])};
    for (std::size_t i = 0; i < battle::kBattlerSlots; ++i) {
        if (block[i] != kNoOne) {
            shown.bouncedTo[i] = battle::BattlerSlot::fromIndex(block[i]);
        }
    }
    shown.bouncesThatHit = setOf(static_cast<std::uint16_t>(block[kBlockSet] | block[kBlockSet + 1] << 8));
    for (std::size_t i = 0; i < battle::kPartySlots; ++i) {
        shown.blocks[i] = static_cast<battle::BlockShown>(block[kBlockBlocks + i]);
    }
    return shown;
}

battle::ScatteredHitsShown scatteredShown(std::span<const std::uint8_t> command, std::span<const std::uint8_t> block) {
    battle::ScatteredHitsShown shown{.attack = static_cast<AttackId>(command[2])};
    for (int i = battle::kBattlerSlots - 1; i >= 0; --i) {
        if (block[static_cast<std::size_t>(i)] != kNoOne) {
            shown.targets.push_back(battle::BattlerSlot::fromIndex(block[static_cast<std::size_t>(i)]));
        }
    }
    const battle::BattlerSet set = setOf(static_cast<std::uint16_t>(block[kBlockSet] | block[kBlockSet + 1] << 8));
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        if (set.has(battle::BattlerSlot::fromIndex(index))) {
            shown.attacker = battle::BattlerSlot::fromIndex(index);
        }
    }
    return shown;
}

std::string bounceText(std::span<const std::uint8_t> command, std::span<const std::uint8_t> block) {
    if (command[0] == kReflectedCommand) {
        return shownText(reflectedShown(command, block));
    }
    if (command[0] == kScatteredCommand) {
        return shownText(scatteredShown(command, block));
    }
    return "command " + std::to_string(command[0]);
}

std::string busAddress(std::uint32_t at) {
    char text[12];
    std::snprintf(text, sizeof text, "$%02X:%04X", (at >> 16) & 0xFF, at & 0xFFFF);
    return text;
}

std::string statusesText(const StatusSet& set) {
    return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
           "/" + std::to_string(set.bytes[3]);
}

std::string setText(battle::BattlerSet set) {
    return std::to_string(maskOf(set));
}

std::string slotText(const std::optional<battle::BattlerSlot>& slot) {
    return slot ? std::to_string(slot->index()) : std::string{"none"};
}

std::string paramsText(const battle::AttackAnimationParams& params) {
    return "attacker " + std::to_string(params.attacker.index()) + " targets " + setText(params.targets) + " hit " +
           setText(params.targetsHit) + " reflected off " + setText(params.reflectedOff) + " swirl " +
           std::to_string(params.castingSwirl) + " monster " + std::to_string(params.attackerIsMonster) +
           " no party targets " + std::to_string(params.noCharacterTargets) + " dragon horn " +
           std::to_string(params.dragonHorn);
}

std::string cellDifferences(const std::map<std::string, std::string>& port,
                            const std::map<std::string, std::string>& guest) {
    std::string out;
    for (const auto& [cell, theirs] : guest) {
        const auto ours = port.find(cell);
        const std::string value = ours == port.end() ? std::string{"(not read)"} : ours->second;
        if (value != theirs) {
            out += cell + ": port " + value + ", cartridge " + theirs + "\n";
        }
    }
    return out;
}

void addLine(std::string& out, const char* field, int port, int guest) {
    if (port != guest) {
        out += std::string{field} + ": port " + std::to_string(port) + ", cartridge " + std::to_string(guest) + "\n";
    }
}

std::string messageText(const MessageShown& shown) {
    std::string text = "message " + std::to_string(shown.message);
    if (shown.first) {
        text += " first " + std::to_string(*shown.first);
    }
    if (shown.second) {
        text += " second " + std::to_string(*shown.second);
    }
    return text;
}

std::string markText(const std::optional<battle::BattlerSlot>& slot) {
    return slot ? std::to_string(slot->index()) : std::string{"none"};
}

const TextCorpus& noText() {
    static const TextCorpus corpus{std::array<std::vector<std::uint8_t>, kTextClassCount>{}};
    return corpus;
}

}  // namespace detail

}  // namespace ostinato::trace
