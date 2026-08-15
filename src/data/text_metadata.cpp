#include "data/text_metadata.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ostinato {

namespace {

// The full metadata table. The generated rows carry the structure read from
// the disassembly; each entry carries its identity as the TextClass
// enumerator, its on-disk stem, its kind, and its record count/size.
constexpr std::array<TextClassMetadata, kTextClassCount> kTextClassMetadata = {{
#include "data/generated/text_metadata_data.inc"
}};

// Self-consistency of the emitted rows: every entry's klass field must equal
// its array position, checked at compile time.
static_assert([] {
    for (std::size_t i = 0; i < kTextClassMetadata.size(); ++i) {
        if (static_cast<std::size_t>(kTextClassMetadata[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kTextClassMetadata entry id fields must match array positions");

// FIXED classes carry a non-zero record size; POINTER classes carry zero.
static_assert([] {
    for (const auto& m : kTextClassMetadata) {
        const bool fixed = m.kind == TextClassKind::FIXED;
        if (fixed != (m.recordSize != 0)) {
            return false;
        }
    }
    return true;
}(), "FIXED rows must have a non-zero recordSize; POINTER rows must have zero");

}  // namespace

std::span<const TextClassMetadata> textClassMetadata() {
    return kTextClassMetadata;
}

const TextClassMetadata& textClassMetadata(TextClass klass) {
    const auto index = static_cast<std::size_t>(klass);
    assert(index < kTextClassMetadata.size() && "text class out of range");
    return kTextClassMetadata[index];
}

}  // namespace ostinato
