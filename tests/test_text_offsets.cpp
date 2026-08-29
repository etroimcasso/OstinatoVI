// Pointer-class offset tests: the per-record byte offsets that locate the
// variable-length text records.
//
// Two layers:
//   1. Drift — the offset arrays compiled into text_offsets.cpp (dialogueOffsets
//      / pointerOffsets) must match the independent parser-emitted fixture
//      (tests/fixtures/text_offsets_expected.h) entry for entry, so a hand edit
//      or re-emit drift in either file fails. No corpus needed.
//   2. Round-trip — against a real cartridge, every pointer-record accessor
//      must return exactly the fixture-defined slice of the family's bytes
//      ([off[i], off[i+1]), last record to end; dialogue over the concatenated
//      dlg1+dlg2 stream). This cross-checks the accessors, the production
//      offsets, and the cartridge's own bytes against the fixture authority at
//      once.
//
// The cartridge is the image FF6_VANILLA_ROM names; without one, or without the
// machine that reads one, these skip and say which is missing.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "data/text_corpus.h"
#include "data/text_metadata.h"
#include "vanilla_rom.h"
#include "data/text_offsets.h"
#include "fixtures/text_offsets_expected.h"
#include "ostinato/attack_id.h"
#include "ostinato/item_id.h"
#include "ostinato/text_class.h"

