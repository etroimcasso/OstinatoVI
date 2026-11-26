// A whole battle checked: the cartridge's against the port's, step by step — the state at each loop
// step's frame wait with the write-set and carried-read gates, the requests each step makes, and what the
// game keeps at the end.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "battle/battle_requests.h"
#include "battle/battler_set.h"
#include "battle/items_obtained.h"
#include "battle/party_setup.h"

#include "cells/battle/state.h"
#include "cells/status/effects.h"

namespace ostinato::trace {

namespace {

std::string named(const std::string& what, const std::string& lines) {
    return lines.empty() ? std::string{} : what + ":\n" + lines;
}

std::string readLine(const char* what, const UnwrittenRead& read) {
    char text[96];
    std::snprintf(text, sizeof text, "%s $%02X:%04X (%02X)\n", what, (read.at >> 16) & 0xFF, read.at & 0xFFFF,
                  read.value);
    return text;
}

// The top of each of the stack's two pages, in hex from offset $E0: where the returns of the routines
// running sit.
std::string stackTops(const std::vector<std::uint8_t>& pages) {
    constexpr std::size_t kPage = 0x100;
    constexpr std::size_t kFrom = 0xE0;
    std::string text;
    for (std::size_t page = 0; page + kPage <= pages.size(); page += kPage) {
        text += page == 0 ? "" : " | ";
        for (std::size_t at = kFrom; at < kPage; ++at) {
            char byte[4];
            std::snprintf(byte, sizeof byte, "%02X ", pages[page + at]);
            text += byte;
        }
    }
    return text;
}

// The requests from `first` to `last` (none past the end) as each side shows them; on the port's side, a
// win's celebration as the cartridge shows it when `won`.
std::vector<std::string> portShown(const PortBattle& port, std::size_t first, std::size_t last, const TextCorpus& text,
                                   bool won = false) {
    const std::size_t end = std::min(last, port.requests.size());
    if (first >= end) {
        return {};
    }
    return requestsShown(std::span<const battle::BattleRequest>(port.requests).subspan(first, end - first), won, text);
}

// The graphics calls from `first` to `last` as the port's requests show them. In a battle's `end`, an item
// added once the wrap-up has merged the items obtained ($0A) — a drop, the colosseum's prize — is merged at
// once and shows only its message, as the ending's own check has it (guestCalls() in
// oracle/ending/ending.cpp): the port's end obtains and merges it with no request.
std::vector<std::string> cartridgeShown(const WholeBattleResult& cartridge, std::size_t first, std::size_t last,
                                        const TextCorpus& text, bool end = false) {
    std::vector<std::string> shown;
    bool merged = false;
    for (std::size_t i = first; i < std::min(last, cartridge.calls.size()); ++i) {
        const GraphicsCall& call = cartridge.calls[i];
        if (end && call.command == graphics::kMergeItemsObtained) {
            merged = true;
        }
        if (end && merged && call.command == graphics::kAddItemObtained) {
            continue;
        }
        const auto script = cartridge.scripts.find(i);
        const std::span<const ScriptCommandRead> played =
            script == cartridge.scripts.end() ? std::span<const ScriptCommandRead>{} : std::span{script->second};
        if (const std::optional<std::string> shownCall = callShown(call, played, text)) {
            shown.push_back(*shownCall);
        }
    }
    return shown;
}

std::string requestDifferences(const std::vector<std::string>& port, const std::vector<std::string>& cartridge) {
    std::string lines;
    for (std::size_t i = 0; i < std::max(port.size(), cartridge.size()); ++i) {
        const std::string ours = i < port.size() ? port[i] : "nothing";
        const std::string theirs = i < cartridge.size() ? cartridge[i] : "nothing";
        if (ours != theirs) {
            lines += "request " + std::to_string(i) + ": port " + ours + " / cartridge " + theirs + "\n";
        }
    }
    return lines;
}

// A boost as the two top bits of a record's maximum hold it (CheckHPBoost :2456-2472, read by
// CalcMaxHPMP): 1 for 25% more, 2 for 50%, 3 for 12.5%.
std::uint8_t boostBits(battle::MaxBoost boost) {
    switch (boost) {
        case battle::MaxBoost::None: return 0;
        case battle::MaxBoost::Plus25Percent: return 1;
        case battle::MaxBoost::Plus50Percent: return 2;
        case battle::MaxBoost::Plus12Point5Percent: return 3;
    }
    return 0;
}

// The records of every party member seated at a loop step up to `steps`: the start, and each stage of the
// final battle, refreshes each one for its equipment (InitChars :6201-6222, UpdateEquip :2349-2362).
std::set<std::size_t> refreshedRecords(const PortBattle& port, std::size_t steps) {
    std::set<std::size_t> records;
    for (std::size_t step = 0; step < std::min(steps, port.steps.size()); ++step) {
        const battle::BattleState& state = port.steps[step];
        for (int position = 0; position < battle::kPartySlots; ++position) {
            if (const std::optional<std::size_t> record = state.at(battle::BattlerSlot::character(position)).record) {
                records.insert(*record);
            }
        }
    }
    return records;
}

// The records' cells at a loop step: a record seated at that step or any before it holds the boosts its
// equipment gives in the top two bits of its maximum HP and MP; every other record keeps the bits the battle
// came in with.
PartyCells boostsAtStep(const PartyCells& start, const PortBattle& port, std::size_t step) {
    PartyCells cells = start;
    for (const std::size_t record : refreshedRecords(port, step + 1)) {
        const battle::DerivedStats derived = battle::deriveEquipment(port.steps[step].party.characters[record]);
        cells.hpBoostBits[record] = boostBits(derived.hpBoost);
        cells.mpBoostBits[record] = boostBits(derived.mpBoost);
    }
    return cells;
}

// Whether a final-battle stage starts between the frame wait of the step before `step` and its own: the
// stage's start runs InitBattle again inside that window (RestartBattle :63-64), so its reads are a
// start's.
bool aStageStartsBefore(const PortBattle& port, std::size_t step) {
    if (step == 0 || step >= port.stepRequests.size()) {
        return false;
    }
    const auto first = port.requests.begin() + static_cast<std::ptrdiff_t>(port.stepRequests[step - 1]);
    const auto last = port.requests.begin() + static_cast<std::ptrdiff_t>(port.stepRequests[step]);
    return std::any_of(first, last, [](const battle::BattleRequest& request) {
        return std::holds_alternative<battle::ScrollFinalBattleBackground>(request);
    });
}

// Whether the equipment refresh at the head of the loop step after `state` takes an empty party slot last.
// It refreshes every slot whose flag is not zero, the fourth first, so the last it takes is the lowest of
// them (UpdateCharProp :5597-5631); at a final-battle stage after the first an empty slot's flag is the
// start's fill.
bool theRefreshEndsOnAnEmptySlot(const battle::BattleState& state) {
    for (int position = 0; position < battle::kPartySlots; ++position) {
        if (state.equipmentChangeFlags[static_cast<std::size_t>(position)] != 0) {
            return !state.at(battle::BattlerSlot::character(position)).actor;
        }
    }
    return false;
}

// The loop steps whose last attack's level and physical bit are an empty slot's refresh's (docs/Bugs.md):
// from the step whose window holds that refresh to the step before the one in whose window the cartridge
// writes the level again, by another refresh's clear (UpdateEquip :2301-2304) or an action's (:6999,
// :7297). The port keeps the refresh before's values there; nothing in those steps reads either
// (BattleWholeOracleTest.TheBlockAnEmptySlotsRefreshLeavesReachesNothing).
std::vector<bool> stepsWithTheLastAttackFromAnEmptySlot(const PortBattle& port, const WholeBattleResult& cartridge,
                                                        std::size_t steps) {
    std::vector<bool> from(steps, false);
    bool open = false;
    for (std::size_t step = 1; step < steps; ++step) {
        if (theRefreshEndsOnAnEmptySlot(port.steps[step - 1])) {
            open = true;
        } else if (open && cartridge.steps[step].writes.contains(cells::kLastAttackLevel)) {
            open = false;
        }
        from[step] = open;
    }
    return from;
}

}  // namespace

std::string DamageOracle::carriedTargetsCompared(const battle::BattleState& state, const PartyCells& boosts,
                                                 const CartridgeStep& step) {
    if (step.slotsHoldingCarriedTargets.empty()) {
        return {};
    }
    const LaidState laid = layState(state, boosts, &step.cells);
    std::string lines;
    for (const std::uint8_t slot : step.slotsHoldingCarriedTargets) {
        const std::uint32_t word = static_cast<std::uint32_t>(slot) * 2;
        bool compared = false;
        for (std::uint32_t byte = 0; byte < 2; ++byte) {
            const std::uint32_t cell = cells::kActionTargets + word + byte;
            compared = compared || (laid.cells.contains(cell) && comparedBits(laid, cell) != 0);
        }
        if (compared) {
            char text[160];
            std::snprintf(text, sizeof text,
                          "slot %u, command $%02X: its targets came from $B8 carried into the step "
                          "(CreateAction :12960-12961) and are compared\n",
                          slot, step.cells[cells::kActionCommand + word - kWorkRam]);
            lines += text;
        }
    }
    return lines;
}

BattleCheck DamageOracle::checkBattle(const BattleScenario& scenario) {
    const TextCorpus& corpus = text();
    // The port's battle first: its facings are what the cartridge's frames are handed.
    PortBattle portRun = portBattle(scenario, corpus, monsterSprites());
    WholeBattleResult cartridgeRun = wholeBattle(scenario, portRun.facings);
    BattleCheck check{.differences = {}, .cartridge = std::move(cartridgeRun), .port = std::move(portRun)};
    facingsHandedOver_ = true;
    byteAboveRngPosition_ = scenario.start.byteAboveRngPosition;
    struct HandBack {
        bool& handed;
        bool& fromAnEmptySlot;
        std::optional<std::uint8_t>& aboveRngPosition;
        ~HandBack() {
            handed = false;
            fromAnEmptySlot = false;
            aboveRngPosition.reset();
        }
    } handBack{facingsHandedOver_, lastAttackFromAnEmptySlot_, byteAboveRngPosition_};
    std::string& lines = check.differences;
    const WholeBattleResult& cartridge = check.cartridge;
    const PortBattle& port = check.port;
    if (cartridge.stackPointers.size() > 1) {
        lines += "the loop's steps left the stack pointer at " + std::to_string(cartridge.stackPointers.size()) +
                 " places\n";
    }
    // The start's own read-set gate, as the start check has it.
    std::string unhanded;
    for (const UnwrittenRead& read : cartridge.unwrittenReads) {
        if (!leftAsTheMachineHasIt(read.at)) {
            unhanded += readLine("read before written:", read);
        }
    }
    for (const UnwrittenRead& read : cartridge.readsBeyondTheBattlesCells) {
        unhanded += readLine("read past the battle's cells:", read);
    }
    lines += named("read-set gate", unhanded);
    const std::size_t steps = std::min(cartridge.steps.size(), port.steps.size());
    const std::vector<bool> lastAttackFromAnEmptySlot = stepsWithTheLastAttackFromAnEmptySlot(port, cartridge, steps);
    for (std::size_t step = 0; step < steps && lines.empty(); ++step) {
        const CartridgeStep& theirs = cartridge.steps[step];
        const battle::BattleState& ours = port.steps[step];
        const PartyCells boosts = boostsAtStep(scenario.start.cells, port, step);
        const std::string at = "step " + std::to_string(step) + " (frame " + std::to_string(theirs.frame) + ")";
        if (port.stepFrames[step] != theirs.frame) {
            lines += at + ": the port reached it at frame " + std::to_string(port.stepFrames[step]) + "\n";
        }
        // The requests made since the step before's frame wait.
        const std::size_t portFrom = step == 0 ? 0 : port.stepRequests[step - 1];
        const std::size_t cartridgeFrom = step == 0 ? 0 : cartridge.steps[step - 1].firstCall;
        lines += named(at + ", the requests before it",
                       requestDifferences(portShown(port, portFrom, port.stepRequests[step], corpus),
                                          cartridgeShown(cartridge, cartridgeFrom, theirs.firstCall, corpus)));
        lastAttackFromAnEmptySlot_ = lastAttackFromAnEmptySlot[step];
        lines += named(at, compareBattleState(ours, boosts, theirs.cells));
        lines += named(at + ", write-set gate", uncoveredWrites(ours, boosts, theirs.writes, &theirs.cells));
        if (step > 0) {
            // The carried reads are held by the step before's state, laid as that step is.
            lastAttackFromAnEmptySlot_ = lastAttackFromAnEmptySlot[step - 1];
            const std::string carried =
                carriedReads(port.steps[step - 1], boostsAtStep(scenario.start.cells, port, step - 1),
                             theirs.carriedReads, &cartridge.steps[step - 1].cells,
                             aStageStartsBefore(port, step) ? CarriedFrom::TheStart : CarriedFrom::AStep);
            lines += named(at + ", carried-read gate", carried);
            lastAttackFromAnEmptySlot_ = lastAttackFromAnEmptySlot[step];
            lines += named(at + ", the copy rule", carriedTargetsCompared(ours, boosts, theirs));
            if (!carried.empty() && !theirs.stackAtFirstCarriedRead.empty()) {
                lines += "the stack's top at the first such read, each page from $E0: " +
                         stackTops(theirs.stackAtFirstCarriedRead) + "\n";
                if (!theirs.tracedBeforeFirstCarriedRead.empty()) {
                    lines += "the trace's last line before it: " + theirs.tracedBeforeFirstCarriedRead + "\n";
                }
                if (!theirs.accessesBeforeFirstCarriedRead.empty()) {
                    lines += "the battle program's accesses before it:";
                    for (const std::string& access : theirs.accessesBeforeFirstCarriedRead) {
                        lines += " " + access + ",";
                    }
                    lines += "\n";
                }
            }
        }
    }
    if (!lines.empty()) {
        return check;
    }
    // A battle past the frame cap is ended by the cartridge's own event 4 a step after the port stops.
    if (cartridge.timedOut || port.timedOut) {
        lines += "the battle did not end within " + std::to_string(scenario.frameCap) + " frames\n";
        return check;
    }
    if (cartridge.steps.size() != port.steps.size()) {
        lines += "the cartridge's battle took " + std::to_string(cartridge.steps.size()) + " steps, the port's " +
                 std::to_string(port.steps.size()) + "\n";
        return check;
    }
    if (!cartridge.ended || !port.result) {
        lines += "the battle did not end on both sides\n";
        return check;
    }
    // From the last step's frame wait to the close: the requests in order, and the frame the battle closed
    // on, every frame of its end with them.
    const bool won = std::holds_alternative<battle::BattleWon>(port.result->outcome);
    lines += named("the requests from the last step to the close",
                   requestDifferences(portShown(port, port.stepRequests.back(), port.requests.size(), corpus, won),
                                      cartridgeShown(cartridge, cartridge.steps.back().firstCall, cartridge.calls.size(),
                                                     corpus, true)));
    if (port.frames != cartridge.frames) {
        lines += "the cartridge's battle closed on frame " + std::to_string(cartridge.frames) + ", the port's on " +
                 std::to_string(port.frames) + "\n";
    }
    if (!lines.empty()) {
        return check;
    }
    // What the game keeps: the party's records, the inventory, the switches and the Veldt's battles. A
    // record the battle refreshed for its equipment keeps the boosts its equipment gives (UpdateEquip
    // :2349-2362, CheckHPBoost :2456-2472); every other record keeps the ones it came in with.
    WriteBackResult kept;
    kept.cells = scenario.start.cells;
    kept.cells.party = port.result->party;
    for (const std::size_t record : refreshedRecords(port, port.steps.size())) {
        const battle::DerivedStats derived = battle::deriveEquipment(port.result->party.characters[record]);
        kept.cells.hpBoostBits[record] = boostBits(derived.hpBoost);
        kept.cells.mpBoostBits[record] = boostBits(derived.mpBoost);
    }
    kept.inventory = port.result->party.inventory;
    kept.switches = port.result->switches;
    kept.veldtBattles = cartridge.veldtBattlesBefore;
    if (port.result->veldtFormation) {
        kept.veldtBattles.set(static_cast<std::size_t>(*port.result->veldtFormation));
    }
    lines += named("what the game keeps", differences(kept, cartridge.kept));
    // The last three places of the items obtained, handed to the next battle: every byte, since other
    // programs write them between battles and a battle hands back what it does not touch as it came. A
    // record made for no item reaches the items obtained only as the colosseum's prize, at the place its
    // count names from zero (BtlGfx_05 btlgfx_main.asm:357-373), and the graphics program's start zeroes
    // that count (:43456-43460), so it never stands in a place the battle hands on: the targeting an empty
    // party slot's hands leave (docs/Bugs.md) reaches none of these bytes.
    std::string tail;
    for (std::size_t place = 0; place < battle::kItemsObtainedTailPlaces; ++place) {
        const battle::ItemRecord& ours = port.result->itemsObtainedTail[place];
        const battle::ItemRecord& theirs = cartridge.itemsObtainedTail[place];
        for (std::size_t at = 0; at < battle::kItemRecordBytes; ++at) {
            if (ours[at] != theirs[at]) {
                char text[64];
                std::snprintf(text, sizeof text, "place %zu byte %zu: port %02X cartridge %02X\n", place, at, ours[at],
                              theirs[at]);
                tail += text;
            }
        }
    }
    lines += named("the items obtained handed back", tail);
    return check;
}

}  // namespace ostinato::trace
