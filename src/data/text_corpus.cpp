#include "data/text_corpus.h"

#include <cassert>
#include <cstddef>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <utility>

#include "data/text_offsets.h"

namespace ostinato {

namespace {

// THE SINGLE FILESYSTEM SEAM. Every byte the corpus holds enters through this
// one function; everything above it works on spans over owned buffers. To
// route text through a different byte source (e.g. an engine data-asset
// provider) this is the only place that changes — no caller touches the disk.
std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("text corpus: cannot open " + path.string());
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        throw std::runtime_error("text corpus: cannot size " + path.string());
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0 &&
        !in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("text corpus: short read on " + path.string());
    }
    return bytes;
}

}  // namespace

// --- DteTable ----------------------------------------------------------------

std::pair<std::uint8_t, std::uint8_t> DteTable::expand(std::uint8_t code) const {
    assert(isDteCode(code) && "not a DTE code (byte < 0x80)");
    assert(loaded() && "DTE table not loaded");
    const std::size_t index = (static_cast<std::size_t>(code) - 0x80) * 2;
    assert(index + 1 < bytes_.size() && "DTE code past the table");
    return {bytes_[index], bytes_[index + 1]};
}

// --- TextCorpus construction -------------------------------------------------

TextCorpus::TextCorpus(
    std::array<std::vector<std::uint8_t>, kTextClassCount> buffers)
    : buffers_(std::move(buffers)) {}

TextCorpus TextCorpus::loadFromDirectory(const std::filesystem::path& dir) {
    std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
    for (const auto& meta : textClassMetadata()) {
        const auto path = dir / (std::string(meta.fileStem) + ".dat");
        if (!std::filesystem::exists(path)) {
            continue;  // EN-only / JP-only / not-yet-ripped: absence is allowed
        }
        std::vector<std::uint8_t> bytes = readFile(path);
        if (meta.kind == TextClassKind::FIXED) {
            const std::size_t expected =
                static_cast<std::size_t>(meta.recordCount) * meta.recordSize;
            if (bytes.size() != expected) {
                throw std::runtime_error(
                    "text corpus: " + path.string() + " is " +
                    std::to_string(bytes.size()) + " bytes, expected " +
                    std::to_string(expected));
            }
        } else if (bytes.empty()) {
            throw std::runtime_error("text corpus: " + path.string() +
                                     " is empty");
        }
        buffers[static_cast<std::size_t>(meta.id)] = std::move(bytes);
    }
    return TextCorpus(std::move(buffers));
}

// --- lookups -----------------------------------------------------------------

bool TextCorpus::has(TextClass klass) const {
    return !buffers_[static_cast<std::size_t>(klass)].empty();
}

std::span<const std::uint8_t> TextCorpus::rawBytes(TextClass klass) const {
    return buffers_[static_cast<std::size_t>(klass)];
}

std::span<const std::uint8_t> TextCorpus::fixedRecord(TextClass klass,
                                                      std::size_t index) const {
    const TextClassMetadata& meta = textClassMetadata(klass);
    assert(meta.kind == TextClassKind::FIXED && "not a fixed-length class");
    const std::vector<std::uint8_t>& buf =
        buffers_[static_cast<std::size_t>(klass)];
    assert(!buf.empty() && "text class not loaded");
    assert(index < meta.recordCount && "text record index out of range");
    const std::size_t offset = index * meta.recordSize;
    return std::span<const std::uint8_t>(buf).subspan(offset, meta.recordSize);
}

std::span<const std::uint8_t> TextCorpus::pointerRecord(TextClass klass,
                                                        std::size_t index) const {
    assert(textClassMetadata(klass).kind == TextClassKind::POINTER &&
           "not a pointer class");
    const std::vector<std::uint8_t>& buf =
        buffers_[static_cast<std::size_t>(klass)];
    assert(!buf.empty() && "text class not loaded");
    const std::span<const std::uint32_t> offsets = pointerOffsets(klass);
    assert(index < offsets.size() && "text record index out of range");
    const std::size_t start = offsets[index];
    // The last record runs to the end of the class's bytes; every other ends
    // where the next begins.
    const std::size_t end =
        (index + 1 < offsets.size()) ? offsets[index + 1] : buf.size();
    return std::span<const std::uint8_t>(buf).subspan(start, end - start);
}

std::span<const std::uint8_t> TextCorpus::dialogue(std::size_t index) const {
    const std::span<const std::uint32_t> offsets = dialogueOffsets();
    assert(index < offsets.size() && "dialogue index out of range");
    const std::vector<std::uint8_t>& dlg1 =
        buffers_[static_cast<std::size_t>(TextClass::DLG1)];
    const std::vector<std::uint8_t>& dlg2 =
        buffers_[static_cast<std::size_t>(TextClass::DLG2)];
    assert(!dlg1.empty() && !dlg2.empty() && "dialogue banks not loaded");
    // The combined offsets address a dlg1‖dlg2 concatenation; the split is
    // dlg1's byte length. Integrity: the emitted table's first dlg2 offset
    // must equal that length (both come from the same rip).
    const std::size_t split = dlg1.size();
    assert(offsets[textClassMetadata(TextClass::DLG1).recordCount] == split &&
           "dialogue offset table split disagrees with dlg1 byte length");
    const std::size_t start = offsets[index];
    const std::size_t end =
        (index + 1 < offsets.size()) ? offsets[index + 1] : split + dlg2.size();
    if (start >= split) {
        // A dlg2 record: shift into the dlg2 buffer.
        return std::span<const std::uint8_t>(dlg2).subspan(start - split,
                                                           end - start);
    }
    // A dlg1 record (the last one ends exactly at the split).
    return std::span<const std::uint8_t>(dlg1).subspan(start, end - start);
}

