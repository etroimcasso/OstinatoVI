// DamageTrace: the escapes on each traced routine's entry and return sites, the watches over its
// cells, and the lines each event is logged as.

#include "trace/trace_log.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
#include "retropp/memory_region.h"
#include "retropp/vm.h"

#include "cells/damage/per_target.h"
#include "scenarios/common/draws.h"
#include "scenarios/common/report_text.h"
#include "trace/cells.h"

namespace ostinato::trace {

using namespace cells;
using namespace detail;

namespace {

// The attack's context read at each traced entry: the params block, the hits to come, the aiming
// rules, where the next params block goes in the script's buffer, and the buffer itself.
struct ContextCells {
    retropp::MemoryRegion params;
    retropp::MemoryRegion hits;
    retropp::MemoryRegion rules;
    retropp::MemoryRegion nextBlock;
    retropp::MemoryRegion blocks;
    retropp::MemoryRegion message;
    retropp::MemoryRegion messageVariables;
};

// The battle program's half of work RAM, whole, for a snapshot.
struct TraceRam {
    retropp::MemoryRegion bytes;
};
constexpr std::uint32_t kTraceRam = 0x7E0000;
constexpr std::uint32_t kTraceRamSize = 0x10000;

// The message ShowMsg shows, in the battle script's second byte, and the four message variables.
constexpr std::uint32_t kShownMessage = 0x7E2D6F;
constexpr std::uint32_t kShownVariables = 0x7E2F35;
constexpr std::uint32_t kShownVariablesSize = 12;
constexpr std::uint32_t kVariableBytes = 3;

// A character record's first cell, its size, and where its actor and level are.
constexpr std::uint32_t kTracedRecords = 0x7E1600;
constexpr std::uint32_t kTracedRecordSize = 37;
constexpr std::uint32_t kTracedRecordCount = 16;
constexpr std::uint32_t kRecordActorByte = 0;
constexpr std::uint32_t kRecordLevelByte = 8;

// The routine whose entry logs the actor and level read before it.
constexpr std::string_view kLearnAbilitiesName = "LearnAbilities";

// The parts of kEndingRanges no range of kTracedRanges or kPartyRecordRanges covers, as [first, end).
std::vector<std::pair<std::uint32_t, std::uint32_t>> endingGaps() {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> covered;
    for (const TracedRange& range : kTracedRanges) {
        covered.emplace_back(range.at, range.at + range.size);
    }
    for (const TracedRange& range : kPartyRecordRanges) {
        covered.emplace_back(range.at, range.at + range.size);
    }
    std::sort(covered.begin(), covered.end());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> gaps;
    for (const TracedRange& range : kEndingRanges) {
        std::uint32_t from = range.at;
        const std::uint32_t end = range.at + range.size;
        for (const auto& [start, stop] : covered) {
            if (stop <= from || start >= end) {
                continue;
            }
            if (start > from) {
                gaps.emplace_back(from, start);
            }
            from = std::max(from, stop);
        }
        if (from < end) {
            gaps.emplace_back(from, end);
        }
    }
    std::sort(gaps.begin(), gaps.end());
    return gaps;
}

std::string hexByte(std::uint8_t value) {
    char text[4];
    std::snprintf(text, sizeof text, "%02X", value);
    return text;
}

std::string framePrefix(std::uint64_t frame) {
    return "f" + std::to_string(frame);
}
}  // namespace

// A running machine answers a read with what its last completed step published, which misses what the
// step under way has written. `written` holds the last byte the guest wrote to each cell the trace
// watches, and a read lays those over the published bytes. `actorRead` and `levelRead` are the last
// actor byte and level byte the guest read from a character record.
struct DamageTrace::Places {
    retropp::RegionMapId<ContextCells> id;
    retropp::RegionMapId<TraceRam> ram;
    std::map<std::uint32_t, std::uint8_t> written{};
    std::optional<std::uint8_t> actorRead;
    std::optional<std::uint8_t> levelRead;

    std::vector<std::uint8_t> overlay(std::vector<std::uint8_t> bytes, std::uint32_t at) const {
        const std::uint32_t end = at + static_cast<std::uint32_t>(bytes.size());
        for (auto cell = written.lower_bound(at); cell != written.end() && cell->first < end; ++cell) {
            bytes[cell->first - at] = cell->second;
        }
        return bytes;
    }

