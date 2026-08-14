// Structural metadata for the FF6 text classes: how each class's raw `.dat`
// file is laid out. The row data is generated
// (src/data/generated/text_metadata_data.inc, emitted by
// tools/asm_parser/parse_text_meta.py from original-src/include/text/*.inc);
// this header owns the entry type and the lookup accessors.
//
// The metadata drives the TextCorpus loader (src/data/text_corpus.h): a FIXED
// class is sliced into recordCount records of recordSize bytes each; a POINTER
// class is variable-length and located by an offset table (built in a later
// pass — only its record count is known here). The numbers come straight from
// the rip-generated ca65 includes and are version-invariant for the ROMs whose
// text ripped identically.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "ostinato/text_class.h"

namespace ostinato {

// How a text class's records are laid out on disk.
enum class TextClassKind : std::uint8_t {
    FIXED,    // recordCount padded records of recordSize bytes each
    POINTER,  // variable-length records located by an offset table
};

// One metadata entry. Identity is the .id field (the TextClass enumerator —
// identity is a field, never a comment); a compile-time assert verifies
// id == array position. .fileStem is the on-disk basename ("<stem>.dat" under
// the language directory). .recordSize is the fixed record width in bytes for
// FIXED classes and 0 for POINTER classes.
struct TextClassMetadata {
    TextClass id;
    std::string_view fileStem;
    TextClassKind kind;
    std::uint16_t recordCount;
    std::uint8_t recordSize;
};

// The full metadata table (TextClass index order), for iteration and
// full-corpus tests.
std::span<const TextClassMetadata> textClassMetadata();

// The metadata for one class. O(1) — indexes the table directly.
const TextClassMetadata& textClassMetadata(TextClass klass);

}  // namespace ostinato
