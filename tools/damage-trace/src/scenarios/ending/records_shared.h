// A character record and its spell list as the cartridge's bytes, and an esper as its byte: shared
// with the rewards, the battle's end and the cartridge's side of the records, in namespace detail.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "game/character_record.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/esper_id.h"

#include "cells/ending/records.h"

namespace ostinato::trace::detail {

using namespace cells;

std::uint32_t recordAt(std::size_t index);

std::uint32_t spellListAt(CharacterPropId actor);

std::uint8_t esperByte(const std::optional<EsperId>& esper);

// A character record's 37 bytes, as the cartridge keeps them.
std::array<std::uint8_t, kRecordSize> recordBytes(const game::CharacterRecord& record, std::uint8_t hpBoostBits,
                                                  std::uint8_t mpBoostBits);

}  // namespace ostinato::trace::detail
