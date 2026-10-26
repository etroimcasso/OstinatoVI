// The start of a battle from a formation and the party, and what the game keeps when it ends:
// the formation chosen, every monster loaded from its record, the battle type, the starting
// gauges, the opening, the final battle's stages, and the writeback. Each expectation is the
// rule in battle/battle_main.asm, cited by line.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "battle/battle_start.h"
#include "battle/party_setup.h"
#include "battle_test_support.h"
#include "data/battle_tables.h"
#include "data/formations.h"
#include "data/monster_items.h"
#include "data/monster_properties.h"
#include "data/monster_special_anim.h"
#include "data/rng_table.h"
#include "ostinato/text_class.h"
#include "party_test_support.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using battle::BattleState;
using battle::BattleType;

constexpr std::size_t kMonsterNameBytes = 3840;

// A corpus holding only a monster-name table, every byte 0xFF but the eight-byte keys given.
TextCorpus monsterNames(std::initializer_list<std::pair<MonsterId, std::array<std::uint8_t, 8>>> keys) {
    std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
    auto& names = buffers[static_cast<std::size_t>(TextClass::MONSTER_NAME)];
    names.assign(kMonsterNameBytes, 0xFF);
    for (const auto& [monster, key] : keys) {
        std::copy(key.begin(), key.end(), names.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(monster) * 8));
    }
    return TextCorpus{std::move(buffers)};
}

const TextCorpus& noNames() {
    static const TextCorpus corpus = monsterNames({});
    return corpus;
}

// The first RNG position whose next entry lies in [low, high).
std::uint8_t positionDrawing(int low, int high) {
    for (int position = 0; position < 256; ++position) {
        const int entry = rngByte(static_cast<std::uint8_t>(position + 1));
        if (entry >= low && entry < high) {
            return static_cast<std::uint8_t>(position);
        }
    }
    ADD_FAILURE() << "no entry in [" << low << ", " << high << ")";
    return 0;
}

std::uint8_t typesOf(std::initializer_list<BattleType> types) {
    std::uint8_t mask = 0;
    for (BattleType type : types) {
        mask = static_cast<std::uint8_t>(mask | (0x10 << static_cast<int>(type)));
    }
    return mask;
}

// The first formation `wanted` accepts.
FormationId findFormation(const std::function<bool(FormationId, const Formation&, const FormationAux&)>& wanted) {
    for (const FormationEntry& entry : formations()) {
        if (wanted(entry.id, entry.record, getFormationAux(entry.id))) {
            return entry.id;
        }
    }
    ADD_FAILURE() << "no formation matches";
    return FormationId{};
}

// An ordinary battle: monsters numbered below 256, no scripted setup, not the final battle or the
// colosseum.
bool ordinary(FormationId id, const Formation& formation, const FormationAux& aux) {
    if (aux.characterAiEnabled() || id >= FormationId::UNUSED_574) {
        return false;
    }
    for (const auto& final : kFinalBattleFormations) {
        if (final == id) return false;
    }
    bool any = false;
    for (std::size_t slot = 0; slot < 6; ++slot) {
        if (formation.slotEmpty(slot)) continue;
        if (static_cast<int>(formation.monsterId(slot)) >= 256) return false;
        any = true;
    }
    return any;
}

battle::BattleStart startFor(FormationId formation) {
    battle::BattleStart start;
    start.formation = FormationRef::of(formation);
    return start;
}

game::Party threeMembers() {
    return battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR}).party;
}

// --- the formation -------------------------------------------------------------------------------

TEST(StartFormation, ARandomizedFormationAddsTheLowTwoBitsOfOneDraw) {
    // LoadBattleProp, :7956-7965: Rand & 3 added to the formation.
    for (std::uint8_t position : {std::uint8_t{0}, std::uint8_t{20}, std::uint8_t{77}, std::uint8_t{200}}) {
        BattleState state;
        state.rng = battle::BattleRng(position);
        const FormationId chosen = battle::chooseFormation(state, FormationRef::of(FormationId::LOBO, true));
        EXPECT_EQ(static_cast<int>(chosen),
                  static_cast<int>(FormationId::LOBO) + (rngByte(static_cast<std::uint8_t>(position + 1)) & 3));
        EXPECT_EQ(state.rng.position(), static_cast<std::uint8_t>(position + 1));
    }
}

TEST(StartFormation, AFormationAskedPlainlyDrawsNothing) {
    BattleState state;
    EXPECT_EQ(battle::chooseFormation(state, FormationRef::of(FormationId::LOBO)), FormationId::LOBO);
    EXPECT_EQ(state.rng.position(), 0);
}

TEST(StartFormation, EachConditionalBattleSwitchReplacesItsFormation) {
    // :7943-7955: row n of CondBattle answers switch 48 + n.
    for (std::size_t row = 0; row < 8; ++row) {
        const ConditionalBattle& conditional = getConditionalBattle(row);
        BattleState off;
        EXPECT_EQ(battle::chooseFormation(off, conditional.trigger), conditional.trigger.formationId()) << row;
        BattleState on;
        on.switches.set(game::kConditionalBattleSwitches + static_cast<int>(row));
        const FormationRef expected = conditional.replacement;
        const FormationId chosen = battle::chooseFormation(on, conditional.trigger);
        if (!expected.randomizePlus3()) {
            EXPECT_EQ(chosen, expected.formationId()) << row;
        }
    }
}

// --- the monsters --------------------------------------------------------------------------------

