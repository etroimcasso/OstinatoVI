// The battle sweep's draws, aimed and plain, and the reach table's rows.

#include "scenarios/battle/battle_draw.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "battle/battle_requests.h"
#include "battle/battle_script.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "battle/commands.h"
#include "data/attack_properties.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"

#include "scenarios/battle/start.h"
#include "scenarios/battle/start_draw.h"
#include "scenarios/common/draws.h"

namespace ostinato::trace {

using namespace detail;

namespace {

// A member's HP cut to at most this, one time in two, so the monsters can end a battle.
constexpr int kPartyHpMost = 300;
// A monster's HP cut to at most this.
constexpr int kMonsterHpMost = 400;
// A monster's HP cut to at most this in a battle aimed at a win: Gau's and Shadow's.
constexpr int kAimedWinMonsterHpMost = 100;
// One record in this many of the party played becomes Umaro, while no record holds him.
constexpr unsigned kUmaroOneIn = 6;
// One battle in this many holds the run buttons, from a frame up to this one.
constexpr unsigned kRunOneIn = 8;
constexpr std::uint64_t kRunFromMost = 600;
constexpr std::size_t kDances = 8;
constexpr std::size_t kRages = 256;
// A model with no story countdown of its own is given one running out on a frame in this range, so every
// drawn battle ends by the frame cap's time at the latest.
constexpr std::uint64_t kCountdownFrom = 8'000;
constexpr std::uint64_t kCountdownSpread = 4'000;
// The drawn battle switches hold the field's countdown run out ($1DD1 bit 5) half the time, which ends a
// battle at its first end check; all but one battle in this many has it cleared.
constexpr unsigned kCountdownAlreadyOutOneIn = 8;
// drawBattle() aims one battle in this many at each of kSweepAims.
constexpr unsigned kAimOneIn = 10;
constexpr std::array<BattleAim, 3> kSweepAims{BattleAim::Dancing, BattleAim::GauTurnsUp, BattleAim::ShadowWalksOff};
// The spell a battle aimed at Terra's event teaches her and gives her the MP for.
constexpr AttackId kTerrasSpell = AttackId::FIRE;

// The part a seated member who is not Umaro and not in the colosseum takes. A Berserk or Muddled member
// holds Rage among their commands one time in three, and a Muddled one Dance one time in three more, so
// the battle's own choosers start a rage or a dance for them (ConfusedCmdTbl, BerserkCmdTbl).
enum class MemberPart : std::uint8_t { Berserk, Muddled, Waiting };
constexpr unsigned kMemberParts = 3;
constexpr unsigned kRageOrDanceOneIn = 3;

constexpr std::array<std::string_view, 15> kReachRows{{
    "a Berserk member",
    "a Muddled member",
    "a dancing member",
    "a raging member",
    "Umaro",
    "a member whose menu is never answered",
    "the colosseum",
    "won",
    "lost",
    "fled",
    "a final stage scrolled on",
    "Gau turned up",
    "Shadow walked off",
    "a story countdown ran out",
    "an ending a battle event or the story chose",
}};

enum ReachRow : std::size_t {
    kBerserkRow,
    kMuddledRow,
    kDancingRow,
    kRagingRow,
    kUmaroRow,
    kWaitingRow,
    kColosseumRow,
    kWonRow,
    kLostRow,
    kFledRow,
    kFinalStageRow,
    kGauRow,
    kShadowRow,
    kCountdownRow,
    kOtherEndingRow,
};

constexpr std::string_view kTerrasEventName = "Terra's Magic and her event";

bool holds(const game::Party& party, CharacterPropId actor) {
    return std::any_of(party.characters.begin(), party.characters.end(),
                       [actor](const game::CharacterRecord& record) { return record.actor == actor; });
}

bool played(const game::Party& party, std::size_t record) {
    return party.membership[record].party == party.currentParty;
}

std::optional<std::size_t> firstPlayed(const game::Party& party) {
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        if (played(party, r)) {
            return r;
        }
    }
    return std::nullopt;
}

// The record holding `actor`, in the party played and standing: the record already there, or the record
// holding them swapped into the first record played's place in the party (that record leaving it), or,
// when no record holds them, the first record played made them. Nothing when no record is played. Each
// actor stays in one record, so a scripted setup still finds its cast.
std::optional<std::size_t> bringIntoTheParty(game::Party& party, CharacterPropId actor) {
    const std::optional<std::size_t> first = firstPlayed(party);
    if (!first) {
        return std::nullopt;
    }
    std::size_t record = *first;
    const auto held = std::find_if(party.characters.begin(), party.characters.end(),
                                   [actor](const game::CharacterRecord& r) { return r.actor == actor; });
    if (held == party.characters.end()) {
        party.characters[record].actor = actor;
    } else {
        record = static_cast<std::size_t>(held - party.characters.begin());
        if (!played(party, record)) {
            std::swap(party.membership[record], party.membership[*first]);
        }
    }
    game::CharacterRecord& standing = party.characters[record];
    for (const StatusId fallen : {StatusId::DEAD, StatusId::PETRIFY, StatusId::ZOMBIE}) {
        standing.status.clear(fallen);
    }
    standing.maxHp = std::max<std::uint16_t>(standing.maxHp, 1);
    standing.hp = std::max<std::uint16_t>(standing.hp, 1);
    return record;
}

// Terra given Magic and a spell she can pay for.
void giveTerraHerSpell(game::Party& party, game::CharacterRecord& terra, std::mt19937& rng) {
    if (std::find(terra.commands.begin(), terra.commands.end(), BattleCommandId::MAGIC) == terra.commands.end()) {
        terra.commands[rng() % terra.commands.size()] = BattleCommandId::MAGIC;
    }
    terra.status.clear(StatusId::MAGITEK);
    party.spells[static_cast<std::size_t>(CharacterPropId::TERRA)].progress[static_cast<std::size_t>(kTerrasSpell)] =
        game::SpellProgress::kSpellLearned;
    const std::uint16_t cost = getAttackProperties(kTerrasSpell).mpCost;
    terra.mp = std::max(terra.mp, cost);
    terra.maxMp = std::max(terra.maxMp, terra.mp);
}

// One battle drawn whole from `rng`, given what `aim` needs.
BattleScenario drawOnce(std::mt19937& rng, const TextCorpus& text, const MonsterSprites& sprites, BattleAim aim) {
    BattleScenario scenario{.start = drawStartScenario(rng, text, sprites),
                            .model = {},
                            .firstStep = {},
                            .frameCap = kBattleFrameCap};
    game::Party& party = scenario.start.cells.party;
    const bool colosseum = scenario.start.start.colosseum.has_value();

    // A dancer and a rager each have one to choose from.
    if (party.dancesKnown.none()) {
        party.dancesKnown.set(rng() % kDances);
    }
    if (party.ragesKnown.none()) {
        party.ragesKnown.set(rng() % kRages);
    }
    // The field's countdown already run out, kept one battle in kCountdownAlreadyOutOneIn.
    if (!oneIn(rng, kCountdownAlreadyOutOneIn)) {
        scenario.start.switches.clear(game::kTimerExpiredSwitch);
    }
    // A battle that seats Shadow, free to walk off.
    const bool shadowBattle = aim == BattleAim::ShadowWalksOff && !colosseum &&
                              bringIntoTheParty(party, CharacterPropId::SHADOW).has_value();
    if (shadowBattle) {
        scenario.start.start.shadowAvailable = true;
        scenario.start.switches.clear(game::kShadowStaysSwitch);
    }
    // A battle aimed at a win: every member Berserk and the monsters weaker.
    const bool aimedAtAWin = shadowBattle || (aim == BattleAim::GauTurnsUp && scenario.start.start.gauMayAppear);
    // The record a dance or Terra's event is aimed through.
    std::optional<std::size_t> aimedRecord;
    if (aim == BattleAim::Dancing) {
        aimedRecord = firstPlayed(party);
    } else if (aim == BattleAim::TerrasMagicEvent && !colosseum) {
        aimedRecord = bringIntoTheParty(party, CharacterPropId::TERRA);
        if (aimedRecord) {
            scenario.start.switches.set(game::kTerraMagicSceneSwitch);
        }
    }

    // Each record of the party played: its HP, Umaro or Shadow, and the part it takes if it is seated.
    std::array<MemberPart, game::kCharacterRecords> parts{};
    parts.fill(MemberPart::Waiting);
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        if (!played(party, r)) {
            continue;
        }
        game::CharacterRecord& record = party.characters[r];
        if (oneIn(rng, 2)) {
            record.hp = std::min<std::uint16_t>(record.hp, static_cast<std::uint16_t>(1 + upTo(rng, kPartyHpMost - 1)));
        }
        if (!aimedAtAWin && r != aimedRecord && !colosseum && !holds(party, CharacterPropId::UMARO) &&
            oneIn(rng, kUmaroOneIn)) {
            record.actor = CharacterPropId::UMARO;
            continue;
        }
        BattleCommandId& slotTaken = record.commands[rng() % record.commands.size()];
        if (aimedAtAWin) {
            parts[r] = MemberPart::Berserk;
        } else if (r == aimedRecord) {
            parts[r] = MemberPart::Muddled;
        } else {
            parts[r] = static_cast<MemberPart>(rng() % kMemberParts);
        }
        if (r == aimedRecord && aim == BattleAim::Dancing) {
            slotTaken = BattleCommandId::DANCE;
            std::replace(record.commands.begin(), record.commands.end(), BattleCommandId::RAGE, BattleCommandId::FIGHT);
        } else if (r == aimedRecord && aim == BattleAim::TerrasMagicEvent) {
            giveTerraHerSpell(party, record, rng);
        } else if (parts[r] != MemberPart::Waiting && oneIn(rng, kRageOrDanceOneIn)) {
            slotTaken = BattleCommandId::RAGE;
        } else if (parts[r] == MemberPart::Muddled && oneIn(rng, kRageOrDanceOneIn)) {
            slotTaken = BattleCommandId::DANCE;
        }
    }

