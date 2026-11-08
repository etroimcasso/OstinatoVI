// The trace tool's cell writes: the three words of a write read and refused by word, and the bytes
// written into a running cartridge's work RAM landing before the frame the write names and not before.

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "assets/cartridge.h"
#include "damage_oracle.h"
#include "machine_state.h"
#include "retropp/memory_region.h"
#include "retropp/vm.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using trace::CellWrite;

// The message a write's words are refused with, or nothing when they are read.
std::string refusal(std::string_view frame, std::string_view address, std::string_view bytes) {
    try {
        (void)trace::readCellWrite(frame, address, bytes);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return "";
}

// ─── the words ──────────────────────────────────────────────────────────────────────────────────

TEST(TraceCellWrite, TheFrameTheAddressAndTheBytesAreRead) {
    const CellWrite write = trace::readCellWrite("300", "$7E:3C9C", "E0");
    EXPECT_EQ(write.frame, 300U);
    EXPECT_EQ(write.at, 0x7E3C9CU);
    EXPECT_EQ(write.bytes, (std::vector<std::uint8_t>{0xE0}));
}

TEST(TraceCellWrite, BytesAreWrittenInTheOrderTheyAreGivenFromTheAddressOn) {
    const CellWrite write = trace::readCellWrite("0", "$7E:3420", "1946a0ff");
    EXPECT_EQ(write.at, 0x7E3420U);
    EXPECT_EQ(write.bytes, (std::vector<std::uint8_t>{0x19, 0x46, 0xA0, 0xFF}));
}

TEST(TraceCellWrite, TheDollarSignIsOptionalAndBothBanksAreWorkRam) {
    EXPECT_EQ(trace::readCellWrite("1", "7E:0000", "00").at, 0x7E0000U);
    EXPECT_EQ(trace::readCellWrite("1", "$7F:FFFF", "00").at, 0x7FFFFFU);
    EXPECT_EQ(trace::readCellWrite("1", "$7E:FFFF", "0102").bytes.size(), 2U);
}

TEST(TraceCellWrite, AFrameThatIsNotANumberIsRefusedNamingIt) {
    EXPECT_NE(refusal("ten", "$7E:3C9C", "E0").find("ten is not a frame"), std::string::npos);
    EXPECT_NE(refusal("-1", "$7E:3C9C", "E0").find("-1 is not a frame"), std::string::npos);
    EXPECT_NE(refusal("", "$7E:3C9C", "E0").find("is not a frame"), std::string::npos);
}

TEST(TraceCellWrite, AnAddressOfAnyOtherShapeIsRefusedNamingIt) {
    EXPECT_NE(refusal("1", "$7E3C9C", "E0").find("$7E3C9C is not an address"), std::string::npos);
    EXPECT_NE(refusal("1", "$7E:3C9", "E0").find("is not an address"), std::string::npos);
    EXPECT_NE(refusal("1", "$7E:3C9G", "E0").find("is not an address"), std::string::npos);
    EXPECT_NE(refusal("1", "$7E-3C9C", "E0").find("is not an address"), std::string::npos);
}

TEST(TraceCellWrite, AnAddressOutsideWorkRamIsRefusedNamingIt) {
    EXPECT_NE(refusal("1", "$7D:FFFF", "E0").find("$7D:FFFF is not in work RAM"), std::string::npos);
    EXPECT_NE(refusal("1", "$80:0000", "E0").find("is not in work RAM"), std::string::npos);
    EXPECT_NE(refusal("1", "$C2:0000", "E0").find("is not in work RAM"), std::string::npos);
}

TEST(TraceCellWrite, BytesThatAreNotWholeHexPairsAreRefusedNamingThem) {
    EXPECT_NE(refusal("1", "$7E:3C9C", "").find("is not bytes"), std::string::npos);
    EXPECT_NE(refusal("1", "$7E:3C9C", "E").find("E is not bytes"), std::string::npos);
    EXPECT_NE(refusal("1", "$7E:3C9C", "E0G0").find("E0G0 is not bytes"), std::string::npos);
    EXPECT_NE(refusal("1", "$7E:3C9C", "E0 1").find("is not bytes"), std::string::npos);
}

TEST(TraceCellWrite, BytesRunningPastWorkRamAreRefused) {
    EXPECT_NE(refusal("1", "$7F:FFFF", "0102").find("0102 runs past $7F:FFFF"), std::string::npos);
    EXPECT_EQ(refusal("1", "$7F:FFFE", "0102"), "");
}

// ─── the running machine ────────────────────────────────────────────────────────────────────────

struct WorkRam {
    retropp::MemoryRegion cells;
};

// The cells written, crossing from bank $7E into $7F, and the frame they are written before.
constexpr std::uint32_t kWrittenFrom = 0x7EFFFE;
constexpr std::uint32_t kWrittenCells = 4;
constexpr std::uint64_t kWriteFrame = 300;

// Bytes written before a frame are in work RAM once that frame has run, and had not landed one frame
// earlier; each is the opposite of what the cell held, so the cartridge did not put it there.
TEST(TraceCellWriteMachine, TheBytesLandBeforeTheFrameTheWriteNames) {
    const std::vector<std::uint8_t> rom = vanillaRom();
    const std::span<const std::uint8_t> image = assets::stripCopierHeader(rom);

    trace::runOnLoop([&] {
        retropp::Vm::SNES machine{retropp::VmConfig{}};
        machine.hostRom(image);
        const trace::WorkRamBytes::Map bytes = trace::declareWorkRamBytes(machine);
        const retropp::RegionMapId<WorkRam> ram = machine.registerRegions(retropp::regions(retropp::region(
            &WorkRam::cells, retropp::MemoryRegion{.at = kWrittenFrom, .size = kWrittenCells}, "the cells written")));
        machine.run(retropp::Vm::Advance::OnTick);

        std::vector<CellWrite> writes;
        for (std::uint64_t frame = 0; frame < kWriteFrame; ++frame) {
            trace::writeCells(machine, bytes, writes, frame);
            machine.advanceTick();
        }
        const std::vector<std::uint8_t> before = machine.read(ram, &WorkRam::cells);
        std::vector<std::uint8_t> opposite = before;
        for (std::uint8_t& byte : opposite) {
            byte = static_cast<std::uint8_t>(~byte);
        }
        writes.push_back(CellWrite{.frame = kWriteFrame, .at = kWrittenFrom, .bytes = opposite});
        // A write naming another frame is left alone.
        writes.push_back(CellWrite{.frame = kWriteFrame + 1, .at = kWrittenFrom, .bytes = before});

        trace::writeCells(machine, bytes, writes, kWriteFrame);
        machine.advanceTick();
        EXPECT_EQ(machine.read(ram, &WorkRam::cells), opposite);
        machine.stop();
    });
}

}  // namespace
}  // namespace ostinato::test
