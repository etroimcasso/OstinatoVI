// A script's attack commands: an animation with its params block, a bounce off Reflect and a Launcher's
// hits with theirs, and the damage numerals.

#include <cstdint>
#include <span>

#include "battle/battle_script.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_id.h"

#include "cells/damage/per_target.h"
#include "oracle/battle/script_read_shared.h"
#include "oracle/status/attacker_effects_shared.h"
#include "scenarios/common/report_text.h"

namespace ostinato::trace::detail {

using namespace cells;

namespace {

// A command value from $20 up is one of the special animations (CmdAnimTbl btlgfx_main.asm:27082-27121).
constexpr std::uint8_t kFirstSpecialAnimation = static_cast<std::uint8_t>(battle::SpecialAnimation::DanceStumble);
// A Dance that changes no background leaves $FF in the fourth byte (Cmd_13 battle_main.asm:3711-3712).
constexpr std::uint8_t kNoBackgroundChange = 0xFF;

battle::DamageNumber numeral(battle::BattlerSlot target, std::uint16_t value) {
    return battle::DamageNumber{.target = target,
                                .amount = static_cast<std::uint16_t>(value & kAmountBits),
                                .miss = (value & kMissMark) != 0,
                                .heals = (value & kHealMark) != 0};
}

}  // namespace

battle::BattleScriptCommand animationRead(const QueueCommand& command, std::span<const std::uint8_t> block) {
    // GfxCmd_06 :27069-27078: the command the animation is shown as, the attack and the fourth byte, then
    // the block.
    battle::AttackAnimation animation{.attack = static_cast<AttackId>(command[2]),
                                      .secondary = command[3],
                                      .params = frameParamsFromBytes(block)};
    if (command[1] < kFirstSpecialAnimation) {
        animation.what = static_cast<BattleCommandId>(command[1]);
    } else {
        animation.what = static_cast<battle::SpecialAnimation>(command[1]);
    }
    if (command[1] == static_cast<std::uint8_t>(BattleCommandId::DANCE) && command[3] != kNoBackgroundChange) {
        animation.newBackground = static_cast<BattleBackgroundId>(command[3]);
    }
    // Tools shows a tool's index among the tools' animations: the item less Noiseblaster's, or 8 for the
    // Chain Saw's kill (Cmd_01 battle_main.asm:3933-3937, ToolsEffect_03 :7130-7131).
    if (command[1] == static_cast<std::uint8_t>(BattleCommandId::TOOLS)) {
        constexpr std::uint8_t kChainSawKillAnimation = 8;
        animation.chainSawKill = command[2] == kChainSawKillAnimation;
        animation.item = animation.chainSawKill
                             ? ItemId::CHAIN_SAW
                             : static_cast<ItemId>(static_cast<std::uint8_t>(ItemId::NOISEBLASTER) + command[2]);
    }
    // Blitz shows a blitz's index among the blitzes', the attack less Pummel's (Cmd_0a :3383-3387).
    if (command[1] == static_cast<std::uint8_t>(BattleCommandId::BLITZ)) {
        animation.attack = static_cast<AttackId>(static_cast<std::uint8_t>(AttackId::PUMMEL) + command[2]);
    }
    return animation;
}

battle::BattleScriptCommand bounceRead(const QueueCommand& command, std::span<const std::uint8_t> block) {
    if (command[0] == kReflectedScriptCommand) {
        return reflectedShown(command, block);
    }
    return scatteredShown(command, block);
}

battle::BattleScriptCommand numeralRead(const QueueCommand& command) {
    // ShowDmgNumerals :16280-16295: the battler's index, then the numeral's word.
    const auto value = static_cast<std::uint16_t>(command[2] | command[3] << 8);
    return battle::DamageNumbersShown{
        .numbers = {numeral(battle::BattlerSlot::fromIndex(command[1] % battle::kBattlerSlots), value)},
        .wait = battle::DamageNumberWait::CarryOn};
}

battle::BattleScriptCommand numeralsTogetherRead(std::span<const std::uint8_t> buffer) {
    // A word per battler by its index, $FFFF for none (_c262ef :16193-16274), drawn from the last
    // battler's down, as the one-at-a-time form writes them.
    battle::DamageNumbersShown shown{.wait = battle::DamageNumberWait::UntilGone};
    for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
        const auto at = static_cast<std::size_t>(index * 2);
        const auto value = static_cast<std::uint16_t>(buffer[at] | buffer[at + 1] << 8);
        if (value != kNoDamage) {
            shown.numbers.push_back(numeral(battle::BattlerSlot::fromIndex(index), value));
        }
    }
    return shown;
}

}  // namespace ostinato::trace::detail
