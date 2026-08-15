// Per-record byte offsets for the POINTER text classes. The offset arrays are
// generated (src/data/generated/text_offsets_data.inc, emitted by
// tools/asm_parser/parse_text_meta.py from the `_N :=` symbols in
// original-src/include/text/*_en.inc); this header owns the accessors.
//
// A POINTER class's records are variable-length, so the loader locates record
// `i` as the byte span [offset[i], offset[i+1]) (the last record runs to the
// end of the class's bytes). See src/data/text_corpus.h for the slicer.
//
// Dialogue is special: dlg1 and dlg2 share ONE combined offset array whose
// values address the concatenated dlg1+dlg2 byte stream — records below the
// dlg1 record count live in dlg1's bytes, the rest in dlg2's. The ROM's
// 16-bit pointer + bank-increment mechanism does not appear here.
#pragma once

#include <cstdint>
#include <span>

#include "ostinato/text_class.h"

namespace ostinato {

// Combined dialogue offsets (3084 = dlg1 1574 + dlg2 1510) into the
// concatenated dlg1+dlg2 byte stream. Offsets below dlg1's byte length address
// dlg1; the rest address dlg2 (shifted by that length).
std::span<const std::uint32_t> dialogueOffsets();

// Per-record offsets for a self-contained POINTER class (into its own `.dat`).
// Valid for every POINTER class EXCEPT DLG1/DLG2, which use dialogueOffsets();
// asserts otherwise.
std::span<const std::uint32_t> pointerOffsets(TextClass klass);

}  // namespace ostinato
