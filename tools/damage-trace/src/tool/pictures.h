// A picture written as a PPM file, and the scripted run to a battle (--to-battle).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

#include "retropp/raster_content.h"

#include "machine/machine_state.h"
#include "oracle/damage_oracle.h"
#include "ostinato/game_version.h"

namespace damage_trace {

using namespace retropp;
using namespace ostinato;

bool writePicture(const std::filesystem::path& file, const RasterContent& picture);

int runToBattle(std::span<const std::uint8_t> image, GameVersion cartridge, const std::filesystem::path& directory,
                std::span<const trace::InputStep> script, bool twice);

}  // namespace damage_trace
