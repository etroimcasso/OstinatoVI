// Work RAM declared on a machine, and a --watch address read from the command line.

#include "tool/cell_log.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "retropp/memory_region.h"
#include "retropp/vm.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

RegionMapId<WorkRam> declareWorkRam(Vm& machine) {
    return machine.registerRegions(regions(region(&WorkRam::cells, MemoryRegion{.at = 0x7E0000, .size = 0x10000}, "work RAM")));
}

std::optional<std::uint32_t> parseWorkRamAddress(std::string_view text) {
    std::string digits;
    for (const char c : text) {
        if (c != '$' && c != ':') {
            digits.push_back(c);
        }
    }
    std::uint32_t at = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), at, 16);
    if (error != std::errc{} || end != digits.data() + digits.size() || digits.size() != 6 || at < 0x7E0000 ||
        at > 0x7FFFFF) {
        return std::nullopt;
    }
    return at;
}

}  // namespace damage_trace