DteTable TextCorpus::dte() const {
    return DteTable(rawBytes(TextClass::DTE_TABLE));
}

// Enum-keyed name accessors.
std::span<const std::uint8_t> TextCorpus::itemName(ItemId id) const {
    return fixedRecord(TextClass::ITEM_NAME, static_cast<std::size_t>(id));
}
std::span<const std::uint8_t> TextCorpus::monsterName(MonsterId id) const {
    return fixedRecord(TextClass::MONSTER_NAME, static_cast<std::size_t>(id));
}
std::span<const std::uint8_t> TextCorpus::monsterSpecialName(MonsterId id) const {
    return fixedRecord(TextClass::MONSTER_SPECIAL_NAME,
                       static_cast<std::size_t>(id));
}
std::span<const std::uint8_t> TextCorpus::statusName(StatusId id) const {
    return fixedRecord(TextClass::STATUS_NAME, static_cast<std::size_t>(id));
}
std::span<const std::uint8_t> TextCorpus::danceName(DanceId id) const {
    return fixedRecord(TextClass::DANCE_NAME, static_cast<std::size_t>(id));
}
std::span<const std::uint8_t> TextCorpus::genjuName(EsperId id) const {
    return fixedRecord(TextClass::GENJU_NAME, static_cast<std::size_t>(id));
}
std::span<const std::uint8_t> TextCorpus::genjuAttackName(EsperId id) const {
    return fixedRecord(TextClass::GENJU_ATTACK_NAME,
                       static_cast<std::size_t>(id));
}
std::span<const std::uint8_t> TextCorpus::genjuBonusName(EsperBonus bonus) const {
    return fixedRecord(TextClass::GENJU_BONUS_NAME,
                       static_cast<std::size_t>(bonus));
}

// Decimal-index name accessors.
std::span<const std::uint8_t> TextCorpus::charName(std::size_t index) const {
    return fixedRecord(TextClass::CHAR_NAME, index);
}
std::span<const std::uint8_t> TextCorpus::attackName(std::size_t index) const {
    return fixedRecord(TextClass::ATTACK_NAME, index);
}
std::span<const std::uint8_t> TextCorpus::magicName(std::size_t index) const {
    return fixedRecord(TextClass::MAGIC_NAME, index);
}
std::span<const std::uint8_t> TextCorpus::bushidoName(std::size_t index) const {
    return fixedRecord(TextClass::BUSHIDO_NAME, index);
}
std::span<const std::uint8_t> TextCorpus::battleCommandName(
    std::size_t index) const {
    return fixedRecord(TextClass::BATTLE_CMD_NAME, index);
}
std::span<const std::uint8_t> TextCorpus::itemTypeName(std::size_t index) const {
    return fixedRecord(TextClass::ITEM_TYPE_NAME, index);
}
std::span<const std::uint8_t> TextCorpus::rareItemName(std::size_t index) const {
    return fixedRecord(TextClass::RARE_ITEM_NAME, index);
}

// Enum-keyed pointer-record accessors.
std::span<const std::uint8_t> TextCorpus::attackMessage(AttackId id) const {
    return pointerRecord(TextClass::ATTACK_MSG, static_cast<std::size_t>(id));
}
std::span<const std::uint8_t> TextCorpus::itemDescription(ItemId id) const {
    return pointerRecord(TextClass::ITEM_DESC, static_cast<std::size_t>(id));
}

// Decimal-index pointer-record accessors.
std::span<const std::uint8_t> TextCorpus::battleDialogue(std::size_t index) const {
    return pointerRecord(TextClass::BATTLE_DLG, index);
}
std::span<const std::uint8_t> TextCorpus::monsterDialogue(
    std::size_t index) const {
    return pointerRecord(TextClass::MONSTER_DLG, index);
}
std::span<const std::uint8_t> TextCorpus::mapTitle(std::size_t index) const {
    return pointerRecord(TextClass::MAP_TITLE, index);
}
std::span<const std::uint8_t> TextCorpus::magicDescription(
    std::size_t index) const {
    return pointerRecord(TextClass::MAGIC_DESC, index);
}
std::span<const std::uint8_t> TextCorpus::loreDescription(
    std::size_t index) const {
    return pointerRecord(TextClass::LORE_DESC, index);
}
std::span<const std::uint8_t> TextCorpus::blitzDescription(
    std::size_t index) const {
    return pointerRecord(TextClass::BLITZ_DESC, index);
}
std::span<const std::uint8_t> TextCorpus::bushidoDescription(
    std::size_t index) const {
    return pointerRecord(TextClass::BUSHIDO_DESC, index);
}
std::span<const std::uint8_t> TextCorpus::genjuAttackDescription(
    std::size_t index) const {
    return pointerRecord(TextClass::GENJU_ATTACK_DESC, index);
}
std::span<const std::uint8_t> TextCorpus::genjuBonusDescription(
    std::size_t index) const {
    return pointerRecord(TextClass::GENJU_BONUS_DESC, index);
}
std::span<const std::uint8_t> TextCorpus::rareItemDescription(
    std::size_t index) const {
    return pointerRecord(TextClass::RARE_ITEM_DESC, index);
}

}  // namespace ostinato