namespace ostinato {
namespace {

bool spanEq(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

// ---------------------------------------------------------------------------
// Drift — production arrays vs the parser-emitted fixture. No corpus.
// ---------------------------------------------------------------------------

// One production array paired with its independent fixture copy.
struct OffsetCase {
    const char* name;
    std::span<const std::uint32_t> prod;
    std::span<const std::uint32_t> expected;
};

std::vector<OffsetCase> allOffsetCases() {
    using test::kExpectedAttackMsgOffsets;
    using test::kExpectedBattleDlgOffsets;
    using test::kExpectedBlitzDescOffsets;
    using test::kExpectedBushidoDescOffsets;
    using test::kExpectedDialogueOffsets;
    using test::kExpectedGenjuAttackDescOffsets;
    using test::kExpectedGenjuBonusDescOffsets;
    using test::kExpectedItemDescOffsets;
    using test::kExpectedLoreDescOffsets;
    using test::kExpectedMagicDescOffsets;
    using test::kExpectedMapTitleOffsets;
    using test::kExpectedMonsterDlgOffsets;
    using test::kExpectedRareItemDescOffsets;
    return {
        {"dialogue", dialogueOffsets(), kExpectedDialogueOffsets},
        {"attack_msg", pointerOffsets(TextClass::ATTACK_MSG),
         kExpectedAttackMsgOffsets},
        {"battle_dlg", pointerOffsets(TextClass::BATTLE_DLG),
         kExpectedBattleDlgOffsets},
        {"monster_dlg", pointerOffsets(TextClass::MONSTER_DLG),
         kExpectedMonsterDlgOffsets},
        {"map_title", pointerOffsets(TextClass::MAP_TITLE),
         kExpectedMapTitleOffsets},
        {"item_desc", pointerOffsets(TextClass::ITEM_DESC),
         kExpectedItemDescOffsets},
        {"magic_desc", pointerOffsets(TextClass::MAGIC_DESC),
         kExpectedMagicDescOffsets},
        {"lore_desc", pointerOffsets(TextClass::LORE_DESC),
         kExpectedLoreDescOffsets},
        {"blitz_desc", pointerOffsets(TextClass::BLITZ_DESC),
         kExpectedBlitzDescOffsets},
        {"bushido_desc", pointerOffsets(TextClass::BUSHIDO_DESC),
         kExpectedBushidoDescOffsets},
        {"genju_attack_desc", pointerOffsets(TextClass::GENJU_ATTACK_DESC),
         kExpectedGenjuAttackDescOffsets},
        {"genju_bonus_desc", pointerOffsets(TextClass::GENJU_BONUS_DESC),
         kExpectedGenjuBonusDescOffsets},
        {"rare_item_desc", pointerOffsets(TextClass::RARE_ITEM_DESC),
         kExpectedRareItemDescOffsets},
    };
}

TEST(TextOffsets, EveryArrayMatchesFixture) {
    for (const auto& c : allOffsetCases()) {
        ASSERT_EQ(c.prod.size(), c.expected.size()) << c.name;
        for (std::size_t i = 0; i < c.prod.size(); ++i) {
            EXPECT_EQ(c.prod[i], c.expected[i]) << c.name << " offset[" << i << "]";
        }
    }
}

TEST(TextOffsets, DialogueOffsetsAreCombinedDlg1Dlg2) {
    // 3084 = dlg1 1574 + dlg2 1510.
    const auto dlg = dialogueOffsets();
    EXPECT_EQ(dlg.size(), 3084u);
    EXPECT_EQ(dlg.size(), textClassMetadata(TextClass::DLG1).recordCount +
                              textClassMetadata(TextClass::DLG2).recordCount);
    // dlg1's _0 and _1 both live at offset 0 in the ROM; that duplicate is
    // preserved verbatim rather than normalized away.
    EXPECT_EQ(dlg[0], 0u);
    EXPECT_EQ(dlg[1], 0u);
    // Offsets are non-decreasing across the whole combined stream (records lie
    // sequentially; equal only for the zero-length _0/_1 pair).
    for (std::size_t i = 1; i < dlg.size(); ++i) {
        EXPECT_GE(dlg[i], dlg[i - 1]) << "offset[" << i << "]";
    }
}

TEST(TextOffsets, NonDialoguePointerClassAsserts) {
    // pointerOffsets is only valid for the self-contained pointer classes;
    // DLG1/DLG2 route through dialogueOffsets() instead. A fixed class has no
    // offset table at all. (The debug assert fires in a debug build; here we
    // only pin that a self-contained pointer class is well-formed.)
    EXPECT_FALSE(pointerOffsets(TextClass::ITEM_DESC).empty());
    EXPECT_EQ(pointerOffsets(TextClass::ITEM_DESC).size(),
              textClassMetadata(TextClass::ITEM_DESC).recordCount);
}

// ---------------------------------------------------------------------------
// Round-trip — the corpus read out of a cartridge; accessors vs slices of the
// same bytes taken straight from the image.
// ---------------------------------------------------------------------------

class TextOffsetsRipTest : public ::testing::Test {
protected:
    inline static test::IngestedCartridge cartridge_;

    static void SetUpTestSuite() { cartridge_ = test::ingestVanilla(); }

    static const TextCorpus& corpus() { return cartridge_.content->text; }

    static std::vector<std::uint8_t> raw(const std::string& stem) {
        for (const auto& meta : textClassMetadata()) {
            if (meta.fileStem == stem) {
                return test::romSlice(cartridge_.image, meta.id);
            }
        }
        return {};
    }

    // The public accessor for one self-contained pointer class's record.
    static std::span<const std::uint8_t> recordVia(TextClass klass,
                                                    std::size_t i) {
        switch (klass) {
            case TextClass::ATTACK_MSG:
                return corpus().attackMessage(static_cast<AttackId>(i));
            case TextClass::ITEM_DESC:
                return corpus().itemDescription(static_cast<ItemId>(i));
            case TextClass::BATTLE_DLG:
                return corpus().battleDialogue(i);
            case TextClass::MONSTER_DLG:
                return corpus().monsterDialogue(i);
            case TextClass::MAP_TITLE:
                return corpus().mapTitle(i);
            case TextClass::MAGIC_DESC:
                return corpus().magicDescription(i);
            case TextClass::LORE_DESC:
                return corpus().loreDescription(i);
            case TextClass::BLITZ_DESC:
                return corpus().blitzDescription(i);
            case TextClass::BUSHIDO_DESC:
                return corpus().bushidoDescription(i);
            case TextClass::GENJU_ATTACK_DESC:
                return corpus().genjuAttackDescription(i);
            case TextClass::GENJU_BONUS_DESC:
                return corpus().genjuBonusDescription(i);
            case TextClass::RARE_ITEM_DESC:
                return corpus().rareItemDescription(i);
            default:
                return {};
        }
    }
};

// Every self-contained pointer class: each record the accessor returns equals
// the raw `.dat` bytes sliced by the independent fixture offsets.
TEST_F(TextOffsetsRipTest, SelfContainedAccessorsMatchFixtureSlices) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    for (const auto& c : allOffsetCases()) {
        const std::string name = c.name;
        if (name == "dialogue") continue;  // handled separately below
        const TextClass klass = [&] {
            for (const auto& meta : textClassMetadata()) {
                if (std::string(meta.fileStem) == name) return meta.id;
            }
            return TextClass::DTE_TABLE;  // unreachable
        }();
        const std::vector<std::uint8_t> bytes = raw(name);
        const std::span<const std::uint32_t> off = c.expected;
        ASSERT_TRUE(corpus().has(klass)) << name;
        for (std::size_t i = 0; i < off.size(); ++i) {
            const std::size_t start = off[i];
            const std::size_t end = (i + 1 < off.size()) ? off[i + 1]
                                                         : bytes.size();
            const std::span<const std::uint8_t> want(bytes.data() + start,
                                                     end - start);
            ASSERT_TRUE(spanEq(recordVia(klass, i), want))
                << name << " record " << i;
        }
    }
}

// Dialogue is the combined dlg1+dlg2 stream: record i comes from dlg1's bytes
// below the split, dlg2's above it, sliced by the same combined offset table.
TEST_F(TextOffsetsRipTest, DialogueAccessorMatchesConcatenatedSlices) {
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    std::vector<std::uint8_t> concat = raw("dlg1");
    const std::vector<std::uint8_t> dlg2 = raw("dlg2");
    concat.insert(concat.end(), dlg2.begin(), dlg2.end());
    const std::span<const std::uint32_t> off = dialogueOffsets();
    ASSERT_EQ(off.back() <= concat.size(), true);
    for (std::size_t i = 0; i < off.size(); ++i) {
        const std::size_t start = off[i];
        const std::size_t end = (i + 1 < off.size()) ? off[i + 1] : concat.size();
        const std::span<const std::uint8_t> want(concat.data() + start,
                                                 end - start);
        ASSERT_TRUE(spanEq(corpus().dialogue(i), want)) << "dialogue " << i;
    }
}

}  // namespace
}  // namespace ostinato
