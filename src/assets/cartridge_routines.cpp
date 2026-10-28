#include "assets/cartridge_routines.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "retropp/isa.h"
#include "retropp/location.h"
#include "retropp/memory_region.h"
#include "retropp/snes.h"

namespace ostinato::assets {

namespace {

// --- the routine as the cartridge holds it ------------------------------------------------------

// Decompress_ext is 147 bytes (btlgfx_main.asm:48807-48882): it opens by saving the data bank
// and the direct page and setting a 16-bit index register (phb, phd, ldx #$0000), and it returns
// with rtl.
constexpr std::size_t kRoutineBytes = 147;
constexpr std::array<std::uint8_t, 5> kRoutineOpening{0x8B, 0x0B, 0xA2, 0x00, 0x00};
constexpr std::uint8_t kReturnLong = 0x6B;

// --- how it is called ---------------------------------------------------------------------------

// The routine reads its source through the long pointer at $f3-$f5 and writes its destination
// through the long pointer at $f6-$f8 (btlgfx_main.asm:48814-48821). Both sit in the direct page,
// which the routine itself sets to $0000.
constexpr std::uint32_t kSourcePointer = 0x0000F3;
constexpr std::uint32_t kSourceBank = 0x0000F5;
constexpr std::uint32_t kDestinationPointer = 0x0000F6;
constexpr std::uint32_t kDestinationBank = 0x0000F8;

// The routine is written for 16-bit index registers (its first instruction is a three-byte
// ldx #$0000) and an 8-bit accumulator it widens and narrows itself: the status register it is
// entered with has the accumulator narrow, the index registers wide and interrupts off.
constexpr std::uint8_t kEntryStatus = 0x24;

// Where the stream is put for the routine to read: in work RAM, above the direct page and the
// stack the routine runs on.
constexpr std::uint16_t kStreamOffset = 0x2000;
constexpr std::uint8_t kStreamBank = 0x7E;
constexpr std::uint32_t kStreamHome = 0x7E2000;

// Where the routine writes: the battle graphics program's own destination for a decoded block
// (btlgfx_main.asm:4150-4158), in the bank the routine's own ring buffer sits in. The ring begins
// at $f800, so a decoded block has room up to there.
constexpr std::uint16_t kDecodedOffset = 0xC400;
constexpr std::uint8_t kDecodedBank = 0x7F;
constexpr std::uint32_t kDecodedHome = 0x7FC400;
constexpr std::uint32_t kRingBufferStart = 0x7FF800;
constexpr std::size_t kDecodedCapacity = kRingBufferStart - kDecodedHome;

// The length word at the head of a stream.
constexpr std::size_t kStreamHeaderBytes = 2;

std::uint16_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | bytes[at + 1] << 8);
}

}  // namespace

std::span<const std::uint8_t> compressedStream(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kStreamHeaderBytes) {
        throw std::out_of_range("a compressed stream needs its two-byte length word");
    }
    const std::uint16_t length = word(bytes, 0);
    if (length < kStreamHeaderBytes) {
        throw std::invalid_argument("a compressed stream's length word reads " +
                                    std::to_string(length) + ", shorter than its own header");
    }
    if (length > bytes.size()) {
        throw std::out_of_range("a compressed stream of " + std::to_string(length) +
                                " bytes runs past the " + std::to_string(bytes.size()) +
                                " bytes it begins in");
    }
    return bytes.first(length);
}

CartridgeRoutines::CartridgeRoutines(std::span<const std::uint8_t> decompressor) {
    if (decompressor.size() != kRoutineBytes) {
        throw std::invalid_argument("the decompressor is " + std::to_string(kRoutineBytes) +
                                    " bytes, not " + std::to_string(decompressor.size()));
    }
    for (std::size_t i = 0; i < kRoutineOpening.size(); ++i) {
        if (decompressor[i] != kRoutineOpening[i]) {
            throw std::invalid_argument("these bytes do not open as the decompressor does");
        }
    }
    if (decompressor.back() != kReturnLong) {
        throw std::invalid_argument("these bytes do not end in the decompressor's rtl");
    }
    decompress_ = machine_.uploadRoutine<void(std::uint16_t, std::uint8_t, std::uint16_t,
                                              std::uint8_t, std::uint8_t)>(
        decompressor,
        {.inputs = {retropp::Location::memory(kSourcePointer),
                    retropp::Location::memory(kSourceBank),
                    retropp::Location::memory(kDestinationPointer),
                    retropp::Location::memory(kDestinationBank), retropp::snes::P},
         .isa = retropp::Isa::Wdc65816,
         .returns = retropp::RoutineReturn::Long});
}

std::vector<std::uint8_t> CartridgeRoutines::runOnce(std::span<const std::uint8_t> stream,
                                                     std::uint8_t fill) {
    machine_.write(retropp::MemoryRegion{.at = kStreamHome,
                                         .size = static_cast<std::uint32_t>(stream.size())},
                   stream);
    const std::vector<std::uint8_t> filling(kDecodedCapacity, fill);
    machine_.write(retropp::MemoryRegion{.at = kDecodedHome, .size = kDecodedCapacity}, filling);
    decompress_(kStreamOffset, kStreamBank, kDecodedOffset, kDecodedBank, kEntryStatus);
    return machine_.read(retropp::MemoryRegion{.at = kDecodedHome, .size = kDecodedCapacity});
}

std::vector<std::uint8_t> CartridgeRoutines::decompress(std::span<const std::uint8_t> stream) {
    if (stream.size() < kStreamHeaderBytes || word(stream, 0) != stream.size()) {
        throw std::invalid_argument("the stream's first word is not its length");
    }
    // The routine says nothing about how much it wrote, so the destination is run twice under two
    // fills: a byte the routine wrote reads the same both times, a byte it left alone reads each
    // fill in turn. The decoded bytes are the run of agreeing bytes from the start.
    const std::vector<std::uint8_t> onZero = runOnce(stream, 0x00);
    const std::vector<std::uint8_t> onOnes = runOnce(stream, 0xFF);
    std::size_t length = 0;
    while (length < kDecodedCapacity && onZero[length] == onOnes[length]) {
        ++length;
    }
    if (length == kDecodedCapacity) {
        throw std::length_error("a stream decoded to more than the " +
                                std::to_string(kDecodedCapacity) + " bytes the destination holds");
    }
    return std::vector<std::uint8_t>(onZero.begin(),
                                     onZero.begin() + static_cast<std::ptrdiff_t>(length));
}

}  // namespace ostinato::assets
