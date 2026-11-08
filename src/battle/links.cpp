#include "battle/links.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "battle/action_queue.h"
#include "battle/actions.h"
#include "data/attack_properties.h"
#include "data/monster_attacks.h"
#include "ostinato/attack_id.h"
#include "ostinato/game_version.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"

namespace ostinato::battle {

namespace {

// The MP the comparison wraps at: one more than this reads as none.
constexpr std::uint16_t kMpWordMax = 0xFFFF;

// The battler stops once the action is over: its gauge empties, every action it had waiting is
// dropped, and its menu does not open.
void stopsAfterTheAction(Battler& battler) {
    battler.emptyGaugeAfterAction = true;
    battler.pendingUpdates.removeAllActions = true;
    battler.menuMayOpen = false;
}

// Let go of the Love Token in which `covered`'s hits go to `bodyguard`.
void releaseLoveToken(BattleState& state, BattlerSlot covered, BattlerSlot bodyguard) {
    state.at(covered).bodyguard.reset();
    state.at(bodyguard).guarding.reset();
}

// Let go of the Charm in which `charmer` holds `charmed`.
void releaseCharm(BattleState& state, BattlerSlot charmer, BattlerSlot charmed) {
    state.at(charmer).charming.reset();
    state.at(charmed).charmedBy.reset();
}

}  // namespace

const StatusSet kStatusesThatEndControl =
    StatusSet::of(StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE, StatusId::BERSERK, StatusId::CONFUSE,
                  StatusId::SLEEP, StatusId::DANCE, StatusId::STOP, StatusId::RAGE, StatusId::FROZEN);

void controlMenu(BattleState& state, BattlerSlot controller, BattlerSlot monster) {
    const Battler& held = state.at(monster);
    const MonsterControl& attacks = getMonsterControl(held.monster.value_or(MonsterId{}));
    std::array<ControlMenuEntry, 4>& menu = state.at(controller).controlMenu;
    for (std::size_t row = 0; row < menu.size(); ++row) {
        const AttackId attack = attacks.attacks[row];
        if (attack == AttackId::NONE) {
            menu[row] = ControlMenuEntry{};
            continue;
        }
        const AttackProperties& record = getAttackProperties(attack);
        menu[row] = ControlMenuEntry{
            .attack = attack,
            .castable = held.mp != kMpWordMax && record.mpCost <= held.mp,
            .targeting = record.targeting,
        };
    }
}

void endControlIfUnable(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if ((battler.controlling || battler.controlledBy) && battler.status.hasAny(kStatusesThatEndControl)) {
        battler.controlEnding = true;
    }
}

void releaseEndedControl(BattleState& state, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    if (!battler.controlEnding) {
        return;
    }
    if (const std::optional<BattlerSlot> controller = battler.controlledBy) {
        releaseControl(state, *controller, slot);
    }
    if (const std::optional<BattlerSlot> monster = state.at(slot).controlling) {
        releaseControl(state, slot, *monster);
    }
}

void releaseControl(BattleState& state, BattlerSlot controller, BattlerSlot monster) {
    Battler& holder = state.at(controller);
    Battler& held = state.at(monster);
    if (language(state.version) == Language::EN) {
        holder.triggers.usesControlMenu = false;
    }
    // Chant comes off the status itself, not through the statuses waiting to take effect.
    holder.status.clear(StatusId::CHANT);
    held.controlledBy.reset();
    held.controlEnding = false;
    holder.controlling.reset();
    holder.controlEnding = false;
    state.flippedByControl.remove(monster);
    stopsAfterTheAction(held);
    stopsAfterTheAction(holder);
}

void releaseLinks(BattleState& state, BattlerSlot slot) {
    if (state.swallowedByZinger == slot) {
        const BattlerSet zinger = state.heldByZinger ? BattlerSet::of(*state.heldByZinger) : BattlerSet{};
        addImmediateAction(state, MonsterVisibilityAction{.mode = MonsterVisibilityMode::HideAndKeepBattleGoing,
                                                          .monsters = zinger,
                                                          .type = MonsterEntranceType::PRE_DRAWN});
        addImmediateAction(state, MonsterVisibilityAction{.mode = MonsterVisibilityMode::ReviveAtCurrentHp,
                                                          .monsters = zinger,
                                                          .type = MonsterEntranceType::FADE_IN_FROM_TOP});
        state.heldByZinger.reset();
        state.swallowedByZinger.reset();
    }
    Battler& battler = state.at(slot);
    if (const std::optional<BattlerSlot> bodyguard = battler.bodyguard) {
        releaseLoveToken(state, slot, *bodyguard);
    }
    if (const std::optional<BattlerSlot> covered = battler.guarding) {
        releaseLoveToken(state, *covered, slot);
    }
    if (const std::optional<BattlerSlot> charmed = battler.charming) {
        releaseCharm(state, slot, *charmed);
    }
    if (const std::optional<BattlerSlot> charmer = battler.charmedBy) {
        releaseCharm(state, *charmer, slot);
    }
}

}  // namespace ostinato::battle
