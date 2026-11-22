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

#include "battle/battler_set.h"
#include "battle/party_setup.h"

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
std::vector<std::string> portShown(const PortBattle& port, std::size_t first, std::size_t last, bool won = false) {
    const std::size_t end = std::min(last, port.requests.size());
    if (first >= end) {
        return {};
    }
    return requestsShown(std::span<const battle::BattleRequest>(port.requests).subspan(first, end - first), won);
}

// The graphics calls from `first` to `last` as the port's requests show them. In a battle's `end`, an item
// added once the wrap-up has merged the items obtained ($0A) — a drop, the colosseum's prize — is merged at
// once and shows only its message, as the ending's own check has it (guestCalls() in
// oracle/ending/ending.cpp): the port's end obtains and merges it with no request.
std::vector<std::string> cartridgeShown(const WholeBattleResult& cartridge, std::size_t first, std::size_t last,
                                        bool end = false) {
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
        if (const std::optional<std::string> shownCall = callShown(call)) {
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

// The records of every party member seated at some loop step: the start, and each stage of the final
// battle, refreshes each one for its equipment.
std::set<std::size_t> refreshedRecords(const PortBattle& port) {
    std::set<std::size_t> records;
    for (const battle::BattleState& state : port.steps) {
        for (int position = 0; position < battle::kPartySlots; ++position) {
            if (const std::optional<std::size_t> record = state.at(battle::BattlerSlot::character(position)).record) {
                records.insert(*record);
            }
        }
    }
    return records;
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
    struct HandBack {
        bool& handed;
        ~HandBack() { handed = false; }
    } handBack{facingsHandedOver_};
    std::string& lines = check.differences;
    const WholeBattleResult& cartridge = check.cartridge;
    const PortBattle& port = check.port;
    const PartyCells& boosts = scenario.start.cells;
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
    for (std::size_t step = 0; step < steps && lines.empty(); ++step) {
        const CartridgeStep& theirs = cartridge.steps[step];
        const battle::BattleState& ours = port.steps[step];
        const std::string at = "step " + std::to_string(step) + " (frame " + std::to_string(theirs.frame) + ")";
        if (port.stepFrames[step] != theirs.frame) {
            lines += at + ": the port reached it at frame " + std::to_string(port.stepFrames[step]) + "\n";
        }
        // The requests made since the step before's frame wait.
        const std::size_t portFrom = step == 0 ? 0 : port.stepRequests[step - 1];
        const std::size_t cartridgeFrom = step == 0 ? 0 : cartridge.steps[step - 1].firstCall;
        lines += named(at + ", the requests before it",
                       requestDifferences(portShown(port, portFrom, port.stepRequests[step]),
                                          cartridgeShown(cartridge, cartridgeFrom, theirs.firstCall)));
        lines += named(at, compareBattleState(ours, boosts, theirs.cells));
        lines += named(at + ", write-set gate", uncoveredWrites(ours, boosts, theirs.writes, &theirs.cells));
        if (step > 0) {
            const std::string carried = carriedReads(port.steps[step - 1], boosts, theirs.carriedReads,
                                                     &cartridge.steps[step - 1].cells, CarriedFrom::AStep);
            lines += named(at + ", carried-read gate", carried);
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
                   requestDifferences(portShown(port, port.stepRequests.back(), port.requests.size(), won),
                                      cartridgeShown(cartridge, cartridge.steps.back().firstCall, cartridge.calls.size(),
                                                     true)));
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
    for (const std::size_t record : refreshedRecords(port)) {
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
    // programs write them between battles and a battle hands back what it does not touch as it came.
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