    std::vector<std::uint8_t> read(retropp::Vm& vm, retropp::MemoryRegion ContextCells::*key, std::uint32_t at) const {
        return overlay(vm.read(id, key), at);
    }

    std::vector<std::uint8_t> readRam(retropp::Vm& vm) const {
        return overlay(vm.read(ram, &TraceRam::bytes), kTraceRam);
    }
};

std::vector<std::pair<std::uint32_t, std::uint32_t>> DamageTrace::watchedRanges() {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges = endingGaps();
    for (const TracedRange& range : kTracedRanges) {
        ranges.emplace_back(range.at, range.at + range.size);
    }
    for (const TracedRange& range : kPartyRecordRanges) {
        ranges.emplace_back(range.at, range.at + range.size);
    }
    std::sort(ranges.begin(), ranges.end());
    return ranges;
}

void DamageTrace::logSnapshot(retropp::Vm& vm, const std::string& routine, const char* how, std::uint32_t at) {
    const std::string prefix = framePrefix(frame_()) + " snapshot " + routine;
    line_(prefix + " " + how + " " + busAddress(at));
    if (routine == kSnapshotRoutines[1].name && std::string_view{how} == "enter" && lossMessage_) {
        line_(prefix + " handed " + (*lossMessage_ ? hexByte(**lossMessage_) : std::string{"--"}));
        lossMessage_.reset();
    }
    const std::vector<std::uint8_t> ram = places_->readRam(vm);
    for (const TracedRange& range : kEndingRanges) {
        std::string text = prefix + " " + busAddress(range.at) + " ";
        text.reserve(text.size() + 2 * range.size);
        for (std::uint32_t i = 0; i < range.size; ++i) {
            text += hexByte(ram[range.at - kTraceRam + i]);
        }
        line_(text);
    }
}

DamageTrace::DamageTrace(retropp::Vm& machine, Line line, Frame frame, Access access)
    : vm_(&machine), line_(std::move(line)), frame_(std::move(frame)) {
    places_ = std::make_unique<Places>(Places{
        .id = machine.registerRegions(retropp::regions(
            retropp::region(&ContextCells::params, retropp::MemoryRegion{.at = kParamsCells, .size = kParamsSize},
                            "params block"),
            retropp::region(&ContextCells::hits, retropp::MemoryRegion{.at = kExtraHits, .size = 1}, "hits to come"),
            retropp::region(&ContextCells::rules, retropp::MemoryRegion{.at = kAimingRules, .size = 1}, "aiming rules"),
            retropp::region(&ContextCells::nextBlock, retropp::MemoryRegion{.at = kNextParamsBlock, .size = 1},
                            "next params block"),
            retropp::region(&ContextCells::blocks, retropp::MemoryRegion{.at = kParamsBlocks, .size = kBlocksSize},
                            "params blocks"),
            retropp::region(&ContextCells::message, retropp::MemoryRegion{.at = kShownMessage, .size = 1},
                            "message shown"),
            retropp::region(&ContextCells::messageVariables,
                            retropp::MemoryRegion{.at = kShownVariables, .size = kShownVariablesSize},
                            "message variables"))),
        .ram = machine.registerRegions(retropp::regions(retropp::region(
            &TraceRam::bytes, retropp::MemoryRegion{.at = kTraceRam, .size = kTraceRamSize}, "battle's work RAM")))});

    retropp::EscapeMap escapes;
    // Each return site with the routines whose calls return there, in the order kTracedRoutines
    // names them, and the caller it is for each.
    std::map<std::uint32_t, std::vector<std::pair<TracedRoutine, const char*>>> sites;
    for (const TracedRoutine& routine : kTracedRoutines) {
        escapes.declarations.push_back(retropp::GuestEscape{
            .key = std::string{routine.name} + " entry",
            .at = routine.entry,
            .handler = [this, routine](retropp::Vm& vm, std::uint32_t at) {
                ++open_;
                ++openCalls_[routine.name];
                partyRecordsOpen_ = routine.partyRecords;
                const std::string prefix = framePrefix(frame_());
                line_(prefix + " enter " + routine.name + " " + busAddress(at));
                const std::vector<std::uint8_t> params = places_->read(vm, &ContextCells::params, kParamsCells);
                const std::uint8_t hits = places_->read(vm, &ContextCells::hits, kExtraHits)[0];
                const std::uint8_t rules = places_->read(vm, &ContextCells::rules, kAimingRules)[0];
                const std::uint8_t next = places_->read(vm, &ContextCells::nextBlock, kNextParamsBlock)[0];
                std::string context = prefix + " params";
                for (const std::uint8_t byte : params) {
                    context += " " + hexByte(byte);
                }
                context += " hits " + hexByte(hits) + " rules " + hexByte(rules) + " previous ";
                if (next >= kParamsBlockSize) {
                    const std::vector<std::uint8_t> blocks = places_->read(vm, &ContextCells::blocks, kParamsBlocks);
                    context += hexByte(blocks[next - kParamsBlockSize + kBlockAttacker]);
                } else {
                    context += "--";
                }
                line_(context);
                if (routine.name == kLearnAbilitiesName) {
                    if (places_->actorRead && places_->levelRead) {
                        line_(prefix + " abilities actor " + hexByte(*places_->actorRead) + " level " +
                              hexByte(*places_->levelRead));
                    } else {
                        line_(prefix + " abilities --");
                    }
                }
            }});
        for (const std::uint32_t site :
             {routine.frameReturn, routine.bounceReturn, routine.otherReturns[0], routine.otherReturns[1]}) {
            if (site == 0) {
                continue;
            }
            const char* caller = site == routine.frameReturn    ? " frame return"
                                 : site == routine.bounceReturn ? " bounce return"
                                 : site == routine.otherReturns[0] ? " return 1"
                                                                   : " return 2";
            sites[site].emplace_back(routine, caller);
        }
    }
    for (const auto& [site, served] : sites) {
        std::string key = served.front().first.name + std::string{served.front().second};
        for (std::size_t i = 1; i < served.size(); ++i) {
            key += std::string{", "} + served[i].first.name;
        }
        escapes.declarations.push_back(retropp::GuestEscape{
            .key = key,
            .at = site,
            .handler = [this, served](retropp::Vm&, std::uint32_t at) {
                for (const auto& [routine, caller] : served) {
                    const auto open = openCalls_.find(routine.name);
                    if (open == openCalls_.end() || open->second == 0) {
                        continue;
                    }
                    --open->second;
                    if (open_ > 0) {
                        --open_;
                    }
                    partyRecordsOpen_ = false;
                    line_(framePrefix(frame_()) + " return " + routine.name + " " + busAddress(at));
                    return;
                }
            }});
    }
    for (const SnapshotRoutine& routine : kSnapshotRoutines) {
        const std::string name = routine.name;
        escapes.declarations.push_back(retropp::GuestEscape{
            .key = name + " snapshot entry",
            .at = routine.entry,
            .handler = [this, name](retropp::Vm& vm, std::uint32_t at) {
                ++openSnapshots_;
                ++openSnapshotCalls_[name];
                logSnapshot(vm, name, "enter", at);
            }});
        for (std::size_t i = 0; i < routine.returns.size() && routine.returns[i] != 0; ++i) {
            escapes.declarations.push_back(retropp::GuestEscape{
                .key = name + " snapshot return " + std::to_string(i + 1),
                .at = routine.returns[i],
                .handler = [this, name](retropp::Vm& vm, std::uint32_t at) {
                    const auto open = openSnapshotCalls_.find(name);
                    if (open == openSnapshotCalls_.end() || open->second == 0) {
                        return;
                    }
                    --open->second;
                    --openSnapshots_;
                    logSnapshot(vm, name, "return", at);
                }});
        }
    }
    escapes.declarations.push_back(retropp::GuestEscape{
        .key = "message shown",
        .at = kShowMessageCall,
        .handler = [this](retropp::Vm& vm, std::uint32_t) {
            if (open_ == 0 && openSnapshots_ == 0) {
                return;
            }
            const std::uint8_t message = places_->read(vm, &ContextCells::message, kShownMessage)[0];
            const std::vector<std::uint8_t> variables =
                places_->read(vm, &ContextCells::messageVariables, kShownVariables);
            std::string text = framePrefix(frame_()) + " message " + hexByte(message);
            for (std::uint32_t v = 0; v < kShownVariablesSize; v += kVariableBytes) {
                text += " " + hexByte(variables[v + 2]) + hexByte(variables[v + 1]) + hexByte(variables[v]);
            }
            line_(text);
        }});
    machine.registerEscapes(escapes);

    // A watch names the address the access drove; a direct-page access drives $00:00xx, and the same
    // byte of work RAM is logged under its $7E address so every line names one cell one way.
    const auto watched = [this, access](char how, bool partyRecords = false, bool logged = true) {
        return [this, access, how, partyRecords, logged](retropp::Vm&, std::uint32_t at, std::uint8_t value) {
            const std::uint32_t cell = kWorkRam | (at & 0xFFFF);
            if (how == 'w') {
                places_->written[cell] = value;
            } else if (cell >= kTracedRecords && cell < kTracedRecords + kTracedRecordSize * kTracedRecordCount) {
                const std::uint32_t byte = (cell - kTracedRecords) % kTracedRecordSize;
                if (byte == kRecordActorByte) {
                    places_->actorRead = value;
                } else if (byte == kRecordLevelByte) {
                    places_->levelRead = value;
                }
            }
            if (access) {
                access(cell, value, how == 'w');
            }
            if (logged && open_ > 0 && (!partyRecords || partyRecordsOpen_)) {
                char text[48];
                std::snprintf(text, sizeof text, " %c $7E:%04X %02X", how, cell & 0xFFFF, value);
                line_(framePrefix(frame_()) + text);
            }
            return retropp::AccessVerdict::proceed();
        };
    };
    retropp::WatchMap watches;
    for (const TracedRange& range : kTracedRanges) {
        watches.declarations.push_back(retropp::GuestWatch{.key = range.key,
                                                           .at = retropp::MemoryRegion{.at = range.at, .size = range.size},
                                                           .onRead = watched('r'),
                                                           .onWrite = watched('w')});
    }
    for (const TracedRange& range : kPartyRecordRanges) {
        watches.declarations.push_back(retropp::GuestWatch{.key = range.key,
                                                           .at = retropp::MemoryRegion{.at = range.at, .size = range.size},
                                                           .onRead = watched('r', true),
                                                           .onWrite = watched('w', true)});
    }
    for (const auto& [first, end] : endingGaps()) {
        watches.declarations.push_back(retropp::GuestWatch{.key = "ending cells " + busAddress(first),
                                                           .at = retropp::MemoryRegion{.at = first, .size = end - first},
                                                           .onRead = watched('r', false, false),
                                                           .onWrite = watched('w', false, false)});
    }
    machine.registerWatches(watches);
}

DamageTrace::~DamageTrace() = default;

void DamageTrace::forgetContextWrites() {
    places_->written.clear();
    places_->actorRead.reset();
    places_->levelRead.reset();
}

void DamageTrace::noteWrite(std::uint32_t at, std::uint8_t value) {
    places_->written[at] = value;
}

void DamageTrace::handOverLossMessage(std::optional<std::uint8_t> message) {
    lossMessage_ = message;
}

void DamageTrace::readForAbilities(std::uint8_t actor, std::uint8_t level) {
    places_->actorRead = actor;
    places_->levelRead = level;
}

void DamageTrace::returnFromSnapshot(const SnapshotRoutine& routine) {
    const auto open = openSnapshotCalls_.find(routine.name);
    if (open == openSnapshotCalls_.end() || open->second == 0) {
        return;
    }
    --open->second;
    --openSnapshots_;
    logSnapshot(*vm_, routine.name, "return", routine.returns[0]);
}

void DamageTrace::returnToFrame(const TracedRoutine& routine) {
    if (open_ > 0) {
        --open_;
    }
    if (const auto open = openCalls_.find(routine.name); open != openCalls_.end() && open->second > 0) {
        --open->second;
    }
    partyRecordsOpen_ = false;
    line_(framePrefix(frame_()) + " return " + routine.name + " " + busAddress(routine.frameReturn));
}

}  // namespace ostinato::trace
