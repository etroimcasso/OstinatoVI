// A whole battle's scripts compared command by command (docs/contracts/battle_core.md § The battle
// whole): the cartridge's queue, params blocks and numeral buffers read into the port's commands
// (tools/damage-trace/src/oracle/battle/script_read.h), each command shown as the operands its handler in
// the graphics program reads (tools/damage-trace/src/scenarios/battle/script_shown.h), the fourth byte
// of an animation shown only where the attack wrote it, a message's numbers only where its text shows
// them, and drawn battles' scripts read whole.
//
// The tests that read bytes laid by hand run in every run of the tests; the one that asks the cartridge
// on the engine's machine skips in a brief run (OSTINATO_CARTRIDGE_CHECKS=brief, tests/with_cartridge.h).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "assets/rom_reader.h"
#include "battle/battle_script.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_background_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/esper_id.h"
#include "ostinato/item_id.h"
#include "ostinato/monster_animation.h"
#include "ostinato/monster_entrance_type.h"
#include "ostinato/monster_id.h"
#include "oracle/battle/script_read.h"
#include "oracle/damage_oracle.h"
#include "scenarios/battle/battle_draw.h"
#include "scenarios/battle/script_shown.h"
#include "scenarios/battle/whole_battle.h"
#include "scenarios/damage/damage.h"
#include "vanilla_rom.h"
#include "with_cartridge.h"

namespace ostinato::test {
namespace {

using trace::DamageOracle;
using trace::ScriptBytes;
using trace::ScriptCommandRead;

// The drawn battles EveryScriptOfTheDrawnBattlesIsRead reads.
constexpr int kDrawnBattles = 10;

// A params block: the flags, the attacker, the targets, the targets hit, the battlers reflected off or
// stepping in, the covered ally and the party's blocks, as InitGfxParams and the attack leave them.
std::array<std::uint8_t, 16> block(std::uint8_t flags, std::uint8_t attacker, std::uint16_t targets,
                                   std::uint16_t hit) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = flags;
    bytes[1] = attacker;
    bytes[2] = static_cast<std::uint8_t>(targets);
    bytes[3] = static_cast<std::uint8_t>(targets >> 8);
    bytes[4] = static_cast<std::uint8_t>(hit);
    bytes[5] = static_cast<std::uint8_t>(hit >> 8);
    return bytes;
}

class BattleScriptsOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        image_ = new std::vector<std::uint8_t>{vanillaRom()};
        content_ = new assets::IngestedContent{assets::ingestCartridge(image())};
    }
    static void TearDownTestSuite() {
        delete content_;
        content_ = nullptr;
        delete image_;
        image_ = nullptr;
    }

    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }
    static const assets::IngestedContent& content() { return *content_; }
    static const TextCorpus& text() { return content().text; }

    // A queue of the given commands with the end after them, and the bytes the reader reads beside it.
    struct Laid {
        std::vector<std::uint8_t> queue;
        std::vector<std::uint8_t> blocks = std::vector<std::uint8_t>(0x100, 0);
        std::vector<std::uint8_t> numerals = std::vector<std::uint8_t>(0xA0, 0xFF);
        std::array<std::uint8_t, 6> variables{};

        explicit Laid(std::initializer_list<std::array<std::uint8_t, 4>> commands) {
            for (const auto& command : commands) {
                queue.insert(queue.end(), command.begin(), command.end());
            }
            queue.insert(queue.end(), {0xFF, 0xFF, 0xFF, 0xFF});
        }
        void placeBlock(std::size_t index, const std::array<std::uint8_t, 16>& bytes) {
            std::copy(bytes.begin(), bytes.end(), blocks.begin() + static_cast<std::ptrdiff_t>(index * 16));
        }
        std::vector<ScriptCommandRead> read() const {
            return trace::readScript(
                ScriptBytes{.queue = queue, .paramsBlocks = blocks, .numeralBuffers = numerals, .messageVariables = variables});
        }
    };

    static std::vector<std::uint8_t>* image_;
    static assets::IngestedContent* content_;
};

