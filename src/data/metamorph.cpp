#include "data/metamorph.h"

#include <cstddef>

namespace ostinato {

const MetamorphPack& getMetamorphPack(MetamorphInfo info) {
    return kMetamorphPacks[static_cast<std::size_t>(info.packIndex())].record;
}

std::uint8_t metamorphRate(MetamorphInfo info) {
    return kMetamorphRates[static_cast<std::size_t>(info.rate())].value;
}

}  // namespace ostinato
