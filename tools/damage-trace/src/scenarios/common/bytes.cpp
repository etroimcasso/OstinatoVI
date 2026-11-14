// The conversions bytes.h declares.

#include "scenarios/common/bytes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "battle/attack.h"
#include "battle/battle_inventory.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "game/character_record.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"

#include "cells/damage/per_target.h"
#include "cells/status/effects.h"
#include "cells/status/status.h"

namespace ostinato::trace {

using namespace cells;

namespace {

// A battler's last-attacker mark: the attacker's slot index with the top bit set while the battler
// waits to answer it. With no last attacker the slot bits hold $7F, as the battle's start leaves
// them when it fills the cell with $FF; no slot reads as a battler there.
constexpr std::uint8_t kNoLastAttacker = 0x7F;
}  // namespace

namespace detail {

std::uint8_t markByte(const battle::Battler& battler) {
    const std::uint8_t slot =
        battler.lastAttacker ? static_cast<std::uint8_t>(battler.lastAttacker->index()) : kNoLastAttacker;
    return static_cast<std::uint8_t>(slot | (battler.retaliationPending ? kWaitingBit : 0));
}

std::uint16_t slotIndex(battle::BattlerSlot slot) {
    return static_cast<std::uint16_t>(slot.index() * 2);
}

std::uint16_t maskOf(battle::BattlerSet set) {
    return static_cast<std::uint16_t>(set.characters.bits | set.monsters.bits << 8);
}

battle::BattlerSet setOf(std::uint16_t mask) {
    return battle::BattlerSet{BattleSlotMask(static_cast<std::uint8_t>(mask & 0xFF)),
                              BattleSlotMask(static_cast<std::uint8_t>(mask >> 8))};
}
}  // namespace detail

namespace {

// The weapons' effects take the indexes 1 to 14, the items' $49 to $4E and the commands' $50 to $57;
// every other index is an attack's own.
constexpr std::uint8_t kLastWeaponEffect = 14;
constexpr std::uint8_t kFirstItemEffect = 0x49;
constexpr std::uint8_t kLastItemEffect = 0x4E;
constexpr std::uint8_t kItemEffectBase = 0x48;
constexpr std::uint8_t kFirstCommandEffect = 0x50;
constexpr std::uint8_t kLastCommandEffect = 0x57;
}  // namespace

namespace detail {

battle::SpecialEffect effectAt(std::uint8_t index) {
    if (index == 0) {
        return {};
    }
    if (index <= kLastWeaponEffect) {
        return battle::SpecialEffect::of(static_cast<WeaponSpecialEffect>(index));
    }
    if (index >= kFirstItemEffect && index <= kLastItemEffect) {
        return battle::SpecialEffect::of(static_cast<ItemUseEffect>(index - kItemEffectBase));
    }
    if (index >= kFirstCommandEffect && index <= kLastCommandEffect) {
        return battle::SpecialEffect::of(static_cast<battle::CommandEffect>(index));
    }
    return battle::SpecialEffect::of(static_cast<AttackSpecialEffect>(index));
}

std::optional<battle::BattlerSlot> slotTimesTwo(std::uint8_t value) {
    if (value / 2 >= battle::kBattlerSlots) {
        return std::nullopt;
    }
    return battle::BattlerSlot::fromIndex(value / 2);
}
}  // namespace detail

namespace {

// The per-battler cells, each a word at twice the battler's index from its first.
constexpr std::array<std::uint32_t, 22> kBattlerCells{
    kBattlerBit, kToUpdate,  kLastDamagedBy, kController - 1, kLastAttacker, kTaken,          kHealed,
    kOnTheField, kLevel,     kDefenses,      kAbsorbedNullified, kWeakHalved, kHp,           kMp,
    kMaxHp,      kMaxMp,     kRelicEffects1And3, kRelicEffects4And5, kMonsterFlags, kToSet12,
    kStatus12,   kStatus34,
};
}  // namespace

namespace detail {

std::optional<int> battlerOfCell(std::uint32_t cell) {
    for (const std::uint32_t first : kBattlerCells) {
        if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
            return static_cast<int>((cell - first) / 2);
        }
    }
    for (const std::uint32_t first : {kToSet34, kToClear12, kToClear34}) {
        if (cell >= first && cell < first + 2 * battle::kBattlerSlots) {
            return static_cast<int>((cell - first) / 2);
        }
    }
    return std::nullopt;
}

battle::BattlerSlot slotNamed(std::uint8_t index, const char* what) {
    if (index >= battle::kBattlerSlots) {
        throw std::runtime_error(std::string{what} + " names no battler: " + std::to_string(index));
    }
    return battle::BattlerSlot::fromIndex(index);
}

std::uint8_t pendingByte(const battle::PendingUpdates& pending) {
    std::uint8_t bits = 0;
    bits |= pending.enabledMagic ? 0x80 : 0;
    bits |= pending.removeAllActions ? 0x40 : 0;
    bits |= pending.startCondemned ? 0x20 : 0;
    bits |= pending.stopCondemned ? 0x10 : 0;
    bits |= pending.commandList ? 0x08 : 0;
    bits |= pending.speed ? 0x04 : 0;
    bits |= pending.morph ? 0x02 : 0;
    bits |= pending.leaveTheField ? 0x01 : 0;
    return bits;
}

void writeThreeBytes(std::array<std::uint8_t, 3>& bytes, std::uint32_t value) {
    bytes = {static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>((value >> 8) & 0xFF),
             static_cast<std::uint8_t>((value >> 16) & 0xFF)};
}

std::uint8_t slotByte(const std::optional<battle::BattlerSlot>& slot) {
    return slot ? static_cast<std::uint8_t>(slotIndex(*slot)) : kNoOne;
}

std::uint8_t linkByte(const std::optional<battle::BattlerSlot>& slot, bool ending) {
    return slot ? static_cast<std::uint8_t>(slotIndex(*slot) | (ending ? kEndingBit : 0)) : kNoOne;
}

std::uint8_t commandByte(const std::variant<BattleCommandId, battle::SpecialAnimation>& what) {
    return std::visit([](auto value) { return static_cast<std::uint8_t>(value); }, what);
}

std::uint8_t triggersByte(const battle::BattlerTriggers& triggers) {
    std::uint8_t bits = triggers.overcast ? kOvercastBit : 0;
    bits |= triggers.phantasm ? kPhantasmBit : 0;
    bits |= triggers.usesControlMenu ? kControlMenuBit : 0;
    return bits;
}

battle::BattlerSet seizedSet(const battle::BattleState& state) {
    battle::BattlerSet seized;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.at(battle::BattlerSlot::character(position)).seizedBy) {
            seized.add(battle::BattlerSlot::character(position));
        }
    }
    return seized;
}

std::optional<battle::BattlerSlot> slotOrNone(std::uint8_t value) {
    return value == kNoOne ? std::nullopt : slotTimesTwo(value);
}

}  // namespace detail

}  // namespace ostinato::trace
