// The oracle's reads and writes of the machine's cells, and its records of the guest's accesses:
// the cells read before anything wrote them, and the accesses past the battle's own cells.

#include "oracle/damage_oracle.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "retropp/memory_region.h"

#include "oracle/routines.h"

namespace ostinato::trace {

using namespace routines;

namespace {

// The stack's two pages: a battle called from the reset code's, and one the field calls.
constexpr std::uint32_t kResetStackPage = DamageOracle::kWorkRam + 0x0100;
constexpr std::uint32_t kFieldStackPage = DamageOracle::kWorkRam + 0x1500;
constexpr std::uint32_t kStackPageSize = 0x100;
// A cell's bits, all of them: a cell wholly on kCellsNotCompared.
constexpr std::uint8_t kEveryBit = 0xFF;

}  // namespace

void DamageOracle::keepTheStackPages() {
    static_assert(kStackPagesSize == 2 * kStackPageSize);
    const std::vector<std::uint8_t> reset = machine_.read(retropp::MemoryRegion{.at = kResetStackPage, .size = kStackPageSize});
    const std::vector<std::uint8_t> field = machine_.read(retropp::MemoryRegion{.at = kFieldStackPage, .size = kStackPageSize});
    std::copy(reset.begin(), reset.end(), stackPages_.begin());
    std::copy(field.begin(), field.end(), stackPages_.begin() + kStackPageSize);
}

void DamageOracle::writeBytes(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes) {
    writeCells(place, bytes, CellWrite::State);
}

void DamageOracle::writeInFlightBytes(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes) {
    writeCells(place, bytes, CellWrite::InFlight);
}

void DamageOracle::writeInFlightByte(std::uint32_t at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> bytes{value};
    writeCells(retropp::MemoryRegion{.at = at, .size = 1}, bytes, CellWrite::InFlight);
}

void DamageOracle::writeInFlightWord(std::uint32_t at, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value & 0xFF),
                                            static_cast<std::uint8_t>(value >> 8)};
    writeCells(retropp::MemoryRegion{.at = at, .size = 2}, bytes, CellWrite::InFlight);
}

void DamageOracle::writeCells(const retropp::MemoryRegion& place, std::span<const std::uint8_t> bytes,
                              CellWrite kind) {
    if (capture_ != nullptr) {
        if (kind == CellWrite::InFlight) {
            return;
        }
        for (std::uint32_t i = 0; i < place.size; ++i) {
            const auto [cell, laid] = capture_->cells.emplace(place.at + i, bytes[i]);
            if (kind == CellWrite::AsTheCartridgeHoldsIt) {
                capture_->disagreements.erase(place.at + i);
            } else if (!laid && cell->second != bytes[i]) {
                capture_->disagreements[place.at + i] |= static_cast<std::uint8_t>(cell->second ^ bytes[i]);
            }
            cell->second = bytes[i];
        }
        return;
    }
    machine_.write(place, bytes);
    if ((start_ && start_->reachedTheLoop) || whole_) {
        for (std::uint32_t cell = place.at; cell < place.at + place.size; ++cell) {
            writtenSincePoint_.insert(cell);
        }
    }
    // The first write after a call starts the next scenario's cells afresh.
    if (gateFresh_) {
        gateWritten_.clear();
        gateFresh_ = false;
    }
    for (std::uint32_t cell = place.at; cell < place.at + place.size; ++cell) {
        gateWritten_.insert(cell);
    }
}

void DamageOracle::noteAccessBeyond(std::uint32_t cell, std::uint8_t value, bool write) {
    if ((!start_ && !whole_) || !gateCalling_ || answering_) {
        return;
    }
    if (write) {
        writtenBeyond_.insert(cell);
    } else if (!writtenBeyond_.contains(cell) && notComparedBits(cell) == 0) {
        std::vector<UnwrittenRead>& reads =
            start_ ? start_->readsBeyondTheBattlesCells : whole_->result.readsBeyondTheBattlesCells;
        reads.push_back(UnwrittenRead{.at = cell, .value = value});
    }
}

