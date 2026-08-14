// The FF6 text classes: the ~30 named byte tables the game text is stored in
// (name tables, dialogue banks, description tables, and the DTE char table).
// Each class ships as a raw `.dat` file the game reads at runtime; this enum
// is the identity used to key the structural metadata table
// (src/data/text_metadata.h) and the TextCorpus loader (src/data/text_corpus.h).
//
// The enumerator ORDER is the metadata table's row order — a compile-time
// assert in text_metadata.cpp verifies each row's .klass equals its position,
// so this list and src/data/generated/text_metadata_data.inc must stay in
// lockstep.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ostinato {

enum class TextClass : std::uint8_t {
    // Fixed-length name tables (padded ITEM_SIZE-byte records).
    CHAR_NAME,
    ITEM_NAME,
    MAGIC_NAME,
    ATTACK_NAME,
    MONSTER_NAME,
    MONSTER_SPECIAL_NAME,
    STATUS_NAME,
    GENJU_NAME,
    GENJU_ATTACK_NAME,
    GENJU_BONUS_NAME,
    DANCE_NAME,
    BUSHIDO_NAME,
    BATTLE_CMD_NAME,
    ITEM_TYPE_NAME,  // EN-only in the U-ROM rip
    RARE_ITEM_NAME,
    // Pointer-indexed variable-length tables (offset tables built in a later
    // pass; only their record counts are known at this layer).
    DLG1,
    DLG2,
    ATTACK_MSG,
    BATTLE_DLG,
    MONSTER_DLG,
    MAP_TITLE,
    ITEM_DESC,
    MAGIC_DESC,
    LORE_DESC,
    BLITZ_DESC,
    BUSHIDO_DESC,
    GENJU_ATTACK_DESC,
    GENJU_BONUS_DESC,
    RARE_ITEM_DESC,
    // The DTE char table (byte >= $80 expands to a two-glyph pair).
    DTE_TABLE,
};

// Number of text classes. The metadata table's size is static_asserted equal
// to this in text_metadata.cpp.
inline constexpr std::size_t kTextClassCount = 30;

}  // namespace ostinato
