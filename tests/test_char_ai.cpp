// Full-corpus tests for the scripted battle setup table. All 24 records are
// compared against the generated fixture (the ROM-assembled values) one whole
// record at a time, independent of the typed rows, so any decode or re-emit
// drift fails loudly. On top of that: the two flag sets are kept apart, both
// unused-slot encodings are pinned, the slot builders round-trip, and the
// hand-traced setups are checked field by field.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/char_ai.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/char_ai_flags.h"
#include "ostinato/char_ai_id.h"
#include "ostinato/char_ai_slot_character.h"
#include "ostinato/char_ai_slot_graphics.h"
#include "ostinato/character_gfx_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/song_id.h"

#include "fixtures/char_ai_expected.h"

namespace {

using namespace ostinato;

// --- full corpus ---------------------------------------------------------------

TEST(CharAi, EveryRecordMatchesRom) {
    const auto table = charAiSetups();
    ASSERT_EQ(table.size(), kCharAiSetupCount);
    ASSERT_EQ(table.size(), test::kExpectedCharAiEntries.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& expected = test::kExpectedCharAiEntries[i];
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i) << "row " << i;
        EXPECT_EQ(expected.id, i) << "fixture row " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &expected.record,
                              sizeof(CharAiSetup)), 0)
            << "record " << i;
    }
}

TEST(CharAi, AccessorReturnsTheRowAtTheIdsPosition) {
    const auto table = charAiSetups();
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(std::memcmp(&charAiSetup(static_cast<CharAiId>(i)),
                              &table[i].record, sizeof(CharAiSetup)), 0)
            << "accessor row " << i;
    }
}

// --- traced setups ---------------------------------------------------------------

// Setup 0 is the ordinary case: no overrides, no cast.
TEST(CharAi, NoneOverridesNothing) {
    const auto& setup = charAiSetup(CharAiId::NONE);
    EXPECT_FALSE(setup.flags.hidesNames());
    EXPECT_FALSE(setup.flags.hidesPartyCharacters());
    EXPECT_EQ(setup.background, BattleBackgroundId::DEFAULT);
    EXPECT_EQ(setup.song, SongId::NONE);
    for (const auto& slot : setup.slots) {
        EXPECT_TRUE(slot.isEmpty());
    }
}

// Setup 1 (char_ai.asm:76-101): Shadow fights the party in the colosseum, with
// his own music and one populated slot.
TEST(CharAi, ShadowColosseumPlacesOneEnemyCharacter) {
    const auto& setup = charAiSetup(CharAiId::SHADOW_COLOSSEUM);
    EXPECT_EQ(setup.background, BattleBackgroundId::DEFAULT);
    EXPECT_EQ(setup.song, SongId::SHADOW);

    const auto& slot = setup.slots[0];
    ASSERT_FALSE(slot.isEmpty());
    EXPECT_EQ(slot.character.characterProperties(), CharacterPropId::SHADOW);
    EXPECT_TRUE(slot.character.isEnemy());
    EXPECT_FALSE(slot.character.isNotInParty());
    EXPECT_FALSE(slot.graphics.usesCharacterDefault());
    EXPECT_EQ(slot.graphics.id(), CharacterGfxId::SHADOW);
    EXPECT_EQ(slot.x, 40);
    EXPECT_EQ(slot.y, 48);

    for (std::size_t i = 1; i < kCharAiSlotCount; ++i) {
        EXPECT_TRUE(setup.slots[i].isEmpty()) << "slot " << i;
    }
}

// Setup 2 (char_ai.asm:105-114): the flashback hides names and the party, and
// overrides the background.
TEST(CharAi, TerraFlashbackHidesNamesAndParty) {
    const auto& setup = charAiSetup(CharAiId::TERRA_FLASHBACK);
    EXPECT_TRUE(setup.flags.hidesNames());
    EXPECT_TRUE(setup.flags.hidesPartyCharacters());
    EXPECT_EQ(setup.background, BattleBackgroundId::BURNING_BUILDING);
    EXPECT_TRUE(setup.slots[0].character.isNotInParty());
    EXPECT_EQ(setup.slots[0].character.characterProperties(),
              CharacterPropId::KEFKA_1);
}

// --- the two flag sets are not interchangeable -------------------------------------

// A record's byte and a slot's byte both use bit 7, meaning different things.
// Keeping them as separate types is the point; this pins that they decode
// independently rather than through one shared reading.
TEST(CharAi, RecordFlagsAndSlotFlagsAreDistinctDespiteSharingBits) {
    const CharAiFlags record{0x80};
    EXPECT_TRUE(record.hidesPartyCharacters());
    EXPECT_FALSE(record.hidesNames());

    const auto slot = CharAiSlotCharacter::notInParty(CharacterPropId::TERRA);
    EXPECT_TRUE(slot.isNotInParty());
    EXPECT_FALSE(slot.isEnemy());
    EXPECT_EQ(slot.characterProperties(), CharacterPropId::TERRA);

    // Same bit, different question — and the record's byte has no notion of a
    // character at all.
    EXPECT_EQ(record.bits, slot.bits & 0x80);
}

