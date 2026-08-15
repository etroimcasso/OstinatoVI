// Text-corpus tests: the structural metadata table, the TextCorpus loader
// (both the filesystem seam and the seam-free buffer path), the fixed-length
// name accessors round-tripped against the real rip bytes, and DTE expansion.
//
// The real-corpus tests locate the rip via FF6_TEXT_DIR (env), defaulting to
// the repo-relative rip path baked in at configure time. The rip names files
// with a language suffix (char_name_en.dat); the game reads unsuffixed files
// from a language directory (assets/text/en/char_name.dat), so the fixture
// stages the rip into a temp directory under the game's layout and loads
// through the real loadFromDirectory path. Absent corpus -> visible GTEST_SKIP.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "data/text_corpus.h"
#include "data/text_metadata.h"
#include "fixtures/text_metadata_expected.h"
#include "ostinato/dance_id.h"
#include "ostinato/esper_bonus.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_id.h"
#include "ostinato/status_id.h"
#include "ostinato/text_class.h"

#ifndef FF6_TEXT_DIR_DEFAULT
#define FF6_TEXT_DIR_DEFAULT ""
#endif

namespace ostinato {
namespace {

namespace fs = std::filesystem;

// Per-process suffix for temp directories (cross-platform).
long processId() {
#if defined(_WIN32)
    return _getpid();
#else
    return ::getpid();
#endif
}

fs::path ripTextDir() {
    if (const char* env = std::getenv("FF6_TEXT_DIR")) {
        return fs::path(env);
    }
    return fs::path(FF6_TEXT_DIR_DEFAULT);
}

std::vector<std::uint8_t> readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

bool spanEq(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin());
}

// ---------------------------------------------------------------------------
// Metadata table — compared against the parser-emitted fixture, no corpus.
// ---------------------------------------------------------------------------

TEST(TextMetadata, TableMatchesFixture) {
    const auto table = textClassMetadata();
    ASSERT_EQ(table.size(), test::kExpectedTextClassMetadata.size());
    ASSERT_EQ(table.size(), kTextClassCount);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& got = table[i];
        const auto& exp = test::kExpectedTextClassMetadata[i];
        EXPECT_EQ(static_cast<std::uint8_t>(got.id), exp.id) << "row " << i;
        EXPECT_EQ(static_cast<std::uint8_t>(got.kind), exp.kind) << "row " << i;
        EXPECT_EQ(got.recordCount, exp.recordCount) << "row " << i;
        EXPECT_EQ(got.recordSize, exp.recordSize) << "row " << i;
    }
}

TEST(TextMetadata, LookupById) {
    EXPECT_EQ(textClassMetadata(TextClass::ITEM_NAME).recordCount, 256);
    EXPECT_EQ(textClassMetadata(TextClass::ITEM_NAME).recordSize, 13);
    EXPECT_EQ(textClassMetadata(TextClass::ITEM_NAME).kind,
              TextClassKind::FIXED);
    EXPECT_EQ(textClassMetadata(TextClass::DLG1).recordCount, 1574);
    EXPECT_EQ(textClassMetadata(TextClass::DLG1).kind, TextClassKind::POINTER);
    EXPECT_EQ(textClassMetadata(TextClass::DTE_TABLE).recordCount, 128);
    EXPECT_EQ(textClassMetadata(TextClass::DTE_TABLE).recordSize, 2);
}

// ---------------------------------------------------------------------------
// Loader — synthetic inputs, no rip dependency. Exercises the filesystem seam
// (loadFromDirectory -> readFile) and the seam-free buffer constructor.
// ---------------------------------------------------------------------------

