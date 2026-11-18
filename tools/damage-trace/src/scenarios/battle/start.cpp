// The port's start over a scenario, and the differences between two start scenarios.

#include "scenarios/battle/start.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "battle/battle_hooks.h"
#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/command_executor.h"
#include "battle/items_obtained.h"
#include "battle/items_obtained.h"
#include "data/monster_sprites.h"
#include "data/text_corpus.h"
#include "game/battle_switches.h"
#include "game/character_record.h"

#include "scenarios/ending/rewards_shared.h"

namespace ostinato::trace {

using namespace detail;

std::string differences(const StartScenario& a, const StartScenario& b) {
    std::string lines;
    const auto field = [&](const char* name, long ours, long theirs) {
        if (ours != theirs) {
            lines += std::string{name} + " " + std::to_string(ours) + " / " + std::to_string(theirs) + "\n";
        }
    };
    const battle::BattleStart& x = a.start;
    const battle::BattleStart& y = b.start;
    field("formation", x.formation.raw, y.formation.raw);
    field("background", static_cast<long>(x.background), static_cast<long>(y.background));
    field("Gau may appear", x.gauMayAppear, y.gauMayAppear);
    field("on the Veldt", x.onTheVeldt, y.onTheVeldt);
    field("field music plays on", x.keepFieldMusic, y.keepFieldMusic);
    field("Gau recruited", x.gauRecruited, y.gauRecruited);
    field("Shadow available", x.shadowAvailable, y.shadowAvailable);
    field("play clock's frames", x.playTimeFrames, y.playTimeFrames);
    field("frame count", x.frameCount, y.frameCount);
    for (std::size_t place = 0; place < x.itemsObtainedTail.size(); ++place) {
        const battle::ItemRecord ours = battle::recordOf(x.itemsObtainedTail[place]);
        const battle::ItemRecord theirs = battle::recordOf(y.itemsObtainedTail[place]);
        for (std::size_t byte = 0; byte < ours.size(); ++byte) {
            const std::string name = "items obtained tail " + std::to_string(place) + " byte " + std::to_string(byte);
            field(name.c_str(), ours[byte], theirs[byte]);
        }
    }
    field("wait mode", x.config.waitMode, y.config.waitMode);
    field("battle speed", x.config.speedSetting, y.config.speedSetting);
    field("gauge shown", x.config.gaugeShown, y.config.gaugeShown);
    field("spell order", x.config.magicOrder, y.config.magicOrder);
    field("a colosseum match", x.colosseum.has_value(), y.colosseum.has_value());
    if (x.colosseum && y.colosseum) {
        field("wager", static_cast<long>(x.colosseum->wager), static_cast<long>(y.colosseum->wager));
        field("prize", static_cast<long>(x.colosseum->prize), static_cast<long>(y.colosseum->prize));
        field("opponent", static_cast<long>(x.colosseum->opponent), static_cast<long>(y.colosseum->opponent));
        field("fighter", static_cast<long>(x.colosseum->fighter), static_cast<long>(y.colosseum->fighter));
    }
    field("final battle's line", static_cast<long>(x.finalBattleOrder.size()), static_cast<long>(y.finalBattleOrder.size()));
    for (std::size_t i = 0; i < std::min(x.finalBattleOrder.size(), y.finalBattleOrder.size()); ++i) {
        field("final battle's line place", static_cast<long>(x.finalBattleOrder[i]), static_cast<long>(y.finalBattleOrder[i]));
    }
    for (std::size_t i = 0; i < game::kBattleVariables; ++i) {
        field("battle switches byte", a.switches.variables[i], b.switches.variables[i]);
    }
    const std::vector<PartyCell> ours = partyImage(a.cells);
    const std::vector<PartyCell> theirs = partyImage(b.cells);
    for (std::size_t i = 0; i < std::min(ours.size(), theirs.size()); ++i) {
        if (ours[i].value != theirs[i].value) {
            lines += partyCellName(ours[i].at) + " " + std::to_string(ours[i].value) + " / " +
                     std::to_string(theirs[i].value) + "\n";
        }
    }
    const game::Party& p = a.cells.party;
    const game::Party& q = b.cells.party;
    field("party played", p.currentParty, q.currentParty);
    for (std::size_t slot = 0; slot < game::kInventorySlots; ++slot) {
        field("inventory item", static_cast<long>(p.inventory[slot].item), static_cast<long>(q.inventory[slot].item));
        field("inventory quantity", p.inventory[slot].quantity, q.inventory[slot].quantity);
    }
    field("steps", p.steps, q.steps);
    field("hours at the last save", p.playTimeAtLastSave.hours, q.playTimeAtLastSave.hours);
    field("minutes at the last save", p.playTimeAtLastSave.minutes, q.playTimeAtLastSave.minutes);
    return lines;
}

battle::BattleState portStart(const StartScenario& scenario, const TextCorpus& text, const MonsterSprites& sprites) {
    battle::BattleHooks hooks;
    battle::CommandExecutor executor{hooks, text, sprites};
    return battle::startBattle(scenario.start, scenario.cells.party, scenario.switches, text, sprites, executor);
}

}  // namespace ostinato::trace
