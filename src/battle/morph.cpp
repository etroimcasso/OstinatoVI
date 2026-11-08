#include "battle/morph.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "battle/action_queue.h"
#include "battle/actions.h"
#include "battle/command_menu.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/status_id.h"

namespace ostinato::battle {

namespace {

// The meter starts at its fullest.
constexpr std::uint16_t kMorphMeterFull = 0xFFFF;

// The fastest the meter can run down in one time update, before the one every speed adds.
constexpr std::uint16_t kMorphSpeedMax = 0x7FF;

// The full meter over the supply is divided by this to give the speed; by twice this when Morph
// lasts twice as long.
constexpr int kMorphSpeedDivisor = 8;

// How fast the meter runs down for the party's supply: the full meter over the supply, over 8 (16
// when Morph lasts twice as long), at most kMorphSpeedMax, plus 1.
std::uint16_t morphSpeed(const BattleState& state) {
    const int supply = state.party.morphSupply;
    assert(supply >= kMorphSupplyToMorph && "Morph goes on only with the supply its menu row needs");
    const int divisor = state.morphLastsTwiceAsLong ? 2 * kMorphSpeedDivisor : kMorphSpeedDivisor;
    const int speed = std::min<int>(kMorphMeterFull / supply / divisor, kMorphSpeedMax);
    return static_cast<std::uint16_t>(speed + 1);
}

// Morph and Revert trade places in the battler's menu rows: a battler now morphed is offered
// Revert where it was offered Morph, and the other way round.
void swapMorphAndRevert(Battler& battler, bool nowMorphed) {
    const BattleCommandId offered = nowMorphed ? BattleCommandId::MORPH : BattleCommandId::REVERT;
    const BattleCommandId replacement = nowMorphed ? BattleCommandId::REVERT : BattleCommandId::MORPH;
    for (MenuCommand& row : battler.commands) {
        if (row.command == offered) {
            row.command = replacement;
        }
    }
}

}  // namespace

void morphMenuChanged(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (!state.counterattackRunning && battler.gauge.isFull()) {
        battler.atbStopped = true;
        battler.menuMayOpen = true;
    }
    const bool nowMorphed = battler.status.has(StatusId::MORPH);
    swapMorphAndRevert(battler, nowMorphed);
    if (!nowMorphed) {
        saveMorphSupply(state);
        state.morphedBattler.reset();
        battler.morphGauge = 0;
        if (slot.isCharacter()) {
            refreshCommandMenu(state, slot);
        }
        state.morphCounter = 0;
        return;
    }
    if (state.morphedBattler || state.permanentMorph) {
        return;
    }
    state.morphedBattler = slot;
    state.morphCounter = kMorphMeterFull;
    state.morphCounterSpeed = morphSpeed(state);
}

void runDownMorph(BattleState& state, BattlerSlot slot) {
    if (state.morphCounter >= state.morphCounterSpeed) {
        state.morphCounter = static_cast<std::uint16_t>(state.morphCounter - state.morphCounterSpeed);
    } else {
        state.morphCounter = 0;
        saveMorphSupply(state);
        state.morphedBattler.reset();
        addCounterAction(state, slot,
                         CommandAction{.command = BattleCommandId::REVERT, .targets = BattlerSet::of(slot)});
    }
    state.at(slot).morphGauge = static_cast<std::uint8_t>(state.morphCounter >> 8);
}

void saveMorphSupply(BattleState& state) {
    if (!state.morphedBattler) {
        return;
    }
    const int meter = state.morphCounter >> 8;
    state.party.morphSupply = static_cast<std::uint8_t>(state.party.morphSupply * meter / 256);
}

}  // namespace ostinato::battle
