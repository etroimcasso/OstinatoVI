#include "data/esper_properties.h"

#include <cassert>
#include <cstddef>

namespace ostinato {

const EsperProperties& getEsperProperties(EsperId id) {
    const auto raw = static_cast<std::size_t>(id);
    constexpr auto base = static_cast<std::size_t>(EsperId::RAMUH);
    assert(raw >= base && raw - base < kEsperProperties.size() &&
           "esper id out of range ($36..$50)");
    return kEsperProperties[raw - base].record;
}

}  // namespace ostinato
