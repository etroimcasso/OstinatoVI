// Full-corpus tests of the six monster satellite tables: steal/drop items,
// rage, sketch, control, special-attack animation, and vertical alignment.
// The byte-equivalence tests assert EVERY packed record is byte-identical to
// the ROM bytes (no subset) and that every entry's identity field matches
// its position; the semantic tests exercise the lookups and the structural
// invariants the tables carry (rage/control BATTLE slots, control NONE
// padding, the named animation and alignment surfaces).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "data/monster_align.h"
#include "data/monster_attacks.h"
#include "data/monster_items.h"
#include "data/monster_special_anim.h"

#include "ostinato/attack_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_attack_animation.h"
#include "ostinato/monster_id.h"
#include "ostinato/monster_vertical_alignment.h"

#include "fixtures/monster_align_expected.h"
#include "fixtures/monster_control_expected.h"
#include "fixtures/monster_items_expected.h"
#include "fixtures/monster_rage_expected.h"
#include "fixtures/monster_sketch_expected.h"
#include "fixtures/monster_special_anim_expected.h"

namespace {

// Full corpus: identity fields on both sides match the position, and one
// memcmp per packed record catches slot-order, padding, and symbol drift
// against the ROM bytes in a single comparison.
template <typename Table, typename Fixture>
void expectTablesByteIdentical(const Table& table, const Fixture& fixture) {
    ASSERT_EQ(table.size(), fixture.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(fixture[i].id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(std::memcmp(&table[i].record, &fixture[i].record,
                              sizeof(fixture[i].record)), 0)
            << "monster index " << i;
    }
}

TEST(MonsterItems, AllRecordsAreByteIdenticalToRom) {
    expectTablesByteIdentical(ostinato::monsterItems(),
                              ostinato::test::kExpectedMonsterItemsEntries);
}

// The lookup indexes by MonsterId. GUARD's row is hand-traced from
// monster_items.asm's "; 0: guard" block: steal POTION/TONIC, drop
// TONIC/EMPTY.
TEST(MonsterItems, LookupSurface) {
    using ostinato::ItemId;

    const auto& guard = ostinato::getMonsterItems(ostinato::MonsterId::GUARD);
    EXPECT_EQ(guard.rareSteal, ItemId::POTION);
    EXPECT_EQ(guard.commonSteal, ItemId::TONIC);
    EXPECT_EQ(guard.rareDrop, ItemId::TONIC);
    EXPECT_EQ(guard.commonDrop, ItemId::EMPTY);
}

TEST(MonsterRage, AllRecordsAreByteIdenticalToRom) {
    expectTablesByteIdentical(ostinato::monsterRages(),
                              ostinato::test::kExpectedMonsterRageEntries);
}

// Slot 0 is structurally always BATTLE (the upstream macro supplies it —
// monster_rage.asm:3-5); the table covers the consumers' 8-bit index space
// only. GUARD's second slot is hand-traced (SPECIAL).
TEST(MonsterRage, BattleSlotInvariantAndLookup) {
    using ostinato::AttackId;

    const auto table = ostinato::monsterRages();
    EXPECT_EQ(table.size(), 256u);
    for (const auto& entry : table) {
        EXPECT_EQ(entry.record.attacks[0], AttackId::BATTLE)
            << "monster index " << static_cast<std::size_t>(entry.id);
    }

    const auto& guard = ostinato::getMonsterRage(ostinato::MonsterId::GUARD);
    EXPECT_EQ(guard.attacks[1], AttackId::SPECIAL);
}

TEST(MonsterSketch, AllRecordsAreByteIdenticalToRom) {
    expectTablesByteIdentical(ostinato::monsterSketches(),
                              ostinato::test::kExpectedMonsterSketchEntries);
}

// GUARD's and NINJA's rows are hand-traced from monster_sketch.asm's first
// invocations; slot 1 is the 3/4 pick, slot 0 the 1/4 one.
TEST(MonsterSketch, LookupSurface) {
    using ostinato::AttackId;
    using ostinato::MonsterId;

    const auto& guard = ostinato::getMonsterSketch(MonsterId::GUARD);
    EXPECT_EQ(guard.attacks[0], AttackId::BATTLE);
    EXPECT_EQ(guard.attacks[1], AttackId::BATTLE);

    const auto& ninja = ostinato::getMonsterSketch(MonsterId::NINJA);
    EXPECT_EQ(ninja.attacks[0], AttackId::FIRE_SKEAN);
    EXPECT_EQ(ninja.attacks[1], AttackId::WATER_EDGE);
}

TEST(MonsterControl, AllRecordsAreByteIdenticalToRom) {
    expectTablesByteIdentical(ostinato::monsterControls(),
                              ostinato::test::kExpectedMonsterControlEntries);
}

// Slot 0 is structurally always BATTLE and blank macro arguments emit the
// NONE sentinel (monster_control.asm:3-20). GUARD's row is all-blank;
// NINJA's is fully populated — both hand-traced.
TEST(MonsterControl, EmptySlotSentinelsAndLookup) {
    using ostinato::AttackId;
    using ostinato::MonsterId;

    for (const auto& entry : ostinato::monsterControls()) {
        EXPECT_EQ(entry.record.attacks[0], AttackId::BATTLE)
            << "monster index " << static_cast<std::size_t>(entry.id);
    }

    const auto& guard = ostinato::getMonsterControl(MonsterId::GUARD);
    EXPECT_EQ(guard.attacks[1], AttackId::NONE);
    EXPECT_EQ(guard.attacks[2], AttackId::NONE);
    EXPECT_EQ(guard.attacks[3], AttackId::NONE);

    const auto& ninja = ostinato::getMonsterControl(MonsterId::NINJA);
    EXPECT_EQ(ninja.attacks[1], AttackId::FIRE_SKEAN);
    EXPECT_EQ(ninja.attacks[2], AttackId::WATER_EDGE);
    EXPECT_EQ(ninja.attacks[3], AttackId::BOLT_EDGE);
}

// Full corpus: every animation enumerator's underlying value matches the
// ROM byte and every identity field matches its position. GUARD's CRITICAL
// and MAG_ROADER_1's WHEEL are hand-traced spot checks of the named
// surface.
TEST(MonsterSpecialAnim, AllValuesMatchRom) {
    using ostinato::MonsterAttackAnimation;
    using ostinato::MonsterId;

    const auto table = ostinato::monsterSpecialAnims();
    const auto& fixture = ostinato::test::kExpectedMonsterSpecialAnimEntries;
    ASSERT_EQ(table.size(), fixture.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(fixture[i].id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(static_cast<std::uint8_t>(table[i].specialAnim),
                  fixture[i].specialAnim)
            << "monster index " << i;
    }

    EXPECT_EQ(ostinato::monsterSpecialAnim(MonsterId::GUARD),
              MonsterAttackAnimation::CRITICAL);
    EXPECT_EQ(ostinato::monsterSpecialAnim(MonsterId::MAG_ROADER_1),
              MonsterAttackAnimation::WHEEL);
}

// Full corpus: every alignment enumerator's underlying value matches the
// ROM byte and every identity field matches its position. GUARD (ground),
// TRAPPER (ceiling — one of only three records), and PTERODON (flying) are
// hand-traced spot checks of the named surface.
TEST(MonsterAlign, AllValuesMatchRom) {
    using ostinato::MonsterId;
    using ostinato::MonsterVerticalAlignment;

    const auto table = ostinato::monsterAlignments();
    const auto& fixture = ostinato::test::kExpectedMonsterAlignEntries;
    ASSERT_EQ(table.size(), fixture.size());
    EXPECT_EQ(table.size(), 256u);
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(fixture[i].id, i) << "fixture entry " << i;
        EXPECT_EQ(static_cast<std::size_t>(table[i].id), i)
            << "table entry " << i;
        EXPECT_EQ(static_cast<std::uint8_t>(table[i].alignment),
                  fixture[i].alignment)
            << "monster index " << i;
    }

    EXPECT_EQ(ostinato::getMonsterAlignment(MonsterId::GUARD),
              MonsterVerticalAlignment::GROUND);
    EXPECT_EQ(ostinato::getMonsterAlignment(MonsterId::TRAPPER),
              MonsterVerticalAlignment::CEILING);
    EXPECT_EQ(ostinato::getMonsterAlignment(MonsterId::PTERODON),
              MonsterVerticalAlignment::FLYING);
}

}  // namespace