    // No Chain Saw in a battle with a Muddled member or a colosseum match, whose fighter picks commands the
    // same way (RandCharAction :656-745, Tools among ConfusedCmdTbl's): its roll is read past the random
    // numbers' table on the cartridge whenever the byte above their position is not zero (Rand :12378-12384
    // with the index registers wide, from ToolsEffect_03 :7127), which the port does not reproduce.
    if (colosseum || std::find(parts.begin(), parts.end(), MemberPart::Muddled) != parts.end()) {
        for (game::InventorySlot& slot : party.inventory) {
            if (slot.item == ItemId::CHAIN_SAW) {
                slot = game::InventorySlot{.item = ItemId::EMPTY, .quantity = 0};
            }
        }
    }

    // The battle as it starts, for who is seated and which monsters are there.
    const battle::BattleState started = portStart(scenario.start, text, sprites);
    const int monsterHpMost = aimedAtAWin ? kAimedWinMonsterHpMost : kMonsterHpMost;
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        const battle::Battler& battler = started.at(slot);
        if (slot.isMonster()) {
            if (battler.monster && battler.hp > 0) {
                scenario.firstStep.hp[static_cast<std::size_t>(index)] =
                    std::min<std::uint16_t>(battler.hp, static_cast<std::uint16_t>(1 + upTo(rng, monsterHpMost - 1)));
            }
            continue;
        }
        if (!battler.actor || !battler.record || started.colosseum || *battler.actor == CharacterPropId::UMARO) {
            continue;
        }
        StatusSet& added = scenario.firstStep.statusesAdded[static_cast<std::size_t>(index)];
        switch (parts[*battler.record]) {
            case MemberPart::Berserk: added.set(StatusId::BERSERK); break;
            case MemberPart::Muddled: added.set(StatusId::CONFUSE); break;
            case MemberPart::Waiting: break;
        }
    }

    scenario.model = drawPresentationModel(rng(), scenario.frameCap);
    if (!scenario.model.timerRunsOutAt) {
        scenario.model.timerRunsOutAt = kCountdownFrom + rng() % (kCountdownSpread + 1);
    }
    if (oneIn(rng, kRunOneIn)) {
        scenario.model.runButtonsHeldFrom = rng() % (kRunFromMost + 1);
    }
    return scenario;
}

