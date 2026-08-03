// Full-corpus test of the parser-emitted enum surface + the hand-written
// foundation types (PLAN phase-1.A). The enum-value test asserts EVERY emitted
// enumerator against the parser-emitted fixture (no subset). The wrapper tests
// exercise the byte/bit mapping the port depends on for ROM byte-equivalence.

#include <cstdint>

#include <gtest/gtest.h>

// The complete emitted enum surface (23 headers).
#include "ostinato/attack_id.h"
#include "ostinato/battle_character_palette.h"
#include "ostinato/battle_command_flags.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/character_flags.h"
#include "ostinato/character_gfx_id.h"
#include "ostinato/character_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/dance_id.h"
#include "ostinato/element.h"
#include "ostinato/esper_bonus.h"
#include "ostinato/esper_id.h"
#include "ostinato/event_dir.h"
#include "ostinato/event_obj_id.h"
#include "ostinato/item_id.h"
#include "ostinato/item_type.h"
#include "ostinato/item_usage.h"
#include "ostinato/level_mod.h"
#include "ostinato/monster_id.h"
#include "ostinato/run_factor.h"
#include "ostinato/status_id.h"
#include "ostinato/target_flags.h"
#include "ostinato/weapon_flags.h"

// Hand-written foundation types.
#include "ostinato/element_set.h"
#include "ostinato/game_version.h"
#include "ostinato/status_set.h"

// Parser-emitted full-corpus expected values.
#include "fixtures/enums_expected.h"

namespace {

// Every emitted enumerator's value must equal the parser-emitted contract value.
// Full corpus — the X-macro expands to one check per enumerator (1311 of them).
TEST(Enums, AllEnumeratorsMatchContract) {
#define CHECK(EnumT, Member, Val)                                        \
    EXPECT_EQ(static_cast<std::uint32_t>(ostinato::EnumT::Member),       \
              static_cast<std::uint32_t>(Val))                           \
        << #EnumT "::" #Member;
    OSTINATO_ENUM_EXPECTED(CHECK)
#undef CHECK
}

TEST(GameVersion, LanguageAxis) {
    using ostinato::GameVersion;
    using ostinato::Language;
    EXPECT_EQ(ostinato::language(GameVersion::JP_1_0), Language::JP);
    EXPECT_EQ(ostinato::language(GameVersion::US_1_0), Language::EN);
    EXPECT_EQ(ostinato::language(GameVersion::US_1_1), Language::EN);
}

TEST(GameVersion, Revision1Axis) {
    using ostinato::GameVersion;
    EXPECT_FALSE(ostinato::isRevision1(GameVersion::JP_1_0));
    EXPECT_FALSE(ostinato::isRevision1(GameVersion::US_1_0));
    EXPECT_TRUE(ostinato::isRevision1(GameVersion::US_1_1));
}

TEST(ElementSet, SetHasClearAndByteEquivalence) {
    using ostinato::Element;
    ostinato::ElementSet s;
    EXPECT_FALSE(s.has(Element::FIRE));

    s.set(Element::FIRE);
    s.set(Element::WATER);
    EXPECT_TRUE(s.has(Element::FIRE));
    EXPECT_TRUE(s.has(Element::WATER));
    EXPECT_FALSE(s.has(Element::ICE));
    // Byte-exact: FIRE (0x01) | WATER (0x80) == 0x81.
    EXPECT_EQ(s.bits, 0x81u);

    s.clear(Element::FIRE);
    EXPECT_FALSE(s.has(Element::FIRE));
    EXPECT_EQ(s.bits, static_cast<std::uint8_t>(Element::WATER));  // 0x80
}

// The 4-byte packed layout: ids map to (byte id/8, bit id%8). Exercising ids in
// different banks verifies the cross-byte-boundary mapping (PLAN D5) that
// StatusSet must reproduce byte-for-byte against the ROM's four status bytes.
TEST(StatusSet, CrossByteBoundaryMappingIsByteExact) {
    using ostinato::StatusId;
    ostinato::StatusSet st;
    EXPECT_FALSE(st.has(StatusId::POISON));

    st.set(StatusId::POISON);  // id 2  -> byte 0, bit 2
    st.set(StatusId::SLEEP);   // id 15 -> byte 1, bit 7
    st.set(StatusId::FLOAT);   // id 31 -> byte 3, bit 7

    EXPECT_TRUE(st.has(StatusId::POISON));
    EXPECT_TRUE(st.has(StatusId::SLEEP));
    EXPECT_TRUE(st.has(StatusId::FLOAT));
    EXPECT_FALSE(st.has(StatusId::BLIND));  // id 0 -> byte 0, bit 0

    EXPECT_EQ(st.bytes[0], 0x04u);  // POISON, bit 2
    EXPECT_EQ(st.bytes[1], 0x80u);  // SLEEP, bit 7
    EXPECT_EQ(st.bytes[2], 0x00u);  // untouched bank
    EXPECT_EQ(st.bytes[3], 0x80u);  // FLOAT, bit 7

    st.clear(StatusId::SLEEP);
    EXPECT_FALSE(st.has(StatusId::SLEEP));
    EXPECT_EQ(st.bytes[1], 0x00u);
}

}  // namespace