TEST(StartMonsters, EveryMonsterLoadsItsRecord) {
    // LoadMonsterProp :7307-7436, LoadRageProp :7504-7550, FixImmuneStatus :6359-6380 — all 384.
    for (const MonsterPropertiesEntry& entry : monsterProperties()) {
        const MonsterProperties& record = entry.record;
        BattleState state;
        battle::NullExecutor executor;
        const BattlerSlot slot = BattlerSlot::monster(0);
        battle::loadMonster(state, executor, slot, entry.id, noNames());
        const battle::Battler& monster = state.at(slot);
        SCOPED_TRACE(static_cast<int>(entry.id));

        EXPECT_EQ(monster.monster, entry.id);
        EXPECT_EQ(monster.aiScript, static_cast<std::uint16_t>(entry.id));
        EXPECT_EQ(monster.hp, record.hp);
        EXPECT_EQ(monster.maxHp, record.hp);
        EXPECT_EQ(monster.mp, record.mp);
        EXPECT_EQ(monster.maxMp, record.mp);
        EXPECT_EQ(monster.defense, record.defense);
        EXPECT_EQ(monster.magicDefense, record.magicDefense);
        EXPECT_EQ(monster.experience, record.experience);
        EXPECT_EQ(monster.gold, record.gold);
        EXPECT_EQ(monster.level, record.level);
        EXPECT_EQ(monster.speed, record.speed);
        EXPECT_EQ(monster.weaponHand.battlePower, record.attackPower);
        EXPECT_EQ(monster.weaponHand.hitRate, record.hitRate);
        EXPECT_EQ(monster.weaponHand.item, record.attackGraphic);
        EXPECT_EQ(monster.evadeFactor, battle::hitCheckFactor(record.evade));
        EXPECT_EQ(monster.magicBlockFactor, battle::hitCheckFactor(record.magicBlock));
        // :7385-7392: max HP / 512 + 16, at most 40.
        EXPECT_EQ(monster.stamina, std::min(40, 16 + record.hp / 512));
        // AddHalf :11864-11873: half again, at most 255.
        EXPECT_EQ(monster.magicPower, std::min(255, record.magicPower + record.magicPower / 2));
        EXPECT_GE(monster.vigor, 56);
        EXPECT_LE(monster.vigor, 63);

        EXPECT_EQ(monster.rareSteal, getMonsterItems(entry.id).rareSteal);
        EXPECT_EQ(monster.commonSteal, getMonsterItems(entry.id).commonSteal);
        EXPECT_EQ(monster.specialAttack.packed, record.specialAttack.packed);
        EXPECT_EQ(monster.specialAttackAnimation, monsterSpecialAnim(entry.id));
        EXPECT_EQ(monster.metamorph.packed, record.metamorph.packed);
        EXPECT_EQ(monster.monsterFlags.bits, record.battleFlags.bits);
        EXPECT_EQ(monster.human, record.traitFlags.has(MonsterTraitFlag::HUMAN));
        EXPECT_EQ(monster.undead, record.traitFlags.has(MonsterTraitFlag::UNDEAD));
        EXPECT_EQ(monster.diesAtZeroMp, record.traitFlags.has(MonsterTraitFlag::DIES_AT_ZERO_MP));
        EXPECT_EQ(monster.nameHidden, record.traitFlags.has(MonsterTraitFlag::DONT_DISPLAY_NAME));
        EXPECT_EQ(monster.impCritical, record.traitFlags.has(MonsterTraitFlag::IMP_CRITICAL));
        EXPECT_EQ(monster.elementsWeak.bits, record.weakElements.bits);
        EXPECT_EQ(monster.elementsAbsorbed.bits, record.absorbElements.bits);
        EXPECT_EQ(monster.elementsNullified.bits, record.nullifyElements.bits);

        // The status bytes, read as the loader reads them.
        const auto& b = record.innateStatuses.bytes;
        const std::uint8_t flying = b[2] & 0x01;
        const StatusSet innate{{b[0], b[1], static_cast<std::uint8_t>(b[2] & 0xFE),
                                static_cast<std::uint8_t>((b[3] | (flying << 7)) & 0x84)}};
        EXPECT_EQ(monster.innateStatuses.bytes, innate.bytes);
        const StatusSet held{{0, b[1], static_cast<std::uint8_t>(b[2] & 0xFE), static_cast<std::uint8_t>(flying << 7)}};
        EXPECT_EQ(monster.equipmentStatuses.bytes, held.bytes);
        EXPECT_EQ(monster.triggers.monsterRunic, (b[3] & 0x02) != 0);
        EXPECT_EQ(monster.triggers.piranha, (b[3] & 0x80) != 0);
        EXPECT_EQ(monster.relicEffects4.has(RelicEffect4::PROTECT_WEAK_ALLIES), (b[3] & 0x01) != 0);

        std::array<std::uint8_t, 4> inflictable{static_cast<std::uint8_t>(~record.blockedStatuses.bytes[0]),
                                                static_cast<std::uint8_t>(~record.blockedStatuses.bytes[1]),
                                                static_cast<std::uint8_t>(~record.blockedStatuses.bytes[2]), 0xFF};
        EXPECT_EQ(monster.instantDeathProtected, (inflictable[0] & 0x80) == 0);
        inflictable[0] = static_cast<std::uint8_t>(inflictable[0] | 0x80);
        if (record.nullifyElements.has(Element::POISON)) {
            inflictable[0] = static_cast<std::uint8_t>(inflictable[0] & ~0x04);
        }
        EXPECT_EQ(monster.inflictableStatuses.bytes, inflictable);
    }
}

TEST(StartMonsters, VigorIsFiftySixPlusTheLowThreeBitsOfOneDraw) {
    // :7427-7431.
    for (std::uint8_t position : {std::uint8_t{0}, std::uint8_t{9}, std::uint8_t{128}}) {
        BattleState state;
        battle::NullExecutor executor;
        state.rng = battle::BattleRng(position);
        battle::loadMonster(state, executor, BattlerSlot::monster(2), MonsterId::LOBO, noNames());
        EXPECT_EQ(state.at(BattlerSlot::monster(2)).vigor,
                  56 + (rngByte(static_cast<std::uint8_t>(position + 1)) & 7));
        EXPECT_EQ(state.rng.position(), static_cast<std::uint8_t>(position + 1));
    }
}

TEST(StartMonsters, StaminaStopsAtForty) {
    // :7385-7392: the high byte halved stops at 23 with the carry set, so 23 + 16 + 1 = 40, the
    // same as 24 + 16.
    bool sawCap = false;
    for (const MonsterPropertiesEntry& entry : monsterProperties()) {
        if (entry.record.hp >= 25 * 512) {
            BattleState state;
            battle::NullExecutor executor;
            battle::loadMonster(state, executor, BattlerSlot::monster(0), entry.id, noNames());
            EXPECT_EQ(state.at(BattlerSlot::monster(0)).stamina, 40);
            sawCap = true;
        }
    }
    EXPECT_TRUE(sawCap);
}