void DamageOracle::noteGuestAccess(std::uint32_t cell, std::uint8_t value, bool write) {
    if (cell >= kGateEnd) {
        noteAccessBeyond(cell, value, write);
    }
    if (!gateCalling_) {
        return;
    }
    // A whole battle's gates: the battle program's writes up to the comparison point, and its reads
    // after it of a cell nothing has written since. The graphics
    // program's writes inside an answer count as writes since the point; its reads are its own.
    if (start_ && cell < kGateEnd) {
        StartResult& start = *start_;
        if (!start.reachedTheLoop) {
            if (write && !answering_) {
                start.battleWrites.insert(cell);
            }
        } else if (write) {
            writtenSincePoint_.insert(cell);
        } else if (!answering_ && !writtenSincePoint_.contains(cell)) {
            start.readsAfterThePoint.push_back(UnwrittenRead{.at = cell, .value = value});
        }
    }
    // A whole battle's gates, step by step: the battle program's writes since the last frame wait, and
    // its reads since then of a cell nothing has written since. Before the first frame wait there is no
    // point to carry from, and the reads are the start's read-set gate's.
    if (whole_ && cell < kGateEnd) {
        if (!answering_) {
            recentAccesses_[recentNext_] = RecentAccess{.cell = cell, .value = value, .write = write};
            recentNext_ = (recentNext_ + 1) % kRecentAccesses;
        }
        CartridgeStep& pending = whole_->pending;
        if (write) {
            writtenSincePoint_.insert(cell);
            if (!answering_) {
                pending.writes.insert(cell);
            }
            // The stack's pages as the guest writes them: a read of the machine here answers a tick old.
            if (cell >= kResetStackPage && cell < kResetStackPage + kStackPageSize) {
                stackPages_[cell - kResetStackPage] = value;
            } else if (cell >= kFieldStackPage && cell < kFieldStackPage + kStackPageSize) {
                stackPages_[kStackPageSize + cell - kFieldStackPage] = value;
            }
        } else if (!answering_ && !whole_->result.steps.empty() && !writtenSincePoint_.contains(cell)) {
            pending.carriedReads.push_back(UnwrittenRead{.at = cell, .value = value});
            // The first read the gate reports: a cell wholly on kCellsNotCompared. A cell compared in part is
            // held by its compared bits (carriedReads()).
            if (pending.stackAtFirstCarriedRead.empty() && !onTheStack(cell) && !leftAsTheMachineHasIt(cell) &&
                notComparedBits(cell) == kEveryBit) {
                pending.stackAtFirstCarriedRead.assign(stackPages_.begin(), stackPages_.end());
                if (!traced_.empty()) {
                    pending.tracedBeforeFirstCarriedRead = traced_.back();
                }
                for (std::size_t i = 0; i < kRecentAccesses; ++i) {
                    const RecentAccess& access = recentAccesses_[(recentNext_ + i) % kRecentAccesses];
                    char text[24];
                    std::snprintf(text, sizeof text, "%c %04X %02X", access.write ? 'w' : 'r', access.cell & 0xFFFF,
                                  access.value);
                    pending.accessesBeforeFirstCarriedRead.emplace_back(text);
                }
            }
        }
    }
    if (write) {
        gateWritten_.insert(cell);
    } else if (!gateWritten_.contains(cell)) {
        unwrittenReads_.emplace(cell, value);
    }
}

std::vector<UnwrittenRead> DamageOracle::unwrittenReads() const {
    std::vector<UnwrittenRead> reads;
    for (const auto& [cell, value] : unwrittenReads_) {
        reads.push_back(UnwrittenRead{.at = cell, .value = value});
    }
    return reads;
}

void DamageOracle::writeByte(std::uint32_t at, std::uint8_t value) {
    const std::array<std::uint8_t, 1> bytes{value};
    writeBytes(retropp::MemoryRegion{.at = at, .size = 1}, bytes);
}

void DamageOracle::writeWord(std::uint32_t at, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value & 0xFF),
                                            static_cast<std::uint8_t>(value >> 8)};
    writeBytes(retropp::MemoryRegion{.at = at, .size = 2}, bytes);
}

std::uint8_t DamageOracle::readByte(std::uint32_t at) {
    return machine_.read(retropp::MemoryRegion{.at = at, .size = 1})[0];
}

std::uint16_t DamageOracle::readWord(std::uint32_t at) {
    const std::vector<std::uint8_t> bytes = machine_.read(retropp::MemoryRegion{.at = at, .size = 2});
    return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8);
}

}  // namespace ostinato::trace
