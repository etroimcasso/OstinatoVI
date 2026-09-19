// Battle background properties: the 56-record BattleBGProp table. The row data
// is generated (src/data/generated/battle_bg_prop_data.inc); this header owns
// the record type, the entry type and the accessors.
//
// The table lives at ROM E7/0000, six bytes per background
// (gfx/battle_bg.asm:29-51). A record names the graphics blocks, tilemaps and
// palette that compose one background; the loaders are TfrBattleBGGfx
// (btlgfx_main.asm:3999) and TfrBattleBGTiles (btlgfx_main.asm:3934), which
// skip a slot only when its byte is the $FF sentinel.
//
// Each graphics block is 64 tiles (4096 bytes), or 128 when the first block
// sets its double-width flag. Tilemaps are 32x32, of which only the first 19
// rows are visible unless the background scrolls vertically. Palettes hold 48
// colours and load into BG palettes 5, 6 and 7.
#pragma once

#include <cstddef>
#include <span>

#include "ostinato/battle_background_graphics_id.h"
#include "ostinato/battle_background_graphics_slot.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_background_palette_id.h"
#include "ostinato/battle_background_palette_slot.h"
#include "ostinato/battle_background_tilemap_id.h"

namespace ostinato {

// The number of backgrounds BattleBGProp describes — the BATTLE_BG index space
// without its DEFAULT ($FF) fallback.
inline constexpr std::size_t kBattleBackgroundCount = 56;

// One background's six ROM bytes, in the table's own field order. The two
// packed bytes are typed slots; the rest are plain asset ids.
struct BattleBackgroundProperties {
    // VRAM $1000. Carries the double-width flag.
    BattleBackgroundGraphicsSlot graphics1;
    // VRAM $1800. Not loaded when graphics1 is double-width.
    BattleBackgroundGraphicsId graphics2 = BattleBackgroundGraphicsId::NONE;
    // VRAM $4800.
    BattleBackgroundGraphicsId graphics3 = BattleBackgroundGraphicsId::NONE;
    // VRAM $6000.
    BattleBackgroundTilemapId tilemap1 = BattleBackgroundTilemapId::FIELD_WOB;
    // Present in every record and read by no loader.
    BattleBackgroundTilemapId tilemap2 = BattleBackgroundTilemapId::FIELD_WOB;
    // BG palettes 5-7. Carries the wavy-effect flag.
    BattleBackgroundPaletteSlot palette;
};

static_assert(sizeof(BattleBackgroundProperties) == 6,
              "BattleBackgroundProperties must stay byte-identical to a ROM "
              "BattleBGProp record");

// One table entry: the background's identity as a typed field (the
// BattleBackgroundId enumerator — identity is a field, never a comment) and
// its record. A compile-time assert verifies id == array position.
struct BattleBackgroundPropertiesEntry {
    BattleBackgroundId id;
    BattleBackgroundProperties record;
};

// The properties for one background. PRECONDITION (asserted): id names one of
// the 56 real backgrounds, not the DEFAULT ($FF) fallback. The table is
// version-invariant — the property bytes carry no language suffix, and only
// the graphics they name are built per language.
const BattleBackgroundProperties& battleBackgroundProperties(
    BattleBackgroundId id);

// The full 56-entry table (BATTLE_BG index order), for iteration and
// full-corpus tests.
std::span<const BattleBackgroundPropertiesEntry> battleBackgroundTable();

}  // namespace ostinato
