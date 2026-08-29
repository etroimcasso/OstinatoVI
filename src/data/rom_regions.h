// Where each content family lives inside the cartridge. The game reads its
// script, graphics, and tables straight out of the player's ROM image, so it
// needs an address for every family it wants — and the address differs between
// the Japanese ROM and the two US ones.
//
// The row data is generated (src/data/generated/rom_regions_data.inc, emitted
// by tools/asm_parser/parse_rom_regions.py from the upstream rip lists); this
// header owns the entry type and the lookups.
//
// A region is a place, not bytes: hand one to a VM hosting the cartridge and
// read it (retropp/vm.h). Addresses are the SNES addresses the ROM itself uses,
// and the VM resolves them in the machine's decoded address space — so a family
// wider than the bank it starts in reads straight through, and nothing here
// does address arithmetic.
#pragma once

#include <optional>
#include <span>

#include "retropp/memory_region.h"

#include "ostinato/game_version.h"
#include "ostinato/rom_asset.h"
#include "ostinato/text_class.h"

namespace ostinato {

// One family in one language. Identity is the two typed fields; .region is
// where that language's ROM keeps the family, covering the whole of it.
struct RomRegionEntry {
    RomAsset asset;
    Language language;
    retropp::MemoryRegion region;
};

// The full table (asset order, each asset's languages together), for iteration
// and full-corpus tests.
std::span<const RomRegionEntry> romRegions();

// Where `asset` lives in a `language` ROM, or nullopt when that language does
// not ship the family — the item-type names are US-only and the character
// titles Japanese-only, so a caller that wants one asks for it and handles the
// absence.
std::optional<retropp::MemoryRegion> romRegion(RomAsset asset, Language language);

// The family holding a text class's records. The text classes are the port's
// own view of the script (src/data/text_metadata.h); this is how one becomes
// an address.
RomAsset textClassAsset(TextClass klass);

}  // namespace ostinato