// Whether the port's own battle over `scenario` reaches `aim`.
bool portReaches(const BattleScenario& scenario, BattleAim aim, const TextCorpus& text, const MonsterSprites& sprites) {
    BattleCheck check;
    check.port = portBattle(scenario, text, sprites);
    if (aim == BattleAim::TerrasMagicEvent) {
        return std::any_of(check.port.requests.begin(), check.port.requests.end(), [](const battle::BattleRequest& request) {
            const auto* script = std::get_if<battle::RunBattleScript>(&request);
            return script != nullptr &&
                   std::any_of(script->commands.begin(), script->commands.end(), [](const battle::BattleScriptCommand& command) {
                       const auto* event = std::get_if<battle::BattleEventPlays>(&command);
                       return event != nullptr && event->event == battle::kBattleEventTerraMagicScene;
                   });
        });
    }
    const std::vector<std::string_view> reached = battleReached(scenario, check);
    return std::find(reached.begin(), reached.end(), aimName(aim)) != reached.end();
}

}  // namespace

AimedBattle drawAimedBattleScenario(std::mt19937& rng, const TextCorpus& text, const MonsterSprites& sprites,
                                    BattleAim aim) {
    AimedBattle drawn{.aim = aim};
    if (aim == BattleAim::None) {
        drawn.scenario = drawOnce(rng, text, sprites, aim);
        return drawn;
    }
    for (int tried = 0; tried < kAimTries; ++tried) {
        drawn.scenario = drawOnce(rng, text, sprites, aim);
        if (portReaches(drawn.scenario, aim, text, sprites)) {
            drawn.reached = true;
            return drawn;
        }
    }
    return drawn;
}

