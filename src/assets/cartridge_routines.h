// The cartridge's own routines, run on a machine of their own.
//
// Some of what the cartridge holds is reachable only through code the cartridge holds: every
// battle background's tilemap and most of its graphics are LZ streams that the battle graphics
// program decodes with Decompress_ext, a routine of its own. The port does not rewrite that
// routine. Its bytes are read out of the installed image (RomAsset::DECOMPRESS_ROUTINE) and placed
// on a SNES the engine runs for the purpose, hosting no cartridge, and the port calls it as a
// function. Nothing of the game's code is compiled in or written to a file.
//
//     assets::CartridgeRoutines routines{decompressorBytes};
//     const std::span<const std::uint8_t> stream = assets::compressedStream(bytesAtThePointer);
//     const std::vector<std::uint8_t> tilemap = routines.decompress(stream);
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "retropp/vm.h"

namespace ostinato::assets {

// A compressed stream opens with a word giving its own length, header included, so the stream
// that begins at `bytes` is its first `length` bytes.
//
// Throws std::out_of_range when `bytes` is shorter than the stream it opens, and
// std::invalid_argument when the word is shorter than the header itself.
[[nodiscard]] std::span<const std::uint8_t> compressedStream(std::span<const std::uint8_t> bytes);

// The decompressor, placed once and called for every stream.
class CartridgeRoutines {
public:
    // Place the decompressor from `decompressor`, the routine's bytes as the cartridge holds them.
    // Throws std::invalid_argument when the bytes are not the routine (its length, its first
    // instructions and its return are checked), and whatever the engine throws when the machine
    // cannot place it.
    explicit CartridgeRoutines(std::span<const std::uint8_t> decompressor);

    CartridgeRoutines(const CartridgeRoutines&) = delete;
    CartridgeRoutines& operator=(const CartridgeRoutines&) = delete;

    // Decode `stream`, a whole compressed stream (compressedStream), and return exactly the bytes
    // the routine wrote. The decoded length is measured, not read from the stream: the stream's
    // own word is its compressed length, and the routine says nothing about how much it wrote.
    //
    // Throws std::invalid_argument for a stream whose first word is not its length, and
    // std::length_error for one that decodes to more than the destination holds.
    [[nodiscard]] std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> stream);

private:
    // The decompressor's signature: the source's long pointer and the destination's, each split as
    // the routine reads them — a 16-bit offset and a bank — and the status register it is entered
    // with.
    using Decompress = retropp::Routine<void(std::uint16_t, std::uint8_t, std::uint16_t,
                                             std::uint8_t, std::uint8_t)>;

    std::vector<std::uint8_t> runOnce(std::span<const std::uint8_t> stream, std::uint8_t fill);

    retropp::Vm::SNES machine_;
    Decompress decompress_;
};

}  // namespace ostinato::assets
