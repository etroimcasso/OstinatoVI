#include "battle/player_intake.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "battle/action_executor.h"
#include "battle/actions.h"
#include "battle/battle_state.h"
#include "data/battle_commands.h"
#include "data/battle_tables.h"

namespace ostinato::battle {

namespace {

// The slot reel outcomes 0 and 1 are the two Joker Dooms, which carry their own targets.
constexpr std::size_t kJokerDoomOutcomes = 2;

// The menu numbers the eight blitzes 0-7.
constexpr int kBlitzes = 8;

// Settle what one chosen command will do: the attack it comes down to, and whether it keeps its
// targets. `command` is the command as it runs, which for X-Magic's first spell is Magic.
CommandAction settleCommand(BattleState& state, ActionExecutor& executor, BattlerSlot slot, BattleCommandId command,
                            AttackId attack, BattlerSet targets) {
    Battler& battler = state.at(slot);
    switch (command) {
        case BattleCommandId::SUMMON:
            if (attack == AttackId::NONE && battler.spells.esper) {
                attack = static_cast<AttackId>(static_cast<int>(*battler.spells.esper) -
                                               static_cast<int>(EsperId::RAMUH));
            }
            state.espersSummoned.add(slot);
            break;
        case BattleCommandId::ITEM:
        case BattleCommandId::THROW:
            battler.itemInHand = static_cast<ItemId>(attack);
            break;
        case BattleCommandId::SLOT: {
            const auto outcome = static_cast<std::size_t>(attack);
            attack = kSlotOutcomes[outcome].attack;
            if (outcome < kJokerDoomOutcomes) {
                BattleSlotMask& side = outcome == 0 ? targets.characters : targets.monsters;
                side = kJokerDoomTargets[outcome].targets;
                for (int position = 0; position < kPartySlots; ++position) {
                    if (state.at(BattlerSlot::character(position)).actsAsEnemy) {
                        targets.characters.bits = static_cast<std::uint8_t>(targets.characters.bits ^ (1u << position));
                    }
                }
            }
            if (attack == AttackId::NONE) {
                attack = executor.chooseRandomEsper(state, slot, attack);
            }
            break;
        }
        case BattleCommandId::DANCE:
            battler.dance = attack == AttackId::NONE ? std::nullopt
                                                     : std::optional<DanceId>(static_cast<DanceId>(attack));
            state.lastDance = battler.dance;
            attack = executor.chooseDanceStep(state, slot, attack);
            break;
        case BattleCommandId::RAGE:
            battler.rage = attack == AttackId::NONE ? std::nullopt
                                                    : std::optional<MonsterId>(static_cast<MonsterId>(attack));
            attack = executor.chooseRageAttack(state, slot, attack);
            break;
        case BattleCommandId::BLITZ: {
            const int blitz = static_cast<int>(attack);
            if (blitz < kBlitzes && !state.party.blitzesKnown.has(static_cast<std::uint8_t>(blitz))) {
                attack = AttackId::NONE;
            }
            break;
        }
        default:
            break;
    }

    // Summon, Lore, Magitek, Blitz and Bushido count their attacks from a base. One that runs past
    // the last attack is a plain attack.
    for (const CommandAttackBase& base : kCommandAttackBases) {
        if (base.command == command) {
            const int counted = static_cast<int>(attack) + static_cast<int>(base.attackBase);
            attack = counted > static_cast<int>(AttackId::NONE) ? AttackId::BATTLE : static_cast<AttackId>(counted);
        }
    }

    if (kRetargetCommands.has(command)) {
        targets = {};
    }
    // A confused character, or a zombie, picks its own target when it acts.
    if (battler.status.has(StatusId::ZOMBIE) || battler.status.has(StatusId::CONFUSE)) {
        targets = {};
    }
    return CommandAction{.command = command, .attack = attack, .targets = targets};
}

void takePlayerAction(BattleState& state, ActionExecutor& executor, const PlayerAction& chosen) {
    const BattlerSlot slot = chosen.character;
    Battler& battler = state.at(slot);
    joinAdvanceWaitLine(state, slot);
    battler.inBattleMenu = false;
    battler.menuMayOpen = false;

    const bool xMagic = chosen.command == BattleCommandId::X_MAGIC;
    const BattleCommandId first = xMagic ? BattleCommandId::MAGIC : chosen.command;
    addAction(state, slot, settleCommand(state, executor, slot, first, chosen.attack, chosen.targets));
    if (!xMagic) {
        return;
    }
    // X-Magic's second spell takes none of the first's adjustments but the confused/zombie rule.
    BattlerSet secondTargets = chosen.secondTargets;
    if (battler.status.has(StatusId::ZOMBIE) || battler.status.has(StatusId::CONFUSE)) {
        secondTargets = {};
    }
    addAction(state, slot,
              CommandAction{.command = BattleCommandId::X_MAGIC, .attack = chosen.secondAttack,
                            .targets = secondTargets});
}

}  // namespace

void takePlayerActions(BattleState& state, ActionExecutor& executor) {
    while (!state.chosenActions.empty()) {
        const PlayerAction chosen = std::move(state.chosenActions.front());
        state.chosenActions.pop_front();
        takePlayerAction(state, executor, chosen);
    }
}

}  // namespace ostinato::battle