TEST(StartMonsters, AFirstStrikeMonsterOnTheFieldActsBeforeAnyone) {
    // CheckFirstStrike :7483-7498 and :7424-7426: the gauge full and stopped, the advance wait as
    // good as done, in the advance-wait line, and the battle menus shut.
    const auto first = std::find_if(monsterProperties().begin(), monsterProperties().end(), [](const auto& entry) {
        return entry.record.battleFlags.has(MonsterBattleFlag::FIRST_STRIKE);
    });
    ASSERT_NE(first, monsterProperties().end());
    for (bool present : {true, false}) {
        BattleState state;
        battle::NullExecutor executor;
        const BattlerSlot slot = BattlerSlot::monster(1);
        state.at(slot).present = present;
        battle::loadMonster(state, executor, slot, first->id, noNames());
        const battle::Battler& monster = state.at(slot);
        EXPECT_EQ(monster.gauge.isFull(), present);
        EXPECT_EQ(monster.atbStopped, present);
        EXPECT_EQ(state.advanceWaitQueue.contains(slot), present);
        EXPECT_EQ(state.menusDisabled, present);
        if (present) {
            EXPECT_EQ(monster.advanceWaitCounter >> 8, 0xFF);
        }
    }
}

TEST(StartMonsters, MonstersOfOneNameShareTheHighestSlotsLine) {
    // :7325-7359: the first slot from the last whose loaded monster's name matches.
    const std::array<std::uint8_t, 8> guard{1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<std::uint8_t, 8> other{8, 7, 6, 5, 4, 3, 2, 1};
    const TextCorpus text = monsterNames({{MonsterId{10}, guard}, {MonsterId{20}, guard}, {MonsterId{30}, other}});
    BattleState state;
    battle::NullExecutor executor;
    battle::loadMonster(state, executor, BattlerSlot::monster(5), MonsterId{10}, text);
    battle::loadMonster(state, executor, BattlerSlot::monster(3), MonsterId{20}, text);
    battle::loadMonster(state, executor, BattlerSlot::monster(1), MonsterId{30}, text);
    EXPECT_EQ(state.at(BattlerSlot::monster(5)).nameGroup, MonsterId{10});
    EXPECT_EQ(state.at(BattlerSlot::monster(3)).nameGroup, MonsterId{10});
    EXPECT_EQ(state.at(BattlerSlot::monster(1)).nameGroup, MonsterId{30});
}

TEST(StartMonsters, DoomGazeComesBackWithTheHpHeFledWith) {
    // :7378-7384.
    for (std::uint16_t remembered : {std::uint16_t{0}, std::uint16_t{1234}}) {
        BattleState state;
        battle::NullExecutor executor;
        state.startingFormation = FormationId::DOOM_GAZE;
        state.switches.setDoomGazeHp(remembered);
        battle::loadMonster(state, executor, BattlerSlot::monster(0), MonsterId::DOOM_GAZE, noNames());
        const auto& record = getMonsterProperties(MonsterId::DOOM_GAZE);
        EXPECT_EQ(state.doomGaze, BattlerSlot::monster(0));
        EXPECT_EQ(state.at(BattlerSlot::monster(0)).hp, remembered == 0 ? record.hp : remembered);
        EXPECT_EQ(state.at(BattlerSlot::monster(0)).maxHp, record.hp);
    }
}

class StartMonstersOnTheCartridge : public ::testing::Test {
protected:
    inline static IngestedCartridge cartridge_;
    static void SetUpTestSuite() { cartridge_ = ingestVanilla(); }
};

TEST_F(StartMonstersOnTheCartridge, EnglishNamesAreComparedEightBytesApartInTenByteRecords) {
    // :7319-7359 read the name table with an eight-byte stride; the English table's records are
    // ten bytes. Monsters with the same English name are therefore counted on separate lines
    // unless the eight bytes at their own offsets happen to match (docs/Bugs.md).
    OSTINATO_REQUIRE_CARTRIDGE(cartridge_);
    const TextCorpus& text = cartridge_.content->text;
    std::optional<std::pair<MonsterId, MonsterId>> sameName;
    for (int a = 0; a < 384 && !sameName; ++a) {
        for (int b = a + 1; b < 384 && !sameName; ++b) {
            const auto nameA = text.monsterName(static_cast<MonsterId>(a));
            const auto nameB = text.monsterName(static_cast<MonsterId>(b));
            const auto raw = text.rawBytes(TextClass::MONSTER_NAME);
            const bool sameRecord = std::equal(nameA.begin(), nameA.end(), nameB.begin(), nameB.end());
            const bool sameWindow = std::equal(raw.begin() + a * 8, raw.begin() + a * 8 + 8, raw.begin() + b * 8);
            if (sameRecord && !sameWindow) {
                sameName = std::pair{static_cast<MonsterId>(a), static_cast<MonsterId>(b)};
            }
        }
    }
    ASSERT_TRUE(sameName.has_value());
    BattleState state;
    battle::NullExecutor executor;
    battle::loadMonster(state, executor, BattlerSlot::monster(5), sameName->first, text);
    battle::loadMonster(state, executor, BattlerSlot::monster(4), sameName->second, text);
    EXPECT_EQ(state.at(BattlerSlot::monster(4)).nameGroup, sameName->second);
}

// --- the battle type -----------------------------------------------------------------------------

TEST(StartBattleType, TheRatesFavorFrontAttacksAndCountSideFirst) {
    // ChooseBattleType :7556-7578 with RandBitWithRate :13525-13545 and row 4 of RandBitRateTbl
    // (30, 7, 7, 207): each possible type takes its weight + 1, side then pincer, back, front, and
    // RandA(total) picks the first band the draw is below. All four possible: 31, 39, 47, 255.
    for (int position = 0; position < 256; ++position) {
        BattleState state;
        state.allyCount = 3;
        state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
        const int roll = (rngByte(static_cast<std::uint8_t>(position + 1)) * 255) >> 8;
        const BattleType expected = roll < 31   ? BattleType::Side
                                    : roll < 39 ? BattleType::Pincer
                                    : roll < 47 ? BattleType::Back
                                                : BattleType::Front;
        EXPECT_EQ(battle::chooseBattleType(state, typesOf({BattleType::Front, BattleType::Back, BattleType::Pincer,
                                                           BattleType::Side})),
                  expected)
            << position;
    }
}

TEST(StartBattleType, ASingleTypeIsAlwaysChosenAndStillDraws) {
    for (BattleType only : {BattleType::Front, BattleType::Back, BattleType::Pincer, BattleType::Side}) {
        BattleState state;
        state.allyCount = 1;
        state.partyRelicEffects2.set(RelicEffect2::PREVENT_BACK_PINCER);
        EXPECT_EQ(battle::chooseBattleType(state, typesOf({only})), only);
        EXPECT_EQ(state.rng.position(), 1);
    }
}

TEST(StartBattleType, FewerThanThreeAlliesRuleOutSideAttacks) {
    // :7567-7574.
    for (int position = 0; position < 256; ++position) {
        BattleState state;
        state.allyCount = 2;
        state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
        EXPECT_NE(battle::chooseBattleType(state, typesOf({BattleType::Front, BattleType::Side})), BattleType::Side);
    }
}

TEST(StartBattleType, ABackGuardRulesOutPincerAndBackAttacks) {
    // :7557-7566.
    for (int position = 0; position < 256; ++position) {
        BattleState state;
        state.allyCount = 3;
        state.partyRelicEffects2.set(RelicEffect2::PREVENT_BACK_PINCER);
        state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
        const BattleType type = battle::chooseBattleType(
            state, typesOf({BattleType::Front, BattleType::Back, BattleType::Pincer, BattleType::Side}));
        EXPECT_NE(type, BattleType::Back);
        EXPECT_NE(type, BattleType::Pincer);
    }
}

TEST(StartBattleType, PreemptiveStrikesComeOneTimeInEightInAFrontAttack) {
    // InitBattleType_00/03 :7621-7641: Rand below type × 4 + 32 — 32 front, 44 side — doubled by
    // a Gale Hairpin.
    const FormationAux& aux = getFormationAux(FormationId::LOBO);
    ASSERT_FALSE(aux.preemptiveDisabled());
    for (BattleType type : {BattleType::Front, BattleType::Side}) {
        for (bool galeHairpin : {false, true}) {
            const int chance = (32 + 4 * static_cast<int>(type)) * (galeHairpin ? 2 : 1);
            for (int position = 0; position < 256; ++position) {
                BattleState state;
                state.battleType = type;
                if (galeHairpin) state.partyRelicEffects2.set(RelicEffect2::RAISE_PREEMPTIVE_RATE);
                state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
                battle::applyBattleType(state, aux);
                EXPECT_EQ(state.preemptive, rngByte(static_cast<std::uint8_t>(position + 1)) < chance);
            }
        }
    }
}

TEST(StartBattleType, NoPreemptiveStrikeWhileMenusAreShutOrTheFormationForbidsIt) {
    // :7623-7627: no draw either.
    const FormationAux forbids = getFormationAux(findFormation(
        [](FormationId, const Formation&, const FormationAux& aux) { return aux.preemptiveDisabled(); }));
    for (bool menusShut : {true, false}) {
        BattleState state;
        state.menusDisabled = menusShut;
        state.rng = battle::BattleRng(positionDrawing(0, 1));
        battle::applyBattleType(state, menusShut ? getFormationAux(FormationId::LOBO) : forbids);
        EXPECT_FALSE(state.preemptive);
        EXPECT_EQ(state.rng.position(), positionDrawing(0, 1));
    }
}

TEST(StartBattleType, BackAndPincerAttacksMoveTheRows) {
    // InitBattleType_01/02 :7646-7666: a pincer puts everyone in the front row, a back attack
    // swaps every row; both set the back-attack flag, and anything but a front attack keeps Gau
    // away (:7585-7589).
    const FormationAux& aux = getFormationAux(FormationId::LOBO);
    for (BattleType type : {BattleType::Back, BattleType::Pincer}) {
        BattleState state;
        state.battleType = type;
        state.gauMayAppear = true;
        state.at(BattlerSlot::character(0)).backRow = true;
        battle::applyBattleType(state, aux);
        EXPECT_EQ(state.at(BattlerSlot::character(0)).backRow, false);
        EXPECT_EQ(state.at(BattlerSlot::character(1)).backRow, type == BattleType::Back);
        EXPECT_TRUE(state.backAttack);
        EXPECT_FALSE(state.gauMayAppear);
    }
}

// --- the starting gauges -------------------------------------------------------------------------

// Two party members and two monsters at speed `speed`, every gauge unset.
BattleState gaugesToFill(std::uint8_t speed) {
    BattleState state;
    for (BattlerSlot slot : {BattlerSlot::character(0), BattlerSlot::character(1), BattlerSlot::monster(0),
                             BattlerSlot::monster(1)}) {
        state.at(slot).present = true;
        state.at(slot).speed = speed;
    }
    return state;
}

// The meter InitGauge :6228-6299 leaves, replayed from the RNG table for the same draws.
std::array<int, battle::kBattlerSlots> expectedMeters(const BattleState& before) {
    std::array<int, battle::kBattlerSlots> meters{};
    std::uint8_t position = before.rng.position();
    auto draw = [&] { return rngByte(++position); };
    int empty = 0;
    for (const auto& battler : before.battlers) empty += battler.present ? 0 : 1;
    std::vector<int> places{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (int index = battle::kBattlerSlots - 1; index >= 0; --index) {
        const int picked = (draw() * static_cast<int>(places.size())) >> 8;
        const int place = places[static_cast<std::size_t>(picked)];
        places.erase(places.begin() + picked);
        const battle::Battler& battler = before.battlers[static_cast<std::size_t>(index)];
        meters[static_cast<std::size_t>(index)] = battler.gauge.meter();
        if (battler.gauge.meter() != 0xFF) continue;
        const bool enemy = index >= battle::kPartySlots || battler.actsAsEnemy;
        auto random = [&] {
            int v = battler.speed + ((draw() * battler.speed) >> 8);
            if (v > 255) return 255;
            v += place * 8;
            if (v > 255) return 255;
            v += empty * 16;
            return std::min(v, 255);
        };
        int value;
        if (enemy) {
            value = before.preemptive || before.battleType == BattleType::Side ? 1 : random();
        } else {
            if (before.preemptive || before.battleType == BattleType::Side) continue;
            value = before.battleType == BattleType::Front ? random() : place * 8;
        }
        meters[static_cast<std::size_t>(index)] = value == 255 ? 255 : value + 1;
    }
    return meters;
}

TEST(StartGauges, EveryBattleTypeAndPreemptiveStrike) {
    for (BattleType type : {BattleType::Front, BattleType::Back, BattleType::Pincer, BattleType::Side}) {
        for (bool preemptive : {false, true}) {
            for (std::uint8_t position : {std::uint8_t{0}, std::uint8_t{60}, std::uint8_t{199}}) {
                BattleState state = gaugesToFill(40);
                state.battleType = type;
                state.preemptive = preemptive;
                state.rng = battle::BattleRng(position);
                const auto expected = expectedMeters(state);
                battle::fillStartingGauges(state);
                for (int index = 0; index < battle::kBattlerSlots; ++index) {
                    EXPECT_EQ(state.battlers[static_cast<std::size_t>(index)].gauge.meter(),
                              expected[static_cast<std::size_t>(index)])
                        << "type " << static_cast<int>(type) << " preemptive " << preemptive << " slot " << index;
                }
            }
        }
    }
}

TEST(StartGauges, APreemptiveStrikeLeavesThePartyFullAndTheMonstersNearlyEmpty) {
    // :6264-6280: the party is skipped (still unset, full at its first update); monsters get 1,
    // stored as 2.
    BattleState state = gaugesToFill(40);
    state.preemptive = true;
    battle::fillStartingGauges(state);
    EXPECT_EQ(state.at(BattlerSlot::character(0)).gauge.meter(), 0xFF);
    EXPECT_EQ(state.at(BattlerSlot::monster(0)).gauge.meter(), 2);
}

TEST(StartGauges, ABackAttackSpreadsThePartyByPlaceAlone) {
    // :6268-6273: back and pincer give a party member its place × 8, plus one.
    BattleState state = gaugesToFill(200);
    state.battleType = BattleType::Back;
    battle::fillStartingGauges(state);
    for (int position = 0; position < 2; ++position) {
        const int meter = state.at(BattlerSlot::character(position)).gauge.meter();
        EXPECT_EQ((meter - 1) % 8, 0);
        EXPECT_LE(meter, 73);
    }
}

TEST(StartGauges, ARandomStartStopsAt255AndEmptySlotsCountSixteenEach) {
    BattleState fast = gaugesToFill(250);
    battle::fillStartingGauges(fast);
    EXPECT_EQ(fast.at(BattlerSlot::monster(0)).gauge.meter(), 255);
    // Six empty slots add 96 to a speed-1 battler's start.
    BattleState slow = gaugesToFill(1);
    battle::fillStartingGauges(slow);
    EXPECT_GE(slow.at(BattlerSlot::monster(0)).gauge.meter(), 1 + 96 + 1);
}

TEST(StartGauges, AGaugeAlreadySetIsKeptAndALaterFinalStageSetsNone) {
    BattleState state = gaugesToFill(40);
    state.at(BattlerSlot::monster(1)).gauge.setMeter(0);  // first strike
    battle::fillStartingGauges(state);
    EXPECT_TRUE(state.at(BattlerSlot::monster(1)).gauge.isFull());
    // Ten places, and a random start for every unset gauge — the empty slots' too (InitGauge does
    // not look at presence): nine.
    EXPECT_EQ(state.rng.position(), 10 + 9);

    BattleState stage = gaugesToFill(40);
    stage.finalBattleScroll = 0x90;
    battle::fillStartingGauges(stage);
    for (const auto& battler : stage.battlers) EXPECT_EQ(battler.gauge.meter(), 0xFF);
    EXPECT_EQ(stage.rng.position(), 10);
}

// --- the whole start -----------------------------------------------------------------------------

TEST(StartBattle, AnOrdinaryBattleOpens) {
    const FormationId formation = findFormation(ordinary);
    battle::NullExecutor executor;
    battle::BattleStart start = startFor(formation);
    start.playTimeFrames = 17;
    const BattleState state = battle::startBattle(start, threeMembers(), {}, noNames(), executor);
    EXPECT_EQ(state.formation, formation);
    EXPECT_EQ(state.startingFormation, formation);
    EXPECT_FALSE(state.finalBattleChain);
    EXPECT_FALSE(state.colosseum);
    for (int position = 0; position < 3; ++position) {
        EXPECT_TRUE(state.at(BattlerSlot::character(position)).present);
    }
    const Formation& record = getFormation(formation);
    for (int position = 0; position < 6; ++position) {
        const battle::Battler& monster = state.at(BattlerSlot::monster(position));
        EXPECT_EQ(monster.present, record.isPresent(static_cast<std::size_t>(position)));
        EXPECT_EQ(monster.monster.has_value(), !record.slotEmpty(static_cast<std::size_t>(position)));
    }
    ASSERT_FALSE(state.requests.empty());
    EXPECT_TRUE(std::holds_alternative<battle::OpenBattleScene>(state.requests.back()));
    EXPECT_FALSE(state.waitingForScript.has_value());
}

TEST(StartBattle, TheRandomNumbersBeginAtFourTimesThePlayClocksFrames) {
    // :6088-6090 seeds the RNG; LoadBattleProp's randomizing draw is the first after it.
    battle::NullExecutor executor;
    for (std::uint8_t frames : {std::uint8_t{0}, std::uint8_t{13}, std::uint8_t{59}}) {
        battle::BattleStart start;
        start.formation = FormationRef::of(FormationId::LOBO, true);
        start.playTimeFrames = frames;
        const BattleState state = battle::startBattle(start, threeMembers(), {}, noNames(), executor);
        EXPECT_EQ(static_cast<int>(state.formation),
                  static_cast<int>(FormationId::LOBO) + (rngByte(static_cast<std::uint8_t>(frames * 4 + 1)) & 3));
    }
}

TEST(StartBattle, TheSwitchesTheBattleClearsAsItStarts) {
    // :6093-6096: 63, and 64, 68 and 71 (party defeated, ran out of time, Zone Eater); 69, the
    // timer, is left.
    battle::NullExecutor executor;
    game::BattleSwitches switches;
    for (int number : {game::kClearedAtBattleStartSwitch, game::kPartyDefeatedSwitch, game::kRanOutOfTimeSwitch,
                       game::kEatenByZoneEaterSwitch, game::kTimerExpiredSwitch, 3}) {
        switches.set(number);
    }
    const BattleState state =
        battle::startBattle(startFor(findFormation(ordinary)), threeMembers(), switches, noNames(), executor);
    EXPECT_FALSE(state.switches.isSet(game::kClearedAtBattleStartSwitch));
    EXPECT_FALSE(state.switches.isSet(game::kPartyDefeatedSwitch));
    EXPECT_FALSE(state.switches.isSet(game::kRanOutOfTimeSwitch));
    EXPECT_FALSE(state.switches.isSet(game::kEatenByZoneEaterSwitch));
    EXPECT_TRUE(state.switches.isSet(game::kTimerExpiredSwitch));
    EXPECT_TRUE(state.switches.isSet(3));
}

TEST(StartBattle, TheStorySwitchesSetTheBattlesRules) {
    battle::NullExecutor executor;
    game::BattleSwitches switches;
    switches.set(game::kMagicOnlySwitch);
    switches.set(game::kMorphAvailableSwitch);
    switches.set(game::kGauObtainedSwitch);
    switches.set(game::kShadowStaysSwitch);
    const BattleState state =
        battle::startBattle(startFor(findFormation(ordinary)), threeMembers(), switches, noNames(), executor);
    EXPECT_TRUE(state.magicOnly);
    EXPECT_TRUE(state.morphAvailable);
    EXPECT_FALSE(state.permanentMorph);
    EXPECT_TRUE(state.gauObtained);
    EXPECT_TRUE(state.shadowStays);
}

TEST(StartBattle, TheOpeningMessageNamesTheBattleType) {
    // :6138-6161: back 35, pincer 37, side 36, preemptive 34 (overriding side); none for a front
    // attack without one, and none at all where the formation forbids preemptive strikes.
    const FormationId formation = findFormation([](FormationId id, const Formation& record, const FormationAux& aux) {
        return ordinary(id, record, aux) && !aux.preemptiveDisabled() && aux.frontPossible() && aux.backPossible() &&
               aux.pincerPossible() && aux.sidePossible();
    });
    battle::NullExecutor executor;
    std::array<bool, 4> seenType{};
    bool seenPreemptive = false;
    for (std::uint8_t frames = 0; frames < 60; ++frames) {
        battle::BattleStart start = startFor(formation);
        start.playTimeFrames = frames;
        const BattleState state = battle::startBattle(start, threeMembers(), {}, noNames(), executor);
        seenType[static_cast<std::size_t>(state.battleType)] = true;
        seenPreemptive = seenPreemptive || state.preemptive;
        std::optional<std::uint8_t> expected;
        switch (state.battleType) {
            case BattleType::Back: expected = 35; break;
            case BattleType::Pincer: expected = 37; break;
            case BattleType::Side: expected = state.preemptive ? 34 : 36; break;
            case BattleType::Front: if (state.preemptive) expected = 34; break;
        }
        if (!expected) {
            EXPECT_TRUE(state.immediateActions.empty()) << static_cast<int>(frames);
            continue;
        }
        ASSERT_EQ(state.immediateActions.size(), 1u) << static_cast<int>(frames);
        EXPECT_EQ(std::get<battle::BattleMessageAction>(state.immediateActions[0]).message, *expected);
    }
    EXPECT_TRUE(seenType[0]);
    EXPECT_TRUE(seenPreemptive);
}

TEST(StartBattle, AFormationThatForbidsPreemptiveStrikesOpensSilently) {
    const FormationId formation = findFormation([](FormationId id, const Formation& record, const FormationAux& aux) {
        return ordinary(id, record, aux) && aux.preemptiveDisabled() && !aux.frontPossible();
    });
    battle::NullExecutor executor;
    const BattleState state = battle::startBattle(startFor(formation), threeMembers(), {}, noNames(), executor);
    EXPECT_NE(state.battleType, BattleType::Front);
    EXPECT_TRUE(state.immediateActions.empty());
}

TEST(StartBattle, EveryBattlerHasItsRecalculationsPendingAndItsDamageOverTimeBeatDrawn) {
    // :6097-6106: the pending updates are worked out once by AfterAction2 at the start, so
    // afterwards none are left; the ATB rate is one of them.
    battle::NullExecutor executor;
    const BattleState state =
        battle::startBattle(startFor(findFormation(ordinary)), threeMembers(), {}, noNames(), executor);
    for (const auto& battler : state.battlers) {
        if (!battler.present) continue;
        EXPECT_NE(battler.atbRate, 0);
        EXPECT_EQ(battler.condemnedNumber, 0);
    }
}

TEST(StartBattle, AColosseumMatchPutsTheFighterFirstAndTheOpponentInEverySlot) {
    // InitParty :7748-7756, InitMonsters :7693-7698.
    battle::NullExecutor executor;
    ASSERT_FALSE(getFormationAux(FormationId::PUGS).characterAiEnabled());
    battle::BattleStart start = startFor(FormationId::PUGS);
    start.colosseum = battle::ColosseumMatch{.wager = ItemId::ELIXIR, .prize = ItemId::MEGALIXIR,
                                             .opponent = MonsterId::LOBO, .fighter = CharacterPropId::LOCKE};
    const BattleState state = battle::startBattle(start, threeMembers(), {}, noNames(), executor);
    EXPECT_TRUE(state.colosseum);
    EXPECT_EQ(state.at(BattlerSlot::character(0)).actor, CharacterPropId::LOCKE);
    EXPECT_FALSE(state.at(BattlerSlot::character(1)).actor.has_value());
    const Formation& record = getFormation(FormationId::PUGS);
    for (int position = 0; position < 6; ++position) {
        const bool below256 = (record.monsterIdHigh & (1u << position)) == 0;
        EXPECT_EQ(state.at(BattlerSlot::monster(position)).monster,
                  below256 ? std::optional<MonsterId>(MonsterId::LOBO) : std::nullopt);
    }
}

// --- the final battle ----------------------------------------------------------------------------

game::Party sixInLine() {
    return battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR,
                            CharacterPropId::SABIN, CharacterPropId::CELES, CharacterPropId::SETZER})
        .party;
}