TEST(CharAi, FlagBuildersCompose) {
    EXPECT_EQ(CharAiFlags::none().bits, 0x00);
    EXPECT_EQ(CharAiFlags::hideNames().bits, 0x01);
    EXPECT_EQ(CharAiFlags::hideParty().bits, 0x80);
    EXPECT_EQ(CharAiFlags::hideNames().andHideParty().bits, 0x81);
    EXPECT_TRUE(CharAiFlags::hideNames().andHideParty().hidesNames());
    EXPECT_TRUE(CharAiFlags::hideNames().andHideParty().hidesPartyCharacters());
}

// --- slot encodings -----------------------------------------------------------------

// Every slot flag combination round-trips through its builder.
TEST(CharAi, SlotCharacterBuildersRoundTrip) {
    const auto id = CharacterPropId::CYAN;
    const auto ally = CharAiSlotCharacter::ally(id);
    EXPECT_FALSE(ally.isEnemy());
    EXPECT_FALSE(ally.isNotInParty());
    EXPECT_EQ(ally.characterProperties(), id);

    const auto enemy = CharAiSlotCharacter::enemy(id);
    EXPECT_TRUE(enemy.isEnemy());
    EXPECT_FALSE(enemy.isNotInParty());

    const auto absent = CharAiSlotCharacter::notInParty(id);
    EXPECT_FALSE(absent.isEnemy());
    EXPECT_TRUE(absent.isNotInParty());

    const auto both = CharAiSlotCharacter::absentEnemy(id);
    EXPECT_TRUE(both.isEnemy());
    EXPECT_TRUE(both.isNotInParty());
    EXPECT_EQ(both.characterProperties(), id);
}

// The $FF sentinel is the whole byte. Its low six bits look like a valid
// character index, so emptiness must be asked about first — these accessors
// report false rather than reading flags out of the sentinel.
TEST(CharAi, EmptySlotIsNotReadAsACharacter) {
    const CharAiSlotCharacter empty{0xFF};
    EXPECT_TRUE(empty.isEmpty());
    EXPECT_FALSE(empty.isEnemy());
    EXPECT_FALSE(empty.isNotInParty());
}

TEST(CharAi, SlotGraphicsSeparatesDefaultFromASheet) {
    EXPECT_TRUE(CharAiSlotGraphics::fromCharacter().usesCharacterDefault());
    const auto sheet = CharAiSlotGraphics::of(CharacterGfxId::LOCKE);
    EXPECT_FALSE(sheet.usesCharacterDefault());
    EXPECT_EQ(sheet.id(), CharacterGfxId::LOCKE);
}

// The corpus writes an unused slot two ways. Both are unused, and both are
// carried exactly as written rather than normalized to one form.
TEST(CharAi, BothUnusedSlotEncodingsArePreserved) {
    const auto sentinel = CharAiSlot::unused();
    const auto zeroed = CharAiSlot::unusedZeroed();
    EXPECT_TRUE(sentinel.isEmpty());
    EXPECT_TRUE(zeroed.isEmpty());

    EXPECT_EQ(sentinel.graphics.bits, 0xFF);
    EXPECT_EQ(sentinel.aiScript, 0xFF);
    EXPECT_EQ(zeroed.graphics.bits, 0x00);
    EXPECT_EQ(zeroed.aiScript, 0x00);
    EXPECT_NE(std::memcmp(&sentinel, &zeroed, sizeof(CharAiSlot)), 0);

    // Both forms really do occur, so neither builder is dead.
    int sentinels = 0;
    int zeroes = 0;
    for (const auto& entry : charAiSetups()) {
        for (const auto& slot : entry.record.slots) {
            if (!slot.isEmpty()) {
                continue;
            }
            if (std::memcmp(&slot, &sentinel, sizeof(CharAiSlot)) == 0) {
                ++sentinels;
            } else if (std::memcmp(&slot, &zeroed, sizeof(CharAiSlot)) == 0) {
                ++zeroes;
            } else {
                ADD_FAILURE() << "unused slot in setup "
                              << static_cast<int>(entry.id)
                              << " matches neither encoding";
            }
        }
    }
    EXPECT_EQ(sentinels, 46);
    EXPECT_EQ(zeroes, 20);
}

// --- corpus-wide invariants ----------------------------------------------------------

TEST(CharAi, ThirtySlotsArePopulated) {
    int populated = 0;
    for (const auto& entry : charAiSetups()) {
        for (const auto& slot : entry.record.slots) {
            if (!slot.isEmpty()) {
                ++populated;
            }
        }
    }
    EXPECT_EQ(populated, 30);
}

// A populated slot's character index must address a real properties record.
TEST(CharAi, EveryPopulatedSlotNamesARealCharacterRecord) {
    for (const auto& entry : charAiSetups()) {
        for (const auto& slot : entry.record.slots) {
            if (slot.isEmpty()) {
                continue;
            }
            EXPECT_LE(static_cast<std::uint8_t>(
                          slot.character.characterProperties()), 0x3F)
                << "setup " << static_cast<int>(entry.id);
        }
    }
}

}  // namespace
