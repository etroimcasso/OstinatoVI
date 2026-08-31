// Reaching into a cartridge image at the addresses the game itself uses.
//
// Final Fantasy VI is a HiROM cartridge: banks $C0-$FF map straight onto the image, the low 16
// bits of an address being the offset inside a bank and the bank number picking the 64 KB slice.
// A table that runs past the end of the bank it starts in simply continues into the next one, so a
// family is read as one extent rather than walked bank by bank.
//
// This is the ONE place in the port that knows how a cartridge address becomes an image offset.
// Everywhere above it, an address is an opaque fact carried in the region table — nothing else does
// arithmetic on one. The mapping lives here because this is the I/O boundary, where the bytes and
// their addressing are the contract.
#pragma once

#include <cstdint>
#include <span>

#include "retropp/memory_region.h"

namespace ostinato::assets {

// A cartridge image, addressable the way the cartridge addresses itself.
//
// Holds a view, not a copy: the image must outlive the reader, and every span handed back points
// into it.
class HiRomImage {
public:
    explicit HiRomImage(std::span<const std::uint8_t> image) : image_(image) {}

    // Whether `where` lies wholly inside the image.
    [[nodiscard]] bool contains(const retropp::MemoryRegion& where) const;

    // The bytes `where` names, as a view into the image.
    //
    // Throws std::out_of_range naming the address when the region is not wholly inside the image,
    // and std::invalid_argument for an address outside the cartridge's own bank range — both mean
    // the region table disagrees with the cartridge in hand, which is never something to read past.
    [[nodiscard]] std::span<const std::uint8_t> read(const retropp::MemoryRegion& where) const;

private:
    std::span<const std::uint8_t> image_;
};

}  // namespace ostinato::assets