battle::BattleStart finalBattle() {
    battle::BattleStart start = startFor(kFinalBattleFormations[0]);
    start.finalBattleOrder = {4, 0, 1, 2, 3, 5};
    return start;
}

TEST(StartFinalBattle, TheFirstStageFieldsTheFirstFourInLine) {
    // InitParty :7721-7747 with the order the player chose.
    battle::NullExecutor executor;
    const BattleState state = battle::startBattle(finalBattle(), sixInLine(), {}, noNames(), executor);
    EXPECT_TRUE(state.finalBattleChain);
    EXPECT_EQ(state.at(BattlerSlot::character(0)).actor, CharacterPropId::CELES);
    EXPECT_EQ(state.at(BattlerSlot::character(1)).actor, CharacterPropId::TERRA);
    EXPECT_EQ(state.at(BattlerSlot::character(2)).actor, CharacterPropId::LOCKE);
    EXPECT_EQ(state.at(BattlerSlot::character(3)).actor, CharacterPropId::EDGAR);
    EXPECT_EQ(state.finalBattleReserve.size(), 2u);
}

TEST(StartFinalBattle, TheNextStageKeepsTheStandingAndReplacesTheFallen) {
    // BattleEnd_06 :12204-12239: a fallen, absent or air-anchored fighter makes way, the standing
    // keep their statuses less Dance, Rage and Chant, every monster's are cleared, and the stage
    // opens with the background scrolling to it and no gauge set.
    battle::NullExecutor executor;
    const battle::BattleStart start = finalBattle();
    BattleState finished = battle::startBattle(start, sixInLine(), {}, noNames(), executor);
    finished.at(BattlerSlot::character(1)).status.set(StatusId::DEAD);
    finished.at(BattlerSlot::character(2)).status.set(StatusId::DANCE);
    finished.at(BattlerSlot::character(2)).status.set(StatusId::PROTECT);
    finished.at(BattlerSlot::monster(0)).status.set(StatusId::SLOW);
    finished.outcome = battle::BattleEnded{.reason = battle::BattleEndReason::FinalBattleNextStage,
                                           .nextFormation = kFinalBattleFormations[1]};
    const battle::BattleResult handedBack = battle::writeBack(finished);
    const BattleState next = battle::startNextFinalBattleStage(finished, handedBack, start, noNames(), executor);

    EXPECT_EQ(next.formation, kFinalBattleFormations[1]);
    EXPECT_EQ(next.startingFormation, kFinalBattleFormations[0]);
    EXPECT_EQ(next.at(BattlerSlot::character(0)).actor, CharacterPropId::CELES);
    EXPECT_EQ(next.at(BattlerSlot::character(1)).actor, CharacterPropId::SABIN);  // next in line
    EXPECT_EQ(next.at(BattlerSlot::character(2)).actor, CharacterPropId::LOCKE);
    EXPECT_FALSE(next.at(BattlerSlot::character(2)).status.has(StatusId::DANCE));
    EXPECT_TRUE(next.at(BattlerSlot::character(2)).status.has(StatusId::PROTECT));
    EXPECT_FALSE(next.at(BattlerSlot::monster(0)).status.has(StatusId::SLOW));
    EXPECT_EQ(next.finalBattleScroll, kFinalBattleScroll[0].scroll);
    ASSERT_EQ(next.requests.size(), 1u);
    const auto& script = std::get<battle::RunBattleScript>(next.requests[0]);
    ASSERT_EQ(script.commands.size(), 1u);
    const auto& arrives = std::get<battle::FinalBattleStageArrives>(script.commands[0]);
    EXPECT_EQ(arrives.scroll, kFinalBattleScroll[0].scroll);
    EXPECT_EQ(arrives.monsters, next.alive.monstersOnly());
    EXPECT_TRUE(next.waitingForScript.has_value());
    for (const auto& battler : next.battlers) EXPECT_EQ(battler.gauge.meter(), 0xFF);
}