std::vector<std::uint8_t>* BattleScriptsOracleTest::image_ = nullptr;
assets::IngestedContent* BattleScriptsOracleTest::content_ = nullptr;

template <typename T>
const T& as(const ScriptCommandRead& read) {
    EXPECT_TRUE(read.command.has_value());
    return std::get<T>(*read.command);
}

// Each command the battle program writes reads as the port's command its handler shows: the names and
// the monster's special name (GfxCmd_01, GfxCmd_11), a pose reset by index, a battle event, a monster's
// dialog as a word, a formation change with its scroll bit and the monsters still fighting, monsters
// entering with those revived, both forms of a monster animation, a numeral shown alone, and the step
// back. A name kind no routine writes reads as none, and the queue stops at $FF.
TEST_F(BattleScriptsOracleTest, EachCommandReadsAsThePortsCommand) {
    const Laid laid{{0x01, 0x00, 0x55, 0x00}, {0x01, 0x01, 0xE8, 0x00}, {0x01, 0x02, 0x03, 0x00},
                    {0x01, 0x04, 0x07, 0x00}, {0x01, 0x05, 0x16, 0x00}, {0x01, 0x03, 0x09, 0x00},
                    {0x11, 0x2A, 0x01, 0x00}, {0x11, 0xFF, 0xFF, 0x00}, {0x0E, 0x02, 0x00, 0x00},
                    {0x0F, 0x1B, 0x00, 0x00}, {0x10, 0x34, 0x01, 0x00}, {0x12, 0x85, 0x77, 0x03},
                    {0x13, 0x02, 0x30, 0x04}, {0x14, 0x07, 0x01, 0x00}, {0x14, 0x09, 0x40, 0x9A},
                    {0x0B, 0x05, 0x2C, 0x41}, {0x0D, 0x00, 0x00, 0x00}};
    const std::vector<ScriptCommandRead> read = laid.read();
    ASSERT_EQ(read.size(), 17u);
    EXPECT_EQ(as<battle::AttackNameShown>(read[0]).attack, AttackId::DISPATCH);
    EXPECT_EQ(as<battle::ItemNameShown>(read[1]).item, static_cast<ItemId>(0xE8));
    EXPECT_EQ(as<battle::EsperNameShown>(read[2]).esper, static_cast<EsperId>(static_cast<std::uint8_t>(EsperId::RAMUH) + 3));
    EXPECT_EQ(as<battle::BushidoNameShown>(read[3]).technique, static_cast<AttackId>(0x07));
    EXPECT_EQ(as<battle::CommandNameShown>(read[4]).command, BattleCommandId::JUMP);
    EXPECT_FALSE(read[5].command.has_value());
    EXPECT_EQ(as<battle::MonsterSpecialAttackNameShown>(read[6]).monster, static_cast<MonsterId>(0x012A));
    EXPECT_FALSE(as<battle::MonsterSpecialAttackNameShown>(read[7]).monster.has_value());
    EXPECT_EQ(as<battle::PoseReset>(read[8]).battler, battle::BattlerSlot::character(2));
    EXPECT_EQ(as<battle::BattleEventPlays>(read[9]).event, 0x1B);
    EXPECT_EQ(as<battle::MonsterDialogShown>(read[10]).dialog, 0x0134);
    const auto& formation = as<battle::FormationChanges>(read[11]);
    EXPECT_TRUE(formation.scrollBackground);
    EXPECT_EQ(formation.exit, static_cast<MonsterEntranceType>(0x05));
    EXPECT_EQ(formation.monsters,
              battle::BattlerSet::of(battle::BattlerSlot::monster(0)) | battle::BattlerSet::of(battle::BattlerSlot::monster(1)));
    const auto& entrance = as<battle::MonstersEnterOrLeave>(read[12]);
    EXPECT_EQ(entrance.type, static_cast<MonsterEntranceType>(0x02));
    EXPECT_EQ(entrance.hidden,
              battle::BattlerSet::of(battle::BattlerSlot::monster(4)) | battle::BattlerSet::of(battle::BattlerSlot::monster(5)));
    EXPECT_EQ(entrance.revived, battle::BattlerSet::of(battle::BattlerSlot::monster(2)));
    const auto& moving = as<battle::MonsterAnimationPlays>(read[13]);
    EXPECT_EQ(moving.animation, MonsterAnimation::MoveBackFar);
    EXPECT_EQ(moving.monsters, battle::BattlerSet::of(battle::BattlerSlot::monster(0)));
    const auto& sound = as<battle::MonsterAnimationPlays>(read[14]);
    EXPECT_EQ(sound.animation, MonsterAnimation::SoundEffect);
    EXPECT_EQ(sound.pan, 0x40);
    EXPECT_EQ(sound.soundEffect, 0x9A);
    const auto& numeral = as<battle::DamageNumbersShown>(read[15]);
    EXPECT_EQ(numeral.wait, battle::DamageNumberWait::CarryOn);
    ASSERT_EQ(numeral.numbers.size(), 1u);
    EXPECT_EQ(numeral.numbers[0].target, battle::BattlerSlot::monster(1));
    EXPECT_EQ(numeral.numbers[0].amount, 0x012C);
    EXPECT_TRUE(numeral.numbers[0].miss);
    EXPECT_FALSE(numeral.numbers[0].heals);
    EXPECT_TRUE(std::holds_alternative<battle::StepBack>(*read[16].command));
}