class LoaderSyntheticTest : public ::testing::Test {
protected:
    fs::path dir_;
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("ostinato_text_syn_" + std::to_string(processId()));
        fs::create_directories(dir_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    void write(const std::string& name, const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(dir_ / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
};

TEST_F(LoaderSyntheticTest, LoadsFixedClassAndSlicesRecords) {
    // char_name is 64 records of 6 bytes. Fill each record with its index.
    std::vector<std::uint8_t> bytes(64 * 6);
    for (std::size_t r = 0; r < 64; ++r) {
        for (std::size_t b = 0; b < 6; ++b) {
            bytes[r * 6 + b] = static_cast<std::uint8_t>(r);
        }
    }
    write("char_name.dat", bytes);
    const TextCorpus corpus = TextCorpus::loadFromDirectory(dir_);
    ASSERT_TRUE(corpus.has(TextClass::CHAR_NAME));
    const auto rec = corpus.charName(63);
    ASSERT_EQ(rec.size(), 6u);
    EXPECT_EQ(rec[0], 63);
    EXPECT_EQ(rec[5], 63);
}

TEST_F(LoaderSyntheticTest, WrongFixedSizeThrows) {
    write("char_name.dat", std::vector<std::uint8_t>(64 * 6 - 1));  // one short
    EXPECT_THROW(TextCorpus::loadFromDirectory(dir_), std::runtime_error);
}

TEST_F(LoaderSyntheticTest, AbsentClassIsNotLoaded) {
    const TextCorpus corpus = TextCorpus::loadFromDirectory(dir_);  // empty dir
    EXPECT_FALSE(corpus.has(TextClass::CHAR_NAME));
    EXPECT_TRUE(corpus.rawBytes(TextClass::CHAR_NAME).empty());
}

TEST(TextCorpusBufferPath, ConstructsFromBuffersWithoutIo) {
    // The seam-free path (the engine data-asset migration point): feed buffers
    // directly, no filesystem involved.
    std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
    std::vector<std::uint8_t> item(256 * 13);
    for (std::size_t i = 0; i < item.size(); ++i) {
        item[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    buffers[static_cast<std::size_t>(TextClass::ITEM_NAME)] = item;
    const TextCorpus corpus(std::move(buffers));
    ASSERT_TRUE(corpus.has(TextClass::ITEM_NAME));
    const auto rec = corpus.itemName(ItemId{1});
    ASSERT_EQ(rec.size(), 13u);
    EXPECT_EQ(rec[0], static_cast<std::uint8_t>(13));  // record 1 starts at 13
}

// ---------------------------------------------------------------------------
// Real corpus — staged from the rip through the real loader, then round-tripped
// against the raw bytes. Skipped cleanly when the rip is absent.
// ---------------------------------------------------------------------------

class TextCorpusRipTest : public ::testing::Test {
protected:
    inline static fs::path src_;
    inline static fs::path staged_;
    inline static std::optional<TextCorpus> corpus_;
    inline static bool available_ = false;

    static void SetUpTestSuite() {
        src_ = ripTextDir();
        if (src_.empty() ||
            !fs::exists(src_ / "char_name_en.dat")) {
            available_ = false;
            return;
        }
        staged_ = fs::temp_directory_path() /
                  ("ostinato_text_rip_" + std::to_string(processId()));
        fs::create_directories(staged_);
        for (const auto& meta : textClassMetadata()) {
            const fs::path srcFile =
                src_ / (std::string(meta.fileStem) + "_en.dat");
            if (fs::exists(srcFile)) {
                fs::copy_file(srcFile,
                              staged_ / (std::string(meta.fileStem) + ".dat"),
                              fs::copy_options::overwrite_existing);
            }
        }
        corpus_ = TextCorpus::loadFromDirectory(staged_);
        available_ = true;
    }

    static void TearDownTestSuite() {
        corpus_.reset();
        if (!staged_.empty()) {
            std::error_code ec;
            fs::remove_all(staged_, ec);
        }
    }

    // Raw bytes of a class straight from the rip _en.dat, for independent
    // comparison against the loader's slicing.
    static std::vector<std::uint8_t> raw(const std::string& stem) {
        return readAll(src_ / (stem + "_en.dat"));
    }
};

TEST_F(TextCorpusRipTest, EveryFixedClassRoundTrips) {
    if (!available_) GTEST_SKIP() << "rip corpus absent (FF6_TEXT_DIR)";
    for (const auto& meta : textClassMetadata()) {
        if (meta.kind != TextClassKind::FIXED) continue;
        const std::vector<std::uint8_t> bytes = raw(std::string(meta.fileStem));
        ASSERT_EQ(bytes.size(),
                  static_cast<std::size_t>(meta.recordCount) * meta.recordSize)
            << meta.fileStem;
        ASSERT_TRUE(corpus_->has(meta.id)) << meta.fileStem;
        // Every record's accessor slice must equal the raw slice.
        for (std::size_t r = 0; r < meta.recordCount; ++r) {
            const std::span<const std::uint8_t> want(
                bytes.data() + r * meta.recordSize, meta.recordSize);
            std::span<const std::uint8_t> got =
                corpus_->rawBytes(meta.id).subspan(r * meta.recordSize,
                                                   meta.recordSize);
            ASSERT_TRUE(spanEq(got, want))
                << meta.fileStem << " record " << r;
        }
    }
}

TEST_F(TextCorpusRipTest, EnumKeyedAccessorsHitTheRightRecord) {
    if (!available_) GTEST_SKIP() << "rip corpus absent (FF6_TEXT_DIR)";
    const auto itemBytes = raw("item_name");
    const auto item5 = corpus_->itemName(ItemId{5});
    EXPECT_TRUE(spanEq(item5,
                       std::span<const std::uint8_t>(itemBytes.data() + 5 * 13, 13)));

    const auto monBytes = raw("monster_name");
    const auto mon383 = corpus_->monsterName(static_cast<MonsterId>(383));
    EXPECT_TRUE(spanEq(mon383,
                       std::span<const std::uint8_t>(monBytes.data() + 383 * 10, 10)));

    const auto danceBytes = raw("dance_name");
    const auto dance0 = corpus_->danceName(static_cast<DanceId>(0));
    EXPECT_TRUE(spanEq(dance0,
                       std::span<const std::uint8_t>(danceBytes.data(), 12)));

    const auto bonusBytes = raw("genju_bonus_name");
    const auto bonus0 = corpus_->genjuBonusName(static_cast<EsperBonus>(0));
    EXPECT_TRUE(spanEq(bonus0,
                       std::span<const std::uint8_t>(bonusBytes.data(), 9)));
}

TEST_F(TextCorpusRipTest, DecimalIndexAccessorsHitTheRightRecord) {
    if (!available_) GTEST_SKIP() << "rip corpus absent (FF6_TEXT_DIR)";
    const auto attackBytes = raw("attack_name");
    const auto atk174 = corpus_->attackName(174);  // last of 175
    EXPECT_TRUE(spanEq(atk174,
                       std::span<const std::uint8_t>(attackBytes.data() + 174 * 10, 10)));

    const auto cmdBytes = raw("battle_cmd_name");
    const auto cmd0 = corpus_->battleCommandName(0);
    EXPECT_TRUE(spanEq(cmd0,
                       std::span<const std::uint8_t>(cmdBytes.data(), 7)));
}

TEST_F(TextCorpusRipTest, DteTableExpandsAgainstRawPairs) {
    if (!available_) GTEST_SKIP() << "rip corpus absent (FF6_TEXT_DIR)";
    const auto dteBytes = raw("dte_tbl");
    ASSERT_EQ(dteBytes.size(), 256u);
    const DteTable dte = corpus_->dte();
    ASSERT_TRUE(dte.loaded());
    EXPECT_EQ(dte.size(), 128u);
    EXPECT_TRUE(DteTable::isDteCode(0x80));
    EXPECT_FALSE(DteTable::isDteCode(0x7F));
    const auto first = dte.expand(0x80);
    EXPECT_EQ(first.first, dteBytes[0]);
    EXPECT_EQ(first.second, dteBytes[1]);
    const auto last = dte.expand(0xFF);
    EXPECT_EQ(last.first, dteBytes[254]);
    EXPECT_EQ(last.second, dteBytes[255]);
}

TEST_F(TextCorpusRipTest, PointerClassBytesLoadWhole) {
    if (!available_) GTEST_SKIP() << "rip corpus absent (FF6_TEXT_DIR)";
    ASSERT_TRUE(corpus_->has(TextClass::DLG1));
    const auto dlg1 = raw("dlg1");
    EXPECT_EQ(corpus_->rawBytes(TextClass::DLG1).size(), dlg1.size());
}

// ---------------------------------------------------------------------------
// JP posture — the surface is built, but the JP corpus is a U-ROM skeleton
// (only mte_tbl ripped). Validation is visibly deferred until a J ROM exists.
// ---------------------------------------------------------------------------

TEST(TextCorpusJp, NameCorpusValidationDeferred) {
    GTEST_SKIP() << "JP name/description corpus not ripped (U-ROM only); "
                    "loader + metadata surface built, byte validation deferred "
                    "to a J ROM";
}

TEST(TextCorpusJp, MteDecodeValidationDeferred) {
    GTEST_SKIP() << "JP MTE table decode deferred to a later pass; corpus "
                    "validation deferred to a J ROM";
}

}  // namespace
}  // namespace ostinato