TEST(StartFinalBattle, AFighterStillInTheAirComesDownAsTheStageOpens) {
    // :6164-6179: queued as a Jump with its gauge full.
    battle::NullExecutor executor;
    const battle::BattleStart start = finalBattle();
    BattleState finished = battle::startBattle(start, sixInLine(), {}, noNames(), executor);
    finished.awayFromTheField.add(BattlerSlot::character(3));
    finished.outcome = battle::BattleEnded{.reason = battle::BattleEndReason::FinalBattleNextStage,
                                           .nextFormation = kFinalBattleFormations[1]};
    const BattleState next =
        battle::startNextFinalBattleStage(finished, battle::writeBack(finished), start, noNames(), executor);
    const battle::Battler& jumper = next.at(BattlerSlot::character(3));
    EXPECT_TRUE(next.actionQueue.contains(BattlerSlot::character(3)));
    EXPECT_TRUE(jumper.gauge.isFull());
    EXPECT_TRUE(jumper.atbStopped);
    ASSERT_FALSE(jumper.actions.empty());
    EXPECT_EQ(std::get<battle::CommandAction>(jumper.actions.back()).command, BattleCommandId::JUMP);
}

// --- the writeback -------------------------------------------------------------------------------

BattleState finishedBattle(battle::BattleOutcome outcome) {
    battle::NullExecutor executor;
    BattleState state = battle::startBattle(startFor(findFormation(ordinary)), threeMembers(), {}, noNames(), executor);
    state.outcome = outcome;
    return state;
}