// The params blocks go to the commands that play with them, in order: an animation and a bounce off
// Reflect take one each, the pointer step ($07) passes one by, and the ready stance reads the next and
// leaves it for the command after it.
TEST_F(BattleScriptsOracleTest, TheParamsBlocksGoToTheCommandsThatPlayThem) {
    Laid laid{{0x0C, 0x02, 0x00, 0x00}, {0x06, 0x02, 0x10, 0x00}, {0x07, 0x00, 0x00, 0x00},
              {0x06, 0x02, 0x11, 0x00}, {0x09, 0x00, 0x12, 0x00}};
    laid.placeBlock(0, block(0x00, 1, 0x0100, 0x0100));
    laid.placeBlock(1, block(0x00, 2, 0x0200, 0x0200));
    laid.placeBlock(2, block(0x80, 5, 0x0001, 0x0001));
    std::array<std::uint8_t, 16> bounce{};
    bounce.fill(0xFF);
    bounce[4] = 1;
    bounce[14] = 0x00;
    bounce[15] = 0x01;
    std::fill(bounce.begin() + 10, bounce.begin() + 14, 0);
    laid.placeBlock(3, bounce);
    const std::vector<ScriptCommandRead> read = laid.read();
    ASSERT_EQ(read.size(), 5u);
    const auto& stance = as<battle::ReadyStance>(read[0]);
    EXPECT_EQ(stance.actor, battle::BattlerSlot::character(1));
    EXPECT_EQ(stance.command, BattleCommandId::MAGIC);
    EXPECT_EQ(stance.targets, battle::BattlerSet::of(battle::BattlerSlot::monster(0)));
    EXPECT_EQ(as<battle::AttackAnimation>(read[1]).params.attacker, battle::BattlerSlot::character(1));
    EXPECT_FALSE(read[2].command.has_value());
    const auto& second = as<battle::AttackAnimation>(read[3]);
    EXPECT_EQ(second.params.attacker, battle::BattlerSlot::monster(1));
    EXPECT_TRUE(second.params.attackerIsMonster);
    EXPECT_EQ(second.attack, static_cast<AttackId>(0x11));
    const auto& bounced = as<battle::ReflectedAttackShown>(read[4]);
    EXPECT_EQ(bounced.attack, static_cast<AttackId>(0x12));
    EXPECT_EQ(bounced.bouncedTo[4], battle::BattlerSlot::character(1));
    EXPECT_FALSE(bounced.bouncedTo[0].has_value());
    EXPECT_EQ(bounced.bouncesThatHit, battle::BattlerSet::of(battle::BattlerSlot::monster(0)));
}

