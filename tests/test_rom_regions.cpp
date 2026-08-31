// Full-corpus tests for the ROM region table and the ROM identity table. Every row of both is
// compared to its parser-emitted fixture, plus the lookups: a family present in one language only,
// the text-class mapping, and the ordering the region lookup depends on.
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>

#include <gtest/gtest.h>

#include "data/rom_regions.h"
#include "data/text_metadata.h"
#include "ostinato/rom_identity.h"

#include "fixtures/rom_identity_expected.h"
#include "fixtures/rom_regions_expected.h"

namespace {

using namespace ostinato;

// --- the region table --------------------------------------------------------

TEST(RomRegions, TableMatchesRipLists) {
    const auto table = romRegions();
    ASSERT_EQ(table.size(), test::kExpectedRomRegions.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = test::kExpectedRomRegions[i];
        EXPECT_EQ(static_cast<std::size_t>(table[i].asset), expected.asset) << "asset at " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].language), expected.language)
            << "language at " << i;
        EXPECT_EQ(table[i].region.at, expected.at) << "address at " << i;
        EXPECT_EQ(table[i].region.size, expected.size) << "size at " << i;
        EXPECT_EQ(table[i].region.count, 1u) << "count at " << i;
    }
}

TEST(RomRegions, EveryRowNamesAKnownAssetExactlyOnce) {
    std::set<std::pair<std::size_t, std::size_t>> seen;
    for (const auto& row : romRegions()) {
        EXPECT_LT(static_cast<std::size_t>(row.asset), kRomAssetCount);
        const auto key = std::pair(static_cast<std::size_t>(row.asset),
                                   static_cast<std::size_t>(row.language));
        EXPECT_TRUE(seen.insert(key).second)
            << "asset " << key.first << " listed twice for language " << key.second;
    }
}

TEST(RomRegions, EveryFamilyIsReachableThroughTheLookup) {
    for (const auto& row : romRegions()) {
        const auto found = romRegion(row.asset, row.language);
        ASSERT_TRUE(found.has_value())
            << "asset " << static_cast<std::size_t>(row.asset) << " is in the table but the "
            << "lookup does not find it";
        EXPECT_EQ(found->at, row.region.at);
        EXPECT_EQ(found->size, row.region.size);
    }
}

TEST(RomRegions, LanguageOnlyFamiliesAreAbsentFromTheOtherLanguage) {
    // The US cartridges name item types; the Japanese one gives characters titles. Neither has the
    // other's table, and asking for it says so rather than answering with a wrong place.
    EXPECT_TRUE(romRegion(RomAsset::ITEM_TYPE_NAME, Language::EN).has_value());
    EXPECT_FALSE(romRegion(RomAsset::ITEM_TYPE_NAME, Language::JP).has_value());
    EXPECT_TRUE(romRegion(RomAsset::CHAR_TITLE, Language::JP).has_value());
    EXPECT_FALSE(romRegion(RomAsset::CHAR_TITLE, Language::EN).has_value());
}

TEST(RomRegions, WorldModTilesMatchesTheShippedPool) {
    // The world-modification tile pool: 1,182 bytes in both cartridges, at different places. The
    // same block the world-map data layer already accounts for.
    const auto en = romRegion(RomAsset::WORLD_MOD_TILES, Language::EN);
    const auto jp = romRegion(RomAsset::WORLD_MOD_TILES, Language::JP);
    ASSERT_TRUE(en.has_value());
    ASSERT_TRUE(jp.has_value());
    EXPECT_EQ(en->at, 0xCEF648u);
    EXPECT_EQ(jp->at, 0xCEB048u);
    EXPECT_EQ(en->size, 1182u);
    EXPECT_EQ(jp->size, 1182u);
}

TEST(RomRegions, CharacterNamesHoldExactlyTheirRecords) {
    // Sixty-four characters of six bytes each — the fixed-record shape the text metadata states.
    const auto place = romRegion(RomAsset::CHAR_NAME, Language::EN);
    ASSERT_TRUE(place.has_value());
    const auto& meta = textClassMetadata(TextClass::CHAR_NAME);
    EXPECT_EQ(place->size, static_cast<std::uint32_t>(meta.recordCount) * meta.recordSize);
}

TEST(RomRegions, DialogueCrossesABankBoundary) {
    // The first dialogue bank runs past the end of the bank it starts in. Nothing here has to care:
    // the machine resolves the address, and a read runs straight through.
    const auto place = romRegion(RomAsset::DLG1, Language::EN);
    ASSERT_TRUE(place.has_value());
    const std::uint32_t lastByte = place->at + place->size - 1;
    EXPECT_NE(place->at >> 16, lastByte >> 16);
}

// --- text classes ------------------------------------------------------------

TEST(RomRegions, EveryTextClassNamesAFamilyWithARange) {
    for (std::size_t i = 0; i < kTextClassCount; ++i) {
        const auto klass = static_cast<TextClass>(i);
        const RomAsset asset = textClassAsset(klass);
        EXPECT_TRUE(romRegion(asset, Language::EN).has_value() ||
                    romRegion(asset, Language::JP).has_value())
            << "text class " << i << " maps to a family no cartridge ships";
    }
}

TEST(RomRegions, TextClassMappingIsOneToOne) {
    std::set<std::size_t> assets;
    for (std::size_t i = 0; i < kTextClassCount; ++i) {
        const auto asset = static_cast<std::size_t>(textClassAsset(static_cast<TextClass>(i)));
        EXPECT_TRUE(assets.insert(asset).second) << "two text classes share family " << asset;
    }
}

TEST(RomRegions, DteTableIsNamedForItsUpstreamFile) {
    // The one class whose family is spelled differently from the class itself.
    EXPECT_EQ(textClassAsset(TextClass::DTE_TABLE), RomAsset::DTE_TBL);
    EXPECT_EQ(textClassAsset(TextClass::CHAR_NAME), RomAsset::CHAR_NAME);
}

// --- the identity table ------------------------------------------------------

TEST(RomIdentity, TableMatchesUpstream) {
    ASSERT_EQ(kRomIdentities.size(), test::kExpectedRomIdentities.size());
    for (std::size_t i = 0; i < kRomIdentities.size(); ++i) {
        const auto& expected = test::kExpectedRomIdentities[i];
        EXPECT_EQ(static_cast<std::size_t>(kRomIdentities[i].version), expected.version)
            << "revision at " << i;
        EXPECT_EQ(kRomIdentities[i].crc32, expected.crc32) << "checksum at " << i;
    }
    EXPECT_EQ(kRomSizeBytes, test::kExpectedRomSizeBytes);
    EXPECT_EQ(kCopierHeaderBytes, test::kExpectedCopierHeaderBytes);
}

TEST(RomIdentity, EveryRevisionIsListedOnce) {
    std::set<std::size_t> versions;
    std::set<std::uint32_t> checksums;
    for (const auto& entry : kRomIdentities) {
        EXPECT_TRUE(versions.insert(static_cast<std::size_t>(entry.version)).second);
        EXPECT_TRUE(checksums.insert(entry.crc32).second);
    }
    EXPECT_EQ(versions.size(), 3u);
}

}  // namespace