TEST(WriteBack, HpMpAndTheLastingStatusesGoBackToTheRecords) {
    // UpdateSRAM :12092-12122: status 1 whole, and Float and Interceptor of status 4.
    BattleState state = finishedBattle(battle::BattleWon{});
    battle::Battler& terra = state.at(BattlerSlot::character(0));
    terra.hp = 7;
    terra.maxMp = 50;
    terra.mp = 3;
    terra.status = StatusSet::of(StatusId::POISON, StatusId::FLOAT, StatusId::PROTECT, StatusId::INTERCEPTOR);
    battle::Battler& edgar = state.at(BattlerSlot::character(2));
    edgar.maxMp = 0;
    edgar.mp = 99;
    const battle::BattleResult result = battle::writeBack(state);
    const auto& record = result.party.characters[*terra.record];
    EXPECT_EQ(record.hp, 7);
    EXPECT_EQ(record.mp, 3);
    EXPECT_EQ(record.status.bytes, StatusSet::of(StatusId::POISON, StatusId::FLOAT, StatusId::INTERCEPTOR).bytes);
    EXPECT_NE(result.party.characters[*edgar.record].mp, 99);  // no MP to keep
}

TEST(WriteBack, AFallenGhostAndAPossessedCharacterLeaveTheParty) {
    // :12094-12106.
    for (bool ghost : {true, false}) {
        BattleState state = finishedBattle(battle::BattleWon{});
        const BattlerSlot slot = BattlerSlot::character(1);
        battle::Battler& battler = state.at(slot);
        if (ghost) {
            battler.actor = CharacterPropId::GHOST_1;
            battler.status.set(StatusId::PETRIFY);
        } else {
            state.possessed.add(slot);
        }
        const battle::BattleResult result = battle::writeBack(state);
        EXPECT_EQ(result.party.membership[*battler.record].party, 0);
        EXPECT_EQ(result.party.membership[*state.at(BattlerSlot::character(0)).record].party, 1);
    }
}