AimedBattle drawBattle(std::mt19937& rng, const TextCorpus& text, const MonsterSprites& sprites) {
    const unsigned pick = rng() % kAimOneIn;
    const BattleAim aim = pick < kSweepAims.size() ? kSweepAims[pick] : BattleAim::None;
    return drawAimedBattleScenario(rng, text, sprites, aim);
}

BattleScenario drawBattleScenario(std::mt19937& rng, const TextCorpus& text, const MonsterSprites& sprites) {
    return drawBattle(rng, text, sprites).scenario;
}

std::string_view aimName(BattleAim aim) {
    switch (aim) {
        case BattleAim::None: return "nothing";
        case BattleAim::Dancing: return kReachRows[kDancingRow];
        case BattleAim::GauTurnsUp: return kReachRows[kGauRow];
        case BattleAim::ShadowWalksOff: return kReachRows[kShadowRow];
        case BattleAim::TerrasMagicEvent: return kTerrasEventName;
    }
    return "nothing";
}

std::string describe(const BattleScenario& scenario) {
    std::string line = describe(scenario.start) + ", members";
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const StatusSet& added = scenario.firstStep.statusesAdded[static_cast<std::size_t>(position)];
        line += added.has(StatusId::BERSERK)   ? " berserk"
                : added.has(StatusId::CONFUSE) ? " muddled"
                                               : " -";
    }
    if (scenario.model.runButtonsHeldFrom) {
        line += ", running from frame " + std::to_string(*scenario.model.runButtonsHeldFrom);
    }
    return line;
}

std::span<const std::string_view> battleReachRows() { return kReachRows; }

std::vector<std::string_view> battleReached(const BattleScenario& scenario, const BattleCheck& check) {
    std::vector<std::string_view> reached;
    const auto reach = [&](ReachRow row) { reached.push_back(kReachRows[row]); };
    const PortBattle& port = check.port;
    if (port.steps.empty()) {
        return reached;
    }
    const battle::BattleState& first = port.steps.front();
    bool waiting = false;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        const battle::Battler& member = first.at(battle::BattlerSlot::character(position));
        const StatusSet& added = scenario.firstStep.statusesAdded[static_cast<std::size_t>(position)];
        if (!member.actor || first.colosseum) {
            continue;
        }
        if (*member.actor == CharacterPropId::UMARO) {
            reach(kUmaroRow);
        } else if (added.has(StatusId::BERSERK)) {
            reach(kBerserkRow);
        } else if (added.has(StatusId::CONFUSE)) {
            reach(kMuddledRow);
        } else {
            waiting = true;
        }
    }
    if (waiting) reach(kWaitingRow);
    if (first.colosseum) reach(kColosseumRow);
    // A dance or a rage the battle's choosers began for a member.
    const auto someMember = [&](StatusId status) {
        return std::any_of(port.steps.begin(), port.steps.end(), [&](const battle::BattleState& state) {
            for (int position = 0; position < battle::kPartySlots; ++position) {
                if (state.at(battle::BattlerSlot::character(position)).status.has(status)) {
                    return true;
                }
            }
            return false;
        });
    };
    if (someMember(StatusId::DANCE)) reach(kDancingRow);
    if (someMember(StatusId::RAGE)) reach(kRagingRow);

    if (std::any_of(port.requests.begin(), port.requests.end(), [](const battle::BattleRequest& request) {
            return std::holds_alternative<battle::ScrollFinalBattleBackground>(request);
        })) {
        reach(kFinalStageRow);
    }
    if (std::any_of(port.steps.begin(), port.steps.end(), [](const battle::BattleState& state) { return state.gauJoining; })) {
        reach(kGauRow);
    }
    if (!port.result) {
        return reached;
    }
    if (port.result->shadowLeftParty) reach(kShadowRow);
    const battle::BattleOutcome& outcome = port.result->outcome;
    if (std::holds_alternative<battle::BattleWon>(outcome)) {
        reach(kWonRow);
    } else if (std::holds_alternative<battle::BattleLost>(outcome)) {
        reach(kLostRow);
    } else {
        const battle::BattleEndReason reason = std::get<battle::BattleEnded>(outcome).reason;
        if (reason == battle::BattleEndReason::TimerExpired) {
            reach(kCountdownRow);
        } else if (reason == battle::BattleEndReason::RanOutOfTime && scenario.model.runButtonsHeldFrom) {
            reach(kFledRow);
        } else if (reason != battle::BattleEndReason::FinalBattleNextStage) {
            reach(kOtherEndingRow);
        }
    }
    return reached;
}

}  // namespace ostinato::trace
