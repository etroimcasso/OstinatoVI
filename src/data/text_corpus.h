// TextCorpus: runtime loader for the FF6 game text.
//
// FF6's text ships as raw byte `.dat` files (one per TextClass) that the game
// reads at runtime and decodes itself. This class loads those files from a
// language directory, holds the raw bytes, and hands out per-record byte spans
// keyed by the game's own id spaces (ItemId, MonsterId, ...) where a class
// maps cleanly onto one, or by decimal index otherwise.
//
// This layer ships RECORD STRUCTURE only — a span of the raw glyph/escape
// bytes for a record. Turning those bytes into readable text (glyph mapping,
// DTE expansion at the dialogue level, escape-token decoding) is the codec
// layer; the fixed-length name records handled here need no decoding beyond
// slicing, and the DTE char table is exposed for the codecs that do.
//
// I/O boundary: every byte enters through ONE filesystem seam (readFile in the
// .cpp). Everything above the seam works on std::span<const std::uint8_t> over
// buffers this object owns, so the corpus can later be constructed directly
// from engine-supplied byte assets with no file I/O — see the buffer
// constructor.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

#include "data/text_metadata.h"
#include "ostinato/dance_id.h"
#include "ostinato/esper_bonus.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"
#include "ostinato/text_class.h"

namespace ostinato {

// The DTE (Dual-Tile Encoding) char table: 128 two-byte expansions, one for
// each dialogue byte >= 0x80. A view over the loaded dte_tbl bytes (valid for
// the lifetime of the owning TextCorpus).
class DteTable {
public:
    DteTable() = default;
    explicit DteTable(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    // A dialogue byte >= 0x80 is a DTE code; below that it is a literal glyph
    // or an escape.
    static constexpr bool isDteCode(std::uint8_t byte) { return byte >= 0x80; }

    // The two glyph bytes a DTE code expands to. PRECONDITION (asserted):
    // isDteCode(code) and the table is loaded.
    std::pair<std::uint8_t, std::uint8_t> expand(std::uint8_t code) const;

    // Number of expansion pairs (128 when loaded, 0 when empty).
    std::size_t size() const { return bytes_.size() / 2; }
    bool loaded() const { return !bytes_.empty(); }

private:
    std::span<const std::uint8_t> bytes_{};
};

class TextCorpus {
public:
    // Load every text-class `.dat` present under a language directory (e.g.
    // "assets/text/en"). Files are read through the single filesystem seam.
    // A FIXED class whose file size is not recordCount * recordSize is a hard
    // error (corrupt corpus). Classes with no file present are simply absent
    // (has() == false) — the EN-only / JP-only asymmetries and the not-yet-
    // ripped JP corpus fall out of this naturally.
    static TextCorpus loadFromDirectory(const std::filesystem::path& dir);

    // Construct directly from in-memory buffers, indexed by TextClass. This is
    // the seam-free path for the engine's future arbitrary-byte data-asset
    // feature — no file I/O involved.
    explicit TextCorpus(std::array<std::vector<std::uint8_t>, kTextClassCount>
                            buffers);

    // Whether a class's bytes are loaded.
    bool has(TextClass klass) const;

    // The full raw bytes of a class's file (empty span if not loaded). Mainly
    // for the variable-length classes whose per-record structure is built in a
    // later pass, and for tests.
    std::span<const std::uint8_t> rawBytes(TextClass klass) const;

    // --- fixed-length name accessors -----------------------------------------
    // Each returns the raw ITEM_SIZE-byte record (padded, no terminator).
    // Enum-keyed where the class maps cleanly onto one of the game's id spaces.

    std::span<const std::uint8_t> itemName(ItemId id) const;
    std::span<const std::uint8_t> monsterName(MonsterId id) const;
    std::span<const std::uint8_t> monsterSpecialName(MonsterId id) const;
    std::span<const std::uint8_t> statusName(StatusId id) const;
    std::span<const std::uint8_t> danceName(DanceId id) const;
    std::span<const std::uint8_t> genjuName(EsperId id) const;
    std::span<const std::uint8_t> genjuAttackName(EsperId id) const;
    std::span<const std::uint8_t> genjuBonusName(EsperBonus bonus) const;

    // Decimal-index-keyed for the classes whose row count does not match any
    // existing id enum (subset spaces, padded tables, no enum). Re-keying to a
    // named enum is a reader-side change a later pass can make freely.
    std::span<const std::uint8_t> charName(std::size_t index) const;
    std::span<const std::uint8_t> attackName(std::size_t index) const;
    std::span<const std::uint8_t> magicName(std::size_t index) const;
    std::span<const std::uint8_t> bushidoName(std::size_t index) const;
    std::span<const std::uint8_t> battleCommandName(std::size_t index) const;
    std::span<const std::uint8_t> itemTypeName(std::size_t index) const;
    std::span<const std::uint8_t> rareItemName(std::size_t index) const;

    // The DTE char table (for the dialogue codec). Empty/unloaded if the
    // corpus has no dte_tbl.
    DteTable dte() const;

private:
    // The one record slicer every fixed-length accessor funnels through:
    // returns record `index` of a FIXED class. PRECONDITION (asserted): the
    // class is loaded, FIXED, and index < recordCount.
    std::span<const std::uint8_t> fixedRecord(TextClass klass,
                                              std::size_t index) const;

    std::array<std::vector<std::uint8_t>, kTextClassCount> buffers_{};
};

}  // namespace ostinato