TEST(WriteBack, TheInventoryGoesBackWithEmptySlotsAtZero) {
    // :12123-12134.
    BattleState state = finishedBattle(battle::BattleWon{});
    state.inventory.items[0] = battle::BattleItem{.item = ItemId::POTION, .quantity = 4};
    state.inventory.items[1] = battle::BattleItem{.item = ItemId::EMPTY, .quantity = 9};
    const battle::BattleResult result = battle::writeBack(state);
    EXPECT_EQ(result.party.inventory[0].item, ItemId::POTION);
    EXPECT_EQ(result.party.inventory[0].quantity, 4);
    EXPECT_EQ(result.party.inventory[1].item, ItemId::EMPTY);
    EXPECT_EQ(result.party.inventory[1].quantity, 0);
}

TEST(WriteBack, TheColosseumWagerIsGivenUpUnlessTimeRanOut) {
    // :12135-12150; BattleEnd_01 :11972-11976 forgets the wager.
    for (bool ranOut : {false, true}) {
        BattleState state = finishedBattle(ranOut ? battle::BattleOutcome{battle::BattleEnded{
                                                        .reason = battle::BattleEndReason::RanOutOfTime}}
                                                  : battle::BattleOutcome{battle::BattleLost{}});
        state.colosseum = true;
        state.colosseumWager = ItemId::ELIXIR;
        state.inventory.items[5] = battle::BattleItem{.item = ItemId::ELIXIR, .quantity = 2};
        state.inventory.items[6] = battle::BattleItem{.item = ItemId::ELIXIR, .quantity = 1};
        const battle::BattleResult result = battle::writeBack(state);
        EXPECT_EQ(result.party.inventory[5].quantity, ranOut ? 2 : 1);
        EXPECT_EQ(result.party.inventory[6].item, ranOut ? ItemId::ELIXIR : ItemId::EMPTY);
    }
}

