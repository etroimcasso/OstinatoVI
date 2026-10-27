#include "battle/command_menu.h"

#include "data/battle_commands.h"
#include "ostinato/battle_command_flags.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "ostinato/weapon_flags.h"

namespace ostinato::battle {

namespace {

bool eitherHandHas(const Battler& battler, WeaponFlags flag) {
    return battler.weaponHand.weaponFlags.has(flag) || battler.shieldHand.weaponFlags.has(flag);
}

// Whether a command the party member holds can be chosen, by the command's own rule. Fight and
// Capture also take the Offering's targeting here.
bool commandUsable(const BattleState& state, const Battler& battler, MenuCommand& row) {
    switch (row.command) {
    case BattleCommandId::MORPH:
        return state.party.morphSupply >= kMorphSupplyToMorph;
    case BattleCommandId::RUNIC:
        return eitherHandHas(battler, WeaponFlags::RUNIC);
    case BattleCommandId::BUSHIDO:
        return eitherHandHas(battler, WeaponFlags::SWDTECH);
    case BattleCommandId::LORE:
    case BattleCommandId::X_MAGIC:
    case BattleCommandId::MAGIC:
        return !battler.status.has(StatusId::SILENCE);
    case BattleCommandId::CAPTURE:
    case BattleCommandId::FIGHT:
        if (battler.relicEffects4.has(RelicEffect4::FIGHT_TO_X_FIGHT)) {
            row.targeting = kOfferingTargeting;
        }
        return true;
    default:
        return true;
    }
}

}  // namespace

void refreshCommandMenu(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    state.menusToRefresh.add(slot);
    const bool imp = battler.status.has(StatusId::IMP);
    for (MenuCommand& row : battler.commands) {
        if (row.command == BattleCommandId::NONE) {
            row.usable = false;
        } else if (imp && !battleCommandProperties(row.command).flags.has(BattleCommandFlags::IMP)) {
            row.usable = false;
        } else {
            row.usable = commandUsable(state, battler, row);
        }
    }
}

}  // namespace ostinato::battle
