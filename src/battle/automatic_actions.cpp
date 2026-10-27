#include "battle/automatic_actions.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "battle/actions.h"
#include "battle/attack.h"
#include "battle/command_menu.h"
#include "battle/commands.h"
#include "data/battle_commands.h"
#include "data/battle_tables.h"
#include "data/dance_properties.h"
#include "data/monster_attacks.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"
#include "ostinato/weapon_flags.h"

namespace ostinato::battle {

namespace {

// Magitek armor offers its first three beams.
constexpr std::uint8_t kMagitekBeams = 3;

// The espers a slot reel's summon can call: all of them but Odin and Raiden, which sit next to
// each other in the esper order.
constexpr int kEsperCount = static_cast<int>(EsperId::PHOENIX) - static_cast<int>(EsperId::RAMUH) + 1;
constexpr int kOdin = static_cast<int>(EsperId::ODIN) - static_cast<int>(EsperId::RAMUH);
constexpr int kSkippedEspers = 2;

// A monster's four Control attacks.
constexpr int kControlAttacks = 4;

// With no rage known, a Rage reads its attacks at the known-rage list's end marker, which as a
// monster number is Pugs.
constexpr MonsterId kNoRageKnownAttacks = MonsterId::PUGS;

constexpr int kBitsInAByte = 8;

AttackId offset(AttackId first, int by) {
    return static_cast<AttackId>(static_cast<int>(first) + by);
}

// One of the set bits of `bits`, each equally likely, as its bit number. With none set, bit 0, and
// nothing is drawn.
int randomSetBit(BattleRng& rng, std::uint8_t bits) {
    int count = 0;
    for (int bit = 0; bit < kBitsInAByte; ++bit) {
        count += (bits >> bit) & 1;
    }
    if (count == 0) {
        return 0;
    }
    int skip = rng.below(static_cast<std::uint8_t>(count));
    for (int bit = 0; bit < kBitsInAByte; ++bit) {
        if (((bits >> bit) & 1) != 0 && skip-- == 0) {
            return bit;
        }
    }
    return 0;
}

// A spell from the party member's list, each equally likely: a mute caster or one with no
// spells has none. The list is counted from its last spell back.
std::optional<AttackId> randomSpell(BattleState& state, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    if (battler.status.has(StatusId::SILENCE) || battler.spellCount == 0) {
        return std::nullopt;
    }
    int skip = state.rng.below(static_cast<std::uint8_t>(battler.spellCount));
    for (auto entry = battler.spells.magic.rbegin(); entry != battler.spells.magic.rend(); ++entry) {
        if (*entry && skip-- == 0) {
            return (*entry)->spell;
        }
    }
    return AttackId{};
}

// A lore the party knows, each equally likely: a mute caster, or a party that knows none, has
// none. The lores are counted from the last back.
std::optional<AttackId> randomLore(BattleState& state, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    if (battler.status.has(StatusId::SILENCE) || state.loresKnown == 0) {
        return std::nullopt;
    }
    int skip = state.rng.below(static_cast<std::uint8_t>(state.loresKnown));
    for (auto entry = battler.spells.lores.rbegin(); entry != battler.spells.lores.rend(); ++entry) {
        if (*entry && skip-- == 0) {
            return (*entry)->spell;
        }
    }
    return AttackId::CONDEMNED;
}

// One of the Bushido techniques learned so far, which only a Bushido weapon in either hand allows.
std::optional<AttackId> randomBushido(BattleState& state, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    if (!battler.weaponHand.weaponFlags.has(WeaponFlags::SWDTECH) &&
        !battler.shieldHand.weaponFlags.has(WeaponFlags::SWDTECH)) {
        return std::nullopt;
    }
    return offset(AttackId::DISPATCH, state.rng.below(static_cast<std::uint8_t>(state.swdtechsKnown)));
}

AttackId randomBlitz(BattleState& state) {
    return offset(AttackId::PUMMEL, randomSetBit(state.rng, state.party.blitzesKnown.bits));
}

AttackId randomMagitekBeam(BattleState& state) {
    return offset(AttackId::FIRE_BEAM, state.rng.below(kMagitekBeams));
}

// One of the tools the party owns, as the item it is.
AttackId randomTool(BattleState& state) {
    const int tool = randomSetBit(state.rng, state.inventory.tools.owned);
    return static_cast<AttackId>(static_cast<int>(ItemId::NOISEBLASTER) + tool);
}

// The attack a command picked at random makes, or nothing when it has none to make. A command
// with no attack of its own makes the attack numbered 0.
std::optional<AttackId> attackFor(BattleState& state, BattlerSlot slot, BattleCommandId command) {
    switch (command) {
    case BattleCommandId::MAGIC:
    case BattleCommandId::X_MAGIC:
        return randomSpell(state, slot);
    case BattleCommandId::BUSHIDO:
        return randomBushido(state, slot);
    case BattleCommandId::BLITZ:
        return randomBlitz(state);
    case BattleCommandId::RAGE:
        return chooseRageAttack(state, slot);
    case BattleCommandId::DANCE:
        return chooseDanceStep(state, slot);
    case BattleCommandId::LORE:
        return randomLore(state, slot);
    case BattleCommandId::MORPH:
        if (state.party.morphSupply < kMorphSupplyToMorph) {
            return std::nullopt;
        }
        return AttackId{};
    case BattleCommandId::MAGITEK:
        return randomMagitekBeam(state);
    case BattleCommandId::TOOLS:
        return randomTool(state);
    default:
        return AttackId{};
    }
}

// Add a command chosen for the battler. The Roulette command takes no advance wait and is not
// aimed; every other command takes both.
void addChosen(BattleState& state, ActionExecutor& executor, BattlerSlot slot, CommandAction command) {
    if (command.command == kRouletteCommand) {
        addAction(state, slot, command);
        return;
    }
    addAutomaticAction(state, executor, slot, command);
}

}  // namespace

CommandAction chooseOwnCommand(BattleState& state, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    const bool berserkRules = battler.status.has(StatusId::BERSERK) ||
                              !(battler.status.has(StatusId::CONFUSE) || battler.charmedBy || state.colosseum);
    std::array<std::optional<CommandAction>, 4> rows{};
    for (int row = static_cast<int>(rows.size()) - 1; row >= 0; --row) {
        const BattleCommandId command = battler.commands[static_cast<std::size_t>(row)].command;
        if (command == BattleCommandId::NONE || !kConfusedAllowedCommands.has(command) ||
            (berserkRules && !kBerserkAllowedCommands.has(command))) {
            continue;
        }
        if (const std::optional<AttackId> attack = attackFor(state, slot, command)) {
            rows[static_cast<std::size_t>(row)] = CommandAction{.command = command, .attack = *attack};
        }
    }
    std::vector<CommandAction> candidates{CommandAction{.command = BattleCommandId::FIGHT}};
    for (int row = static_cast<int>(rows.size()) - 1; row >= 0; --row) {
        if (rows[static_cast<std::size_t>(row)]) {
            candidates.push_back(*rows[static_cast<std::size_t>(row)]);
        }
    }
    return candidates[state.rng.below(static_cast<std::uint8_t>(candidates.size()))];
}

void chooseAutomaticAction(BattleState& state, ActionExecutor& executor, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    if (battler.status.has(StatusId::DANCE)) {
        addChosen(state, executor, slot,
                  CommandAction{.command = BattleCommandId::DANCE, .attack = chooseDanceStep(state, slot)});
        return;
    }
    if (battler.status.has(StatusId::RAGE)) {
        addChosen(state, executor, slot,
                  CommandAction{.command = BattleCommandId::RAGE, .attack = chooseRageAttack(state, slot)});
        return;
    }
    if (battler.status.has(StatusId::MAGITEK)) {
        addChosen(state, executor, slot,
                  CommandAction{.command = BattleCommandId::MAGITEK, .attack = randomMagitekBeam(state)});
        return;
    }
    CommandAction chosen = chooseOwnCommand(state, slot);
    if (chosen.command == BattleCommandId::X_MAGIC) {
        // The party member could pick X-Magic only with a spell to cast, so a second one is there.
        const AttackId first = randomSpell(state, slot).value_or(AttackId{});
        addChosen(state, executor, slot, CommandAction{.command = BattleCommandId::MAGIC, .attack = first});
    }
    if (chosen.command == BattleCommandId::LORE && chosen.attack == AttackId::ROULETTE) {
        chosen.command = kRouletteCommand;
    }
    addChosen(state, executor, slot, chosen);
}

bool choosesItsOwnAttack(const BattleState& state, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    return state.colosseum || battler.charmedBy || battler.status.has(StatusId::BERSERK) ||
           battler.status.has(StatusId::CONFUSE);
}

void chooseMonsterAction(BattleState& state, ActionExecutor& executor, BattlerSlot slot) {
    const Battler& battler = state.at(slot);
    AttackId attack = AttackId::NONE;
    if (battler.monster) {
        const MonsterControl& control = getMonsterControl(*battler.monster);
        const int drawn = state.rng.next() & (kControlAttacks - 1);
        for (int step = 0; step < kControlAttacks; ++step) {
            const int index = (drawn - step + kControlAttacks) % kControlAttacks;
            if (control.attacks[static_cast<std::size_t>(index)] != AttackId::NONE) {
                attack = control.attacks[static_cast<std::size_t>(index)];
                break;
            }
        }
    }
    if (battler.status.has(StatusId::BERSERK)) {
        attack = AttackId::BATTLE;
    }
    addChosen(state, executor, slot, CommandAction{.command = commandForAttack(attack), .attack = attack});
}

AttackId chooseDanceStep(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    if (!battler.dance) {
        std::uint8_t known = 0;
        for (std::size_t dance = 0; dance < state.dancesKnown.size(); ++dance) {
            if (state.dancesKnown[dance]) {
                known = static_cast<std::uint8_t>(known | (1u << dance));
            }
        }
        battler.dance = static_cast<DanceId>(randomSetBit(state.rng, known));
    }
    const std::uint8_t roll = state.rng.next();
    std::size_t step = 0;
    for (const DanceStepThresholdEntry& threshold : kDanceStepThresholds) {
        if (roll < threshold.threshold.value()) {
            ++step;
        }
    }
    return getDanceProperties(*battler.dance).attacks[step];
}

AttackId chooseRageAttack(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    MonsterId attacksOf{};
    if (battler.rage) {
        attacksOf = *battler.rage;
    } else {
        const std::size_t pick = state.rng.below(static_cast<std::uint8_t>(state.ragesKnown.size()));
        if (pick < state.ragesKnown.size()) {
            battler.rage = state.ragesKnown[pick];
            attacksOf = *battler.rage;
        } else {
            // With no rage known, the party member rages as the first monster.
            battler.rage = MonsterId{};
            attacksOf = kNoRageKnownAttacks;
        }
    }
    return getMonsterRage(attacksOf).attacks[state.rng.coin() ? 1 : 0];
}

AttackId chooseRandomEsper(BattleState& state) {
    int esper = state.rng.below(static_cast<std::uint8_t>(kEsperCount - kSkippedEspers));
    if (esper >= kOdin) {
        esper += kSkippedEspers;
    }
    return offset(AttackId::RAMUH, esper);
}

}  // namespace ostinato::battle