TEST(WriteBack, HowTheBattleEndedSetsItsSwitch) {
    // LoseBattle :15590, BattleEnd_01 :11975, CheckBattleEnd :11900-11914.
    struct Case {
        battle::BattleOutcome outcome;
        int number;
    };
    const std::vector<Case> cases{
        {battle::BattleLost{}, game::kPartyDefeatedSwitch},
        {battle::BattleEnded{.reason = battle::BattleEndReason::RanOutOfTime}, game::kRanOutOfTimeSwitch},
        {battle::BattleEnded{.reason = battle::BattleEndReason::TimerExpired}, game::kTimerExpiredSwitch},
        {battle::BattleEnded{.reason = battle::BattleEndReason::EatenByZoneEater}, game::kEatenByZoneEaterSwitch},
    };
    for (const Case& c : cases) {
        const battle::BattleResult result = battle::writeBack(finishedBattle(c.outcome));
        EXPECT_TRUE(result.switches.isSet(c.number)) << c.number;
    }
    const battle::BattleResult won = battle::writeBack(finishedBattle(battle::BattleWon{}));
    EXPECT_FALSE(won.switches.isSet(game::kPartyDefeatedSwitch));
}

TEST(WriteBack, DoomGazesHpIsKept) {
    BattleState state = finishedBattle(battle::BattleWon{});
    state.doomGaze = BattlerSlot::monster(0);
    state.at(BattlerSlot::monster(0)).hp = 4321;
    EXPECT_EQ(battle::writeBack(state).switches.doomGazeHp(), 4321);
}

TEST(WriteBack, ShadowWalkingOffTakesHimAndHisEsperOutOfTheParty) {
    // ShadowLeaves :11990-11997.
    BattleState state = finishedBattle(battle::BattleWon{});
    state.party.characters[3] = startingRecord(CharacterPropId::SHADOW);
    state.party.characters[3].esper = EsperId::RAMUH;
    state.party.membership[3] = {.party = 1, .slot = 3};
    state.shadowLeftParty = true;
    const battle::BattleResult result = battle::writeBack(state);
    EXPECT_TRUE(result.shadowLeftParty);
    EXPECT_EQ(result.party.membership[3].party, 0);
    EXPECT_FALSE(result.party.characters[3].esper.has_value());
}

TEST(WriteBack, AFormationJoinsTheVeldtWhenItsMonstersCanBeFoundThere) {
    // :12163-12178: a monster below 256, a formation below 512, and the formation's leave.
    BattleState allowed = finishedBattle(battle::BattleWon{});
    ASSERT_FALSE(getFormationAux(allowed.formation).veldtDisabled());
    EXPECT_EQ(battle::writeBack(allowed).veldtFormation, allowed.startingFormation);

    const FormationId kept = findFormation([](FormationId id, const Formation& record, const FormationAux& aux) {
        return ordinary(id, record, aux) && aux.veldtDisabled();
    });
    battle::NullExecutor executor;
    BattleState barred = battle::startBattle(startFor(kept), threeMembers(), {}, noNames(), executor);
    barred.outcome = battle::BattleWon{};
    EXPECT_FALSE(battle::writeBack(barred).veldtFormation.has_value());

    BattleState late = finishedBattle(battle::BattleWon{});
    late.startingFormation = static_cast<FormationId>(512);
    late.formation = static_cast<FormationId>(512);
    if (!getFormationAux(late.formation).veldtDisabled()) {
        EXPECT_FALSE(battle::writeBack(late).veldtFormation.has_value());
    }
}

}  // namespace
}  // namespace ostinato::test