// Each numerals-at-once command takes the next buffer, its numerals from the last battler's down, a
// battler with $FFFF showing none; a heal and a miss carry their marks.
TEST_F(BattleScriptsOracleTest, NumeralsTogetherReadTheirBuffersInTurn) {
    Laid laid{{0x03, 0x00, 0x00, 0x00}, {0x03, 0x00, 0x00, 0x00}};
    const auto word = [&](std::size_t buffer, int battler, std::uint16_t value) {
        laid.numerals[buffer * 20 + static_cast<std::size_t>(battler) * 2] = static_cast<std::uint8_t>(value);
        laid.numerals[buffer * 20 + static_cast<std::size_t>(battler) * 2 + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    word(0, 0, 0x8000 | 120);
    word(0, 6, 35);
    word(1, 4, 0x4000);
    const std::vector<ScriptCommandRead> read = laid.read();
    ASSERT_EQ(read.size(), 2u);
    const auto& first = as<battle::DamageNumbersShown>(read[0]);
    EXPECT_EQ(first.wait, battle::DamageNumberWait::UntilGone);
    ASSERT_EQ(first.numbers.size(), 2u);
    EXPECT_EQ(first.numbers[0].target, battle::BattlerSlot::monster(2));
    EXPECT_EQ(first.numbers[0].amount, 35);
    EXPECT_EQ(first.numbers[1].target, battle::BattlerSlot::character(0));
    EXPECT_EQ(first.numbers[1].amount, 120);
    EXPECT_TRUE(first.numbers[1].heals);
    const auto& second = as<battle::DamageNumbersShown>(read[1]);
    ASSERT_EQ(second.numbers.size(), 1u);
    EXPECT_EQ(second.numbers[0].target, battle::BattlerSlot::monster(0));
    EXPECT_TRUE(second.numbers[0].miss);
}

// An animation's fourth byte is shown where the attack wrote it — a weapon's swing (Fight, Capture,
// Jump), a monster's special attack, a Dance (its background, or $FF), a Sketch that hit — and left out
// for every other animation, which carries the byte the last of those left.
TEST_F(BattleScriptsOracleTest, TheFourthByteIsShownOnlyWhereTheAttackWroteIt) {
    const auto shown = [](battle::AttackAnimation animation) {
        const std::vector<battle::BattleScriptCommand> commands{animation};
        return trace::scriptShown(commands, text()).front();
    };
    const auto upTo = [](const std::string& line) { return line.substr(0, line.find(" |")); };
    battle::AttackAnimation fight{.what = BattleCommandId::FIGHT, .attack = static_cast<AttackId>(0xF7), .secondary = 0x0B};
    EXPECT_TRUE(trace::fourthByteIsTheAttacks(fight));
    EXPECT_EQ(upTo(shown(fight)), "animation 0 247 11");
    battle::AttackAnimation magic{.what = BattleCommandId::MAGIC, .attack = AttackId::FIRE, .secondary = 0x0B};
    EXPECT_FALSE(trace::fourthByteIsTheAttacks(magic));
    EXPECT_EQ(upTo(shown(magic)), "animation 2 " + std::to_string(static_cast<int>(AttackId::FIRE)));
    magic.params.monsterAttack = true;
    EXPECT_TRUE(trace::fourthByteIsTheAttacks(magic));
    battle::AttackAnimation dance{.what = BattleCommandId::DANCE, .attack = static_cast<AttackId>(0x5D), .secondary = 0x0B};
    EXPECT_EQ(trace::fourthByte(dance), 0xFF);
    dance.newBackground = BattleBackgroundId::FOREST_WOB;
    EXPECT_EQ(trace::fourthByte(dance), static_cast<std::uint8_t>(BattleBackgroundId::FOREST_WOB));
    battle::AttackAnimation sketch{.what = BattleCommandId::SKETCH, .attack = static_cast<AttackId>(0x01), .secondary = 0x02};
    EXPECT_FALSE(trace::fourthByteIsTheAttacks(sketch));
    sketch.params.targetsHit = battle::BattlerSet::of(battle::BattlerSlot::monster(2));
    EXPECT_TRUE(trace::fourthByteIsTheAttacks(sketch));
    EXPECT_TRUE(trace::fourthByteIsTheAttacks(battle::AttackAnimation{.what = BattleCommandId::CAPTURE}));
    EXPECT_TRUE(trace::fourthByteIsTheAttacks(battle::AttackAnimation{.what = BattleCommandId::JUMP}));
    EXPECT_FALSE(trace::fourthByteIsTheAttacks(battle::AttackAnimation{.what = battle::SpecialAnimation::UmaroTackle}));
}

// A message shows the numbers its text shows and no others: Scan's level the whole first number, an
// item stolen the first number's low byte, a message with no number in its text neither; a number the
// text shows and the port does not carry shows as none.
TEST_F(BattleScriptsOracleTest, AMessageShowsTheNumbersItsTextShows) {
    const auto shown = [](battle::BattleMessage message) {
        const std::vector<battle::BattleScriptCommand> commands{message};
        return trace::scriptShown(commands, text()).front();
    };
    EXPECT_EQ(shown({.message = battle::kMessageLevel, .firstNumber = 0x012345, .secondNumber = 7}),
              "message " + std::to_string(battle::kMessageLevel) + " first " + std::to_string(0x012345));
    EXPECT_EQ(shown({.message = battle::kMessageStole, .firstNumber = 0x01E8, .secondNumber = 7}),
              "message " + std::to_string(battle::kMessageStole) + " first " + std::to_string(0xE8));
    EXPECT_EQ(shown({.message = battle::kMessageCantRunAway, .firstNumber = 5, .secondNumber = 7}),
              "message " + std::to_string(battle::kMessageCantRunAway));
    EXPECT_EQ(shown({.message = battle::kMessageStole}), "message " + std::to_string(battle::kMessageStole) + " first none");

    Laid laid{{0x02, battle::kMessageLevel, 0x00, 0x00}};
    laid.variables = {0x2A, 0x00, 0x00, 0x09, 0x00, 0x00};
    const std::vector<ScriptCommandRead> script = laid.read();
    const auto& read = as<battle::BattleMessage>(script.front());
    EXPECT_EQ(read.message, battle::kMessageLevel);
    EXPECT_EQ(read.firstNumber, 0x2Au);
    EXPECT_EQ(read.secondNumber, 9u);
}

// Every script the sweep's first drawn battles play reads whole — each command as one of the port's,
// none shown by its bytes — and one is read for each call that plays a script.
TEST_F(BattleScriptsOracleTest, EveryScriptOfTheDrawnBattlesIsRead) {
    OSTINATO_SKIP_WHEN_BRIEF();
    std::map<std::string, int> kinds;
    int scripts = 0;
    trace::runOnLoop([&] {
        DamageOracle oracle{image()};
        oracle.start();
        oracle.useText(content().text);
        oracle.useMonsterSprites(content().monsterSprites);
        std::mt19937 rng{trace::kSweepSeed};
        for (int draw = 0; draw < kDrawnBattles; ++draw) {
            const trace::BattleScenario scenario =
                trace::drawBattleScenario(rng, content().text, content().monsterSprites, oracle.cartridge());
            const trace::BattleCheck check = oracle.checkBattle(scenario);
            EXPECT_EQ(check.differences, "") << "draw " << draw << ": " << trace::describe(scenario);
            std::size_t played = 0;
            for (const trace::GraphicsCall& call : check.cartridge.calls) {
                played += call.command == trace::kPlayBattleScript ? 1 : 0;
            }
            EXPECT_EQ(check.cartridge.scripts.size(), played) << "draw " << draw;
            for (const auto& [call, script] : check.cartridge.scripts) {
                ++scripts;
                for (const ScriptCommandRead& read : script) {
                    EXPECT_TRUE(read.command.has_value()) << "draw " << draw << ", call " << call << ": "
                                                          << trace::scriptShown(std::span{&read, 1}, content().text).front();
                    if (read.command) {
                        const std::string line = trace::scriptShown(std::span{&read, 1}, content().text).front();
                        ++kinds[line.substr(0, line.find_first_of("0123456789|:"))];
                    }
                }
            }
        }
    });
    RecordProperty("scripts", scripts);
    for (const auto& [kind, count] : kinds) {
        RecordProperty(kind, count);
    }
    EXPECT_GT(scripts, 0);
}

}  // namespace
}  // namespace ostinato::test
