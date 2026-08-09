// Full-corpus tests of the formation-core tables: battle_monsters
// (formations), battle_prop (aux), and cond_battle (conditional
// substitutions). The byte-equivalence tests assert EVERY record is
// byte-identical to the ROM bytes (no subset) and that every entry's identity
// field matches its position; the semantic tests hand-trace a few formations
// against the raw bytes and exercise the accessors, the of() builders, and the
// preserved unknown bit.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/formations.h"

#include "ostinato/battle_song.h"
#include "ostinato/formation_id.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_id.h"

#include "fixtures/cond_battle_expected.h"
#include "fixtures/formation_aux_expected.h"
#include "fixtures/formation_expected.h"

namespace {

using ostinato::FormationId;
using ostinato::MonsterId;

// Full corpus: identity fields on both sides match the position, and one
// memcmp per record catches slot-order, packing, and symbol drift against the
// ROM bytes in a single comparison.
template <typename Table, typename Fixture>
void expectByteIdentical(const Table& table, const Fixture& fixture) {
    ASSERT_EQ(table.size(), fixture.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(fixture[i].id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, fixture[i].bytes.data(),
                              sizeof(table[i].record)), 0)
            << "formation " << i;
    }
}

TEST(Formations, AllRecordsAreByteIdenticalToRom) {
    expectByteIdentical(ostinato::formations(),
                        ostinato::test::kExpectedFormations);
}

TEST(FormationAux, AllRecordsAreByteIdenticalToRom) {
    expectByteIdentical(ostinato::formationAux(),
                        ostinato::test::kExpectedFormationAux);
}

TEST(CondBattle, AllEntriesMatchRom) {
    const auto cond = ostinato::conditionalBattles();
    const auto& fixture = ostinato::test::kExpectedConditionalBattles;
    ASSERT_EQ(cond.size(), fixture.size());
    for (std::size_t i = 0; i < cond.size(); ++i) {
        EXPECT_EQ(fixture[i].index, i);
        EXPECT_EQ(cond[i].trigger.raw, fixture[i].trigger) << "entry " << i;
        EXPECT_EQ(cond[i].replacement.raw, fixture[i].replacement)
            << "entry " << i;
    }
}

// Formation 0 (LOBO): one monster in slot 0 at (6,9) in 8px units; slots 1-5
// empty. Raw record 00 01 19 FF FF FF FF FF 69 00 00 00 00 00 3E.
TEST(Formations, Formation0Lobo) {
    const auto& f = ostinato::getFormation(FormationId::LOBO);
    EXPECT_EQ(f.vramMap(), 0);
    EXPECT_FALSE(f.slotEmpty(0));
    EXPECT_EQ(f.monsterId(0), MonsterId::LOBO);
    EXPECT_TRUE(f.isPresent(0));
    EXPECT_EQ(f.positionX(0), 6 * 8);
    EXPECT_EQ(f.positionY(0), 9 * 8);
    for (std::size_t slot = 1; slot < 6; ++slot) {
        EXPECT_TRUE(f.slotEmpty(slot)) << "slot " << slot;
        EXPECT_FALSE(f.isPresent(slot)) << "slot " << slot;
    }
}

// Formation 471 puts three distinct monsters in non-adjacent slots 0, 3, 4 —
// the ids are bit-split across the low bytes and byte 14. Confirms the split
// reassembly and the composition-derived FormationId name.
TEST(Formations, Formation471SplitIds) {
    const auto& f =
        ostinato::getFormation(FormationId::SHORT_ARM_LONG_ARM_FACE);
    EXPECT_EQ(f.vramMap(), 1);
    EXPECT_EQ(f.monsterId(0), MonsterId::SHORT_ARM);
    EXPECT_EQ(f.monsterId(3), MonsterId::LONG_ARM);
    EXPECT_EQ(f.monsterId(4), MonsterId::FACE);
    EXPECT_TRUE(f.slotEmpty(1));
    EXPECT_TRUE(f.slotEmpty(2));
    EXPECT_TRUE(f.slotEmpty(5));
}

// Formation 514 is Kefka's final battle: FINAL_KEFKA in slot 0, and its aux
// selects the FINAL_KEFKA_DESCENT entrance.
TEST(Formations, Formation514FinalKefka) {
    const auto& f = ostinato::getFormation(FormationId::FINAL_KEFKA);
    EXPECT_EQ(f.monsterId(0), MonsterId::FINAL_KEFKA);

    const auto& aux = ostinato::getFormationAux(FormationId::FINAL_KEFKA);
    EXPECT_EQ(aux.entrance(),
              ostinato::MonsterEntranceType::FINAL_KEFKA_DESCENT);
}

// Conditional battle 0 is the undead-Behemoth substitution: kill SRBEHEMOTH
// and it returns as SRBEHEMOTH_UNDEAD.
TEST(CondBattle, Entry0IsUndeadBehemoth) {
    const auto& c = ostinato::getConditionalBattle(0);
    EXPECT_EQ(c.trigger.formationId(), FormationId::SRBEHEMOTH);
    EXPECT_EQ(c.replacement.formationId(), FormationId::SRBEHEMOTH_UNDEAD);
    EXPECT_FALSE(c.trigger.randomizePlus3());
}

// LOBO's aux (raw E3 00 00 00) decodes to a front-only battle with the default
// entrance and the standard battle theme.
TEST(FormationAux, AccessorsDecodeLobo) {
    const auto& aux = ostinato::getFormationAux(FormationId::LOBO);
    EXPECT_EQ(aux.entrance(),
              ostinato::MonsterEntranceType::SLIDE_FROM_SIDES_INDIVIDUAL);
    EXPECT_TRUE(aux.frontPossible());
    EXPECT_FALSE(aux.backPossible());
    EXPECT_FALSE(aux.pincerPossible());
    EXPECT_FALSE(aux.sidePossible());
    EXPECT_FALSE(aux.characterAiEnabled());
    EXPECT_EQ(aux.characterAiIndex(), 0);
    EXPECT_EQ(aux.song(), ostinato::BattleSong::BATTLE_THEME);
}

// The FormationRef builder round-trips the formation index and the bit-15
// randomize flag.
TEST(FormationRef, BuilderRoundTrips) {
    using ostinato::FormationRef;
    const auto plain = FormationRef::of(FormationId::SRBEHEMOTH);
    EXPECT_EQ(plain.formationId(), FormationId::SRBEHEMOTH);
    EXPECT_FALSE(plain.randomizePlus3());

    const auto randomized = FormationRef::of(FormationId::LOBO, true);
    EXPECT_EQ(randomized.formationId(), FormationId::LOBO);
    EXPECT_TRUE(randomized.randomizePlus3());
}

// The unknown $40 bit of aux byte 3 has no consumer; it is preserved raw on
// exactly the two formations that carry it (384, 385) and nowhere else.
TEST(FormationAux, UnknownBit40PreservedOnTwoRows) {
    const auto aux = ostinato::formationAux();
    std::size_t carriers = 0;
    for (std::size_t i = 0; i < aux.size(); ++i) {
        if (aux[i].record.audioFlags & 0x40) {
            ++carriers;
            EXPECT_TRUE(i == 384 || i == 385) << "unexpected carrier " << i;
        }
    }
    EXPECT_EQ(carriers, 2u);
}

}  // namespace
