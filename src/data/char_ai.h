// Scripted battle setups: the 24-record CharAI table. The row data is generated
// (src/data/generated/char_ai_data.inc); this header owns the slot and record
// types and the accessors.
//
// The table lives at ROM D0/FD00, twenty-four bytes per setup
// (btlgfx/char_ai.asm:14-42). A setup is how the game stages a battle that is
// not just "the current party fights": Shadow in the colosseum, Terra's
// flashback, Vargas, Gau on the Veldt, the blitz tutorial. It names up to four
// characters to place in the battle's character slots, and can override the
// background and the music.
//
// Setup NONE is the ordinary case — no override, the player's party fights.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/battle_background_id.h"
#include "ostinato/battle_slot_mask.h"
#include "ostinato/char_ai_flags.h"
#include "ostinato/char_ai_id.h"
#include "ostinato/char_ai_slot_character.h"
#include "ostinato/char_ai_slot_graphics.h"
#include "ostinato/song_id.h"

namespace ostinato {

// The number of setups CharAI describes — the whole CHAR_AI index space.
inline constexpr std::size_t kCharAiSetupCount = 24;

// How many character slots a setup can fill.
inline constexpr std::size_t kCharAiSlotCount = 4;

// One of a setup's four character slots: who stands there, how they are drawn,
// which monster A.I. script drives them, and where they stand.
struct CharAiSlot {
    // Which character, and whether they fight as an enemy or sit outside the
    // party. The $FF sentinel leaves the corresponding party character in place.
    CharAiSlotCharacter character;
    // The sprite sheet, or the character's own.
    CharAiSlotGraphics graphics;
    // The monster A.I. script that drives this character. The consumer adds
    // $0100 to reach the script itself; the byte is stored as the ROM holds it.
    std::uint8_t aiScript = 0;
    // Position on the battle field. The consumer doubles both.
    std::uint8_t x = 0;
    std::uint8_t y = 0;

    // An unused slot, every byte the sentinel.
    static constexpr CharAiSlot unused() {
        return CharAiSlot{CharAiSlotCharacter{0xFF}, CharAiSlotGraphics{0xFF},
                          0xFF, 0xFF, 0xFF};
    }
    // An unused slot whose graphics and script bytes are zeroed instead. The
    // loader reads neither form's trailing bytes; both appear in the table and
    // both are kept as written.
    static constexpr CharAiSlot unusedZeroed() {
        return CharAiSlot{CharAiSlotCharacter{0xFF}, CharAiSlotGraphics{0x00},
                          0x00, 0xFF, 0xFF};
    }

    constexpr bool isEmpty() const { return character.isEmpty(); }
};

static_assert(sizeof(CharAiSlot) == 5,
              "CharAiSlot must stay byte-identical to a ROM slot");

// One setup's twenty-four ROM bytes, in the table's own field order.
struct CharAiSetup {
    // Whether names and gauges, or the party itself, are hidden.
    CharAiFlags flags;
    // The background to fight on. DEFAULT ($FF) keeps whatever was already set.
    BattleBackgroundId background = BattleBackgroundId::DEFAULT;
    // Which monster slots may be targeted. Changed at runtime by an A.I.
    // command; this is the setup's starting value.
    BattleSlotMask validMonsterTargets;
    // The music. NONE ($FF) keeps the default battle song, and is also what a
    // setup uses when it does not interrupt the music already playing.
    SongId song = SongId::NONE;
    // The four character slots, in field order.
    std::array<CharAiSlot, kCharAiSlotCount> slots;
};

static_assert(sizeof(CharAiSetup) == 24,
              "CharAiSetup must stay byte-identical to a ROM CharAI record");

// One table entry: the setup's identity as a typed field (the CharAiId
// enumerator — identity is a field, never a comment) and its record. A
// compile-time assert verifies id == array position.
struct CharAiSetupEntry {
    CharAiId id;
    CharAiSetup record;
};

// The setup for one scripted battle. PRECONDITION (asserted): id names one of
// the 24 records. The table is version-invariant.
const CharAiSetup& charAiSetup(CharAiId id);

// The full 24-entry table (CHAR_AI index order), for iteration and full-corpus
// tests.
std::span<const CharAiSetupEntry> charAiSetups();

}  // namespace ostinato
