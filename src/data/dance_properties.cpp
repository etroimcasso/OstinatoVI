#include "data/dance_properties.h"

#include <cassert>
#include <cstddef>

namespace ostinato {

const DanceProperties& getDanceProperties(DanceId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kDanceProperties.size() &&
           "dance index out of range (0..7)");
    return kDanceProperties[index].record;
}

}  // namespace ostinato
