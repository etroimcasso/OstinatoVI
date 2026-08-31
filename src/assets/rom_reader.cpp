#include "assets/rom_reader.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace ostinato::assets {

namespace {

// The first bank the cartridge occupies. Anything below is the console's own memory, not the
// cartridge, so an address there is a table error rather than something to resolve.
constexpr std::uint32_t kFirstCartridgeBank = 0xC0;

// Sixty-four banks of 64 KB, so six bits of bank index address the whole image.
constexpr std::uint32_t kBankIndexMask = 0x3F;
constexpr unsigned kBankShift = 16;
constexpr std::uint32_t kOffsetInBankMask = 0xFFFF;

std::string describe(std::uint32_t address) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text = "$";
    for (int shift = 20; shift >= 0; shift -= 4) {
        text += kDigits[(address >> shift) & 0xF];
    }
    return text;
}

// Where a cartridge address's bytes sit in the image.
std::size_t imageOffset(std::uint32_t address) {
    const std::uint32_t bank = (address >> kBankShift) & 0xFF;
    if (bank < kFirstCartridgeBank) {
        throw std::invalid_argument("not a cartridge address: " + describe(address));
    }
    return static_cast<std::size_t>(bank & kBankIndexMask) << kBankShift |
           (address & kOffsetInBankMask);
}

}  // namespace

bool HiRomImage::contains(const retropp::MemoryRegion& where) const {
    const std::uint32_t bank = (where.at >> kBankShift) & 0xFF;
    if (bank < kFirstCartridgeBank) {
        return false;
    }
    const std::size_t offset = imageOffset(where.at);
    return offset + where.totalBytes() <= image_.size();
}

std::span<const std::uint8_t> HiRomImage::read(const retropp::MemoryRegion& where) const {
    const std::size_t offset = imageOffset(where.at);
    const std::size_t length = static_cast<std::size_t>(where.totalBytes());
    if (offset + length > image_.size()) {
        throw std::out_of_range("cartridge region " + describe(where.at) + " plus " +
                                std::to_string(length) + " bytes runs past the end of a " +
                                std::to_string(image_.size()) + "-byte image");
    }
    return image_.subspan(offset, length);
}

}  // namespace ostinato::assets
