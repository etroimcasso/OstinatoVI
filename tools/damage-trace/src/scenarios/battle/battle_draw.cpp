// The battle sweep's draws and the reach table's rows.

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
#include <variant>
#include <vector>

#include "battle/battle_requests.h"
#include "battle/battle_start.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "game/character_record.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_prop_id.h"
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
// One record in this many of the party played becomes Umaro, while no record holds him.
constexpr unsigned kUmaroOneIn = 6;
// One battle in this many holds the run buttons, from a frame up to this one.
constexpr unsigned kRunOneIn = 8;
constexpr std::uint64_t kRunFromMost = 600;
constexpr std::size_t kDances = 8;
constexpr std::size_t kRages = 256;

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

bool holdsUmaro(const game::Party& party) {
    return std::any_of(party.characters.begin(), party.characters.end(),
                       [](const game::CharacterRecord& record) { return record.actor == CharacterPropId::UMARO; });
}

}  // namespace

BattleScenario drawBattleScenario(std::mt19937& rng, const TextCorpus& text, const MonsterSprites& sprites) {
    BattleScenario scenario{.start = drawStartScenario(rng, text, sprites),
                            .model = {},
                            .firstStep = {},
                            .frameCap = kBattleFrameCap};
    game::Party& party = scenario.start.cells.party;

    // A dancer and a rager each have one to choose from.
    if (party.dancesKnown.none()) {
        party.dancesKnown.set(rng() % kDances);
    }
    if (party.ragesKnown.none()) {
        party.ragesKnown.set(rng() % kRages);
    }
    // Each record of the party played: its HP, Umaro, and the part it takes if it is seated.
    std::array<MemberPart, game::kCharacterRecords> parts{};
    parts.fill(MemberPart::Waiting);
    for (std::size_t r = 0; r < game::kCharacterRecords; ++r) {
        if (party.membership[r].party != party.currentParty) {
            continue;
        }
        game::CharacterRecord& record = party.characters[r];
        if (oneIn(rng, 2)) {
            record.hp = std::min<std::uint16_t>(record.hp, static_cast<std::uint16_t>(1 + upTo(rng, kPartyHpMost - 1)));
        }
        if (!scenario.start.start.colosseum && !holdsUmaro(party) && oneIn(rng, kUmaroOneIn)) {
            record.actor = CharacterPropId::UMARO;
            continue;
        }
        parts[r] = static_cast<MemberPart>(rng() % kMemberParts);
        BattleCommandId& slotTaken = record.commands[rng() % record.commands.size()];
        if (parts[r] != MemberPart::Waiting && oneIn(rng, kRageOrDanceOneIn)) {
            slotTaken = BattleCommandId::RAGE;
        } else if (parts[r] == MemberPart::Muddled && oneIn(rng, kRageOrDanceOneIn)) {
            slotTaken = BattleCommandId::DANCE;
        }
    }

    // The battle as it starts, for who is seated and which monsters are there.
    const battle::BattleState started = portStart(scenario.start, text, sprites);
    for (int index = 0; index < battle::kBattlerSlots; ++index) {
        const battle::BattlerSlot slot = battle::BattlerSlot::fromIndex(index);
        const battle::Battler& battler = started.at(slot);
        if (slot.isMonster()) {
            if (battler.monster && battler.hp > 0) {
                scenario.firstStep.hp[static_cast<std::size_t>(index)] =
                    std::min<std::uint16_t>(battler.hp, static_cast<std::uint16_t>(1 + upTo(rng, kMonsterHpMost - 1)));
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
    if (oneIn(rng, kRunOneIn)) {
        scenario.model.runButtonsHeldFrom = rng() % (kRunFromMost + 1);
    }
    return scenario;
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
