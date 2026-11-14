// An esper read back from its byte: shared with the rewards, in namespace detail.
#pragma once

#include <cstdint>
#include <optional>

#include "ostinato/esper_id.h"

#include "cells/ending/records.h"

namespace ostinato::trace::detail {

using namespace cells;

std::optional<EsperId> esperOfByte(std::uint8_t value);

}  // namespace ostinato::trace::detail
