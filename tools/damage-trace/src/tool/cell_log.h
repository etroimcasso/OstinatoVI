// The cells a --watch logs, and CellLog, which logs the cartridge's every write to them and their
// bytes after every frame to cells.log.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "retropp/guest_escape.h"
#include "retropp/guest_watch.h"
#include "retropp/memory_region.h"
#include "retropp/vm.h"

#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"
#include "tool/starting_state.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

// The machine's work RAM, declared so it can be read while the machine runs.
struct WorkRam {
    MemoryRegion cells;
};

RegionMapId<WorkRam> declareWorkRam(Vm& machine);

// Cells of work RAM a run with no window watches.
struct WatchedCells {
    std::uint32_t at = 0;  // the first cell, $7E:0000-$7F:FFFF
    std::uint32_t size = 0;
};

// Reads `$BB:AAAA` (the `$` and the colon optional) as a work RAM address. Nothing for any other shape
// or an address outside $7E:0000-$7F:FFFF.
std::optional<std::uint32_t> parseWorkRamAddress(std::string_view text);

// Every write the cartridge makes to the watched cells is logged as it lands, with the frame it lands
// in, and every watched range's bytes are logged after every frame, to `cells.log`: one line each,
//
//     battle entered <frame>
//     write <frame> $BB:AAAA <byte>
//     cells <frame> $BB:AAAA <the range's bytes>
//
// The battle program's entry is logged too, so a write can be placed before, in or after a battle.
class CellLog {
public:
    CellLog(Vm& machine, const std::filesystem::path& directory, std::span<const WatchedCells> watched,
            const std::uint64_t& frame)
        : watched_(watched.begin(), watched.end()), frame_(frame) {
        if (watched_.empty()) {
            return;
        }
        log_.open(directory / "cells.log", std::ios::app);
        // A range the trace already watches cannot be watched twice: its writes come from the trace
        // (traceWrite()), and its bytes are logged every frame like any other.
        const std::vector<std::pair<std::uint32_t, std::uint32_t>> traced = trace::DamageTrace::watchedRanges();
        WatchMap map;
        for (std::size_t i = 0; i < watched_.size(); ++i) {
            const bool tracedAlready = std::any_of(traced.begin(), traced.end(), [&](const auto& range) {
                return watched_[i].at < range.second && range.first < watched_[i].at + watched_[i].size;
            });
            if (tracedAlready) {
                fromTheTrace_.push_back(watched_[i]);
                continue;
            }
            map.declarations.push_back(GuestWatch{
                .key = "watched cells " + std::to_string(i),
                .at = MemoryRegion{.at = watched_[i].at, .size = watched_[i].size},
                .onWrite = [this](Vm&, std::uint32_t at, std::uint8_t value) {
                    logWrite(at, value);
                    return AccessVerdict::proceed();
                }});
        }
        if (!map.declarations.empty()) {
            machine.registerWatches(map);
        }
        machine.registerEscapes(escapes(GuestEscape{.key = "battle entry, logged",
                                                    .at = kBattleEntry,
                                                    .handler = [this](Vm&, std::uint32_t) {
                                                        log_ << "battle entered " << frame_ << '\n';
                                                    }}));
    }

    // Each watched range's bytes as the frame just run left them.
    void logFrame(Vm& machine, const trace::WorkRamBytes::Map& ram) {
        for (const WatchedCells& range : watched_) {
            char text[32];
            std::snprintf(text, sizeof text, "cells %llu $%02X:%04X ", static_cast<unsigned long long>(frame_),
                          (range.at >> 16) & 0xFF, range.at & 0xFFFF);
            std::string line = text;
            for (std::uint32_t i = 0; i < range.size; ++i) {
                const std::vector<std::uint8_t> byte = machine.read(ram, &trace::WorkRamBytes::bytes, range.at - 0x7E0000 + i);
                char hex[3];
                std::snprintf(hex, sizeof hex, "%02X", byte.empty() ? 0 : byte[0]);
                line += hex;
            }
            log_ << line << '\n';
        }
    }

    // A write the trace's own watches saw: logged when it lands in a watched range the trace covers.
    void traceWrite(std::uint32_t at, std::uint8_t value) {
        for (const WatchedCells& range : fromTheTrace_) {
            if (at >= range.at && at < range.at + range.size) {
                logWrite(at, value);
                return;
            }
        }
    }

private:
    std::vector<WatchedCells> watched_;
    std::vector<WatchedCells> fromTheTrace_;
    const std::uint64_t& frame_;
    std::ofstream log_;

    void logWrite(std::uint32_t at, std::uint8_t value) {
        char text[64];
        std::snprintf(text, sizeof text, "write %llu $%02X:%04X %02X", static_cast<unsigned long long>(frame_),
                      (at >> 16) & 0xFF, at & 0xFFFF, value);
        log_ << text << '\n';
    }
};

}  // namespace damage_trace
