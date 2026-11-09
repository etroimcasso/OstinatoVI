// The monster AI scripts as data: every script's steps turned back into bytes and compared with
// the cartridge over the whole script block, the table's shape, scripts traced by hand from the
// game's script source, and the facts about what no script holds.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <variant>
#include <vector>

#include "ai_script_bytes.h"
#include "assets/rom_reader.h"
#include "data/ai_scripts.h"
#include "data/char_ai.h"
#include "fixtures/ai_scripts_expected.h"
#include "retropp/memory_region.h"
#include "vanilla_rom.h"

namespace ostinato {
namespace {

using test::kExpectedAiScriptBytes;
using test::kExpectedAiScripts;

// Where the cartridge holds the 384 script pointers and the scripts they point into
// (battle/ai_script.asm:12-19).
constexpr std::uint32_t kPointersAt = 0xCF8400;
constexpr std::uint32_t kScriptsAt = 0xCF8700;

// The index a scripted battle's party member's script byte is added to.
constexpr std::uint16_t kCharacterScripts = 256;

std::vector<AiStep> steps(std::span<const AiStep> part) {
    return {part.begin(), part.end()};
}

// Calls `visit` with every step of every part, each part once.
template <typename Visit>
void forEachStep(Visit visit) {
    for (const AiScript& script : aiScripts()) {
        for (const AiStep& step : script.turn) {
            visit(script, step);
        }
        if (script.hasCounter() && script.counter.data() != aiScript(MonsterId::WILD_CAT).turn.data()) {
            for (const AiStep& step : script.counter) {
                visit(script, step);
            }
        }
    }
}

class AiScriptsCartridgeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    static test::IngestedCartridge* cartridge_;
};

test::IngestedCartridge* AiScriptsCartridgeTest::cartridge_ = nullptr;

// --- the table ----------------------------------------------------------------------------------

TEST(AiScriptsTest, ThereIsOneScriptPerMonsterInMonsterIdOrder) {
    ASSERT_EQ(aiScripts().size(), kAiScriptCount);
    for (std::size_t i = 0; i < aiScripts().size(); ++i) {
        EXPECT_EQ(static_cast<std::size_t>(aiScripts()[i].monster), i);
        EXPECT_EQ(&aiScript(static_cast<std::uint16_t>(i)), &aiScripts()[i]);
        EXPECT_EQ(&aiScript(static_cast<MonsterId>(i)), &aiScripts()[i]);
    }
}

TEST(AiScriptsTest, EveryPartEndsWithItsOnlyEnd) {
    for (const AiScript& script : aiScripts()) {
        for (const std::span<const AiStep> part : {script.turn, script.counter}) {
            if (part.empty()) {
                continue;
            }
            const auto ends = std::count_if(part.begin(), part.end(),
                                            [](const AiStep& step) { return step.as<ai::End>() != nullptr; });
            EXPECT_EQ(ends, 1) << "script " << static_cast<int>(script.monster);
            EXPECT_NE(part.back().as<ai::End>(), nullptr) << "script " << static_cast<int>(script.monster);
        }
        EXPECT_FALSE(script.turn.empty());
    }
}

TEST(AiScriptsTest, PartLengthsMatchTheFixture) {
    ASSERT_EQ(kExpectedAiScripts.size(), aiScripts().size());
    for (const test::ExpectedAiScript& expected : kExpectedAiScripts) {
        const AiScript& script = aiScript(expected.id);
        EXPECT_EQ(script.turn.size(), expected.turnSteps) << "script " << expected.id;
        EXPECT_EQ(script.counter.size(), expected.counterSteps) << "script " << expected.id;
        EXPECT_EQ(script.hasCounter(), expected.counterOffset.has_value()) << "script " << expected.id;
    }
}

TEST(AiScriptsTest, MoreThanHalfTheScriptsAnswerAttacks) {
    const auto answering = std::count_if(aiScripts().begin(), aiScripts().end(),
                                         [](const AiScript& script) { return script.hasCounter(); });
    EXPECT_EQ(answering, 210);
    EXPECT_EQ(aiScripts().size() - static_cast<std::size_t>(answering), 174u);
}

// Mag Roader's script has no end to its counter part, so the cartridge reads the script that
// follows it — Wild Cat's turn part — as Mag Roader's counterattacks.
TEST(AiScriptsTest, MagRoaderAnswersWithWildCatsTurnPart) {
    const AiScript& magRoader = aiScript(MonsterId::MAG_ROADER_4);
    const AiScript& wildCat = aiScript(MonsterId::WILD_CAT);
    EXPECT_EQ(magRoader.counter.data(), wildCat.turn.data());
    EXPECT_EQ(magRoader.counter.size(), wildCat.turn.size());
    EXPECT_FALSE(wildCat.hasCounter());

    // No other script's part is another script's.
    std::set<const AiStep*> seen;
    int shared = 0;
    for (const AiScript& script : aiScripts()) {
        for (const std::span<const AiStep> part : {script.turn, script.counter}) {
            if (!part.empty() && !seen.insert(part.data()).second) {
                ++shared;
            }
        }
    }
    EXPECT_EQ(shared, 1);
}

// --- against the cartridge ----------------------------------------------------------------------

// Every script's steps, turned back into bytes and laid out where the fixture says the cartridge
// holds them, are the cartridge's script block from its first byte to its last script byte.
TEST_F(AiScriptsCartridgeTest, EveryScriptsBytesAreTheCartridges) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::HiRomImage image{cartridge_->image};
    const std::span<const std::uint8_t> block = image.read(
        retropp::MemoryRegion{.at = kScriptsAt, .size = static_cast<std::uint32_t>(kExpectedAiScriptBytes)});

    std::vector<std::uint8_t> rebuilt(kExpectedAiScriptBytes, 0);
    std::vector<int> written(kExpectedAiScriptBytes, 0);
    auto place = [&](std::size_t at, std::span<const std::uint8_t> bytes) {
        ASSERT_LE(at + bytes.size(), rebuilt.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            rebuilt[at + i] = bytes[i];
            ++written[at + i];
        }
    };
    for (const test::ExpectedAiScript& expected : kExpectedAiScripts) {
        const AiScript& script = aiScript(expected.id);
        const std::vector<std::uint8_t> turn = test::aiPartBytes(script.turn);
        place(expected.turnOffset, turn);
        if (expected.counterOffset) {
            if (script.counter.data() != aiScript(MonsterId::WILD_CAT).turn.data()) {
                place(*expected.counterOffset, test::aiPartBytes(script.counter));
            }
        } else {
            // A script that answers nothing closes with a second end.
            const std::array<std::uint8_t, 1> secondEnd{0xFF};
            place(expected.turnOffset + turn.size(), secondEnd);
        }
    }
    for (std::size_t at = 0; at < written.size(); ++at) {
        ASSERT_EQ(written[at], 1) << "block offset " << at;
    }
    for (std::size_t at = 0; at < rebuilt.size(); ++at) {
        ASSERT_EQ(rebuilt[at], block[at]) << "block offset " << at;
    }
}

TEST_F(AiScriptsCartridgeTest, ThePointerTableNamesEveryTurnPart) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::HiRomImage image{cartridge_->image};
    const std::span<const std::uint8_t> pointers = image.read(
        retropp::MemoryRegion{.at = kPointersAt, .size = static_cast<std::uint32_t>(kAiScriptCount * 2)});
    for (const test::ExpectedAiScript& expected : kExpectedAiScripts) {
        const std::size_t at = static_cast<std::size_t>(expected.id) * 2;
        EXPECT_EQ(pointers[at] | pointers[at + 1] << 8, expected.turnOffset) << "script " << expected.id;
    }
}

// The setup reads a script's counter part as starting right after its turn part's end, unless the
// byte there is another end. Mag Roader's is Wild Cat's turn part for that reason.
TEST_F(AiScriptsCartridgeTest, CounterPartsStartWhereTheSetupFindsThem) {
    OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
    const assets::HiRomImage image{cartridge_->image};
    const std::span<const std::uint8_t> block = image.read(
        retropp::MemoryRegion{.at = kScriptsAt, .size = static_cast<std::uint32_t>(kExpectedAiScriptBytes)});
    for (const test::ExpectedAiScript& expected : kExpectedAiScripts) {
        const std::size_t afterTurn = expected.turnOffset + test::aiPartBytes(aiScript(expected.id).turn).size();
        if (expected.counterOffset) {
            EXPECT_EQ(*expected.counterOffset, afterTurn) << "script " << expected.id;
            EXPECT_NE(block[afterTurn], 0xFF) << "script " << expected.id;
        } else {
            EXPECT_EQ(block[afterTurn], 0xFF) << "script " << expected.id;
        }
    }
    const test::ExpectedAiScript& magRoader = kExpectedAiScripts[static_cast<std::size_t>(MonsterId::MAG_ROADER_4)];
    const test::ExpectedAiScript& wildCat = kExpectedAiScripts[static_cast<std::size_t>(MonsterId::WILD_CAT)];
    EXPECT_EQ(magRoader.counterOffset, std::optional<std::uint16_t>{wildCat.turnOffset});
}

// --- scripts traced by hand from battle/ai_script.asm --------------------------------------------

// ai_script.asm:39-47.
TEST(AiScriptsTest, LoboReadsAsItsSource) {
    const AiScript& lobo = aiScript(MonsterId::LOBO);
    EXPECT_EQ(steps(lobo.turn),
              (std::vector<AiStep>{
                  AiStep::ifLevelAtLeast({.target = AiTargetSelector::RAND_CHAR, .level = 7}),
                  AiStep::attackOneOf({AttackId::BATTLE, AttackId::SPECIAL, kNoAttack}),
                  AiStep::attackOneOf({AttackId::SPECIAL, kNoAttack, kNoAttack}),
                  AiStep::endIf(),
                  AiStep::attack(AttackId::BATTLE),
                  AiStep::end(),
              }));
    EXPECT_FALSE(lobo.hasCounter());
}

// ai_script.asm:92-100.
TEST(AiScriptsTest, RepoManReadsAsItsSource) {
    const AiScript& repoMan = aiScript(MonsterId::REPO_MAN);
    EXPECT_EQ(steps(repoMan.turn),
              (std::vector<AiStep>{
                  AiStep::attackOneOf({AttackId::BATTLE, AttackId::BATTLE, AttackId::SPECIAL}),
                  AiStep::wait(),
                  AiStep::end(),
              }));
    EXPECT_EQ(steps(repoMan.counter),
              (std::vector<AiStep>{
                  AiStep::ifDamaged(),
                  AiStep::setTarget(AiTargetSelector::SELF),
                  AiStep::attackOneOf({AttackId::SPECIAL, kNoAttack, kNoAttack}),
                  AiStep::end(),
              }));
}

// ai_script.asm:4217-4223 and 4259-4271: a block with two conditions, a condition on the monster's
// own variable, and conditions standing after an action inside a block.
TEST(AiScriptsTest, PugsReadsAsItsSource) {
    const AiScript& pugs = aiScript(MonsterId::PUGS);
    const AiMonsters third = AiMonsters::of(AiTargetSelector::MONSTER_SLOT_3);
    ASSERT_GE(pugs.turn.size(), 6u);
    EXPECT_EQ(steps(pugs.turn.first(6)),
              (std::vector<AiStep>{
                  AiStep::ifInSlot(third),
                  AiStep::ifVariableAtLeast({.variable = AiVariable::own(), .value = 8}),
                  AiStep::attack(AttackId::SPECIAL),
                  AiStep::setVariable({.variable = AiVariable::own(), .value = 0}),
                  AiStep::showOrHideMonsters({.mode = MonsterVisibilityMode::HideAndKeepBattleGoing,
                                              .monsters = third,
                                              .type = MonsterEntranceType::FADE_IN_CHECKERED}),
                  AiStep::animate({.animation = MonsterAnimation::MoveBackFar, .monsters = third}),
              }));
    ASSERT_GE(pugs.counter.size(), 12u);
    EXPECT_EQ(steps(pugs.counter.first(12)),
              (std::vector<AiStep>{
                  AiStep::ifMonstersDead(AiMonsters::self()),
                  AiStep::showOrHideMonsters({.mode = MonsterVisibilityMode::Hide,
                                              .monsters = AiMonsters::self(),
                                              .type = MonsterEntranceType::FADE_IN_DIAGONAL}),
                  AiStep::endIf(),
                  AiStep::ifInSlot(third),
                  AiStep::ifHitByCommand(BattleCommandId::FIGHT),
                  AiStep::addToVariable({.variable = AiVariable::battle(0), .amount = 1}),
                  AiStep::ifVariableAtLeast({.variable = AiVariable::own(), .value = 2}),
                  AiStep::ifVariableAtLeast({.variable = AiVariable::battle(0), .value = 3}),
                  AiStep::setVariable({.variable = AiVariable::battle(0), .value = 0}),
                  AiStep::animate({.animation = MonsterAnimation::MoveBackQuickly, .monsters = third}),
                  AiStep::subtractFromVariable({.variable = AiVariable::own(), .amount = 1}),
                  AiStep::endIf(),
              }));
}

// ai_script.asm:7924-7947. The switch Gau's script tests and sets is the game's battle switch 73.
TEST(AiScriptsTest, GauOnTheVeldtReadsAsItsSource) {
    const AiScript& gau = aiScript(MonsterId::GAU_VELDT);
    EXPECT_EQ(steps(gau.turn),
              (std::vector<AiStep>{
                  AiStep::ifSwitchClear(AiSwitch::battleSwitch(73)),
                  AiStep::dialog(35),
                  AiStep::endIf(),
                  AiStep::recruitGau(),
                  AiStep::dialog(36),
                  AiStep::endBattleLearningRages(),
                  AiStep::end(),
              }));
    EXPECT_EQ(steps(gau.counter),
              (std::vector<AiStep>{
                  AiStep::ifHitByItem(ItemId::DRIED_MEAT),
                  AiStep::ifSwitchClear(AiSwitch::battleSwitch(73)),
                  AiStep::recruitGau(),
                  AiStep::setSwitch(AiSwitch::battleSwitch(73)),
                  AiStep::battleEvent(13),
                  AiStep::endBattle(),
                  AiStep::endIf(),
                  AiStep::ifDamaged(),
                  AiStep::battleEvent(28),
                  AiStep::endBattle(),
                  AiStep::end(),
              }));
}

// --- the builders' bytes ------------------------------------------------------------------------

// Each builder against the bytes include/battle/ai_script.inc's macros assemble.
TEST(AiScriptsTest, BuildersHoldWhatTheScriptSourceWrites) {
    using Bytes = std::vector<std::uint8_t>;
    EXPECT_EQ(test::aiStepBytes(AiStep::attack(AttackId::BATTLE)), (Bytes{0xEE}));
    EXPECT_EQ(test::aiStepBytes(AiStep::attackOneOf({AttackId::BATTLE, kNoAttack, AttackId::SPECIAL})),
              (Bytes{0xF0, 0xEE, 0xFE, 0xEF}));
    EXPECT_EQ(test::aiStepBytes(AiStep::setTarget(CharacterPropId::BANON)), (Bytes{0xF1, 0x0E}));
    EXPECT_EQ(test::aiStepBytes(AiStep::setTarget(AiTargetSelector::RAND_CHAR)), (Bytes{0xF1, 68}));
    // An HP is held in steps of 128; a status and an element by their own values.
    EXPECT_EQ(test::aiStepBytes(AiStep::ifHpAtMost({.target = AiTargetSelector::SELF, .hp = 10240})),
              (Bytes{0xFC, 0x06, 54, 80}));
    EXPECT_EQ(test::aiStepBytes(AiStep::ifStatus({.target = AiTargetSelector::CHAR_SLOT_1,
                                                  .status = StatusId::REFLECT})),
              (Bytes{0xFC, 0x08, 72, 23}));
    EXPECT_EQ(test::aiStepBytes(AiStep::ifWeakTo({.target = AiTargetSelector::SELF,
                                                  .elements = ElementSet::of(Element::FIRE, Element::ICE)})),
              (Bytes{0xFC, 0x1A, 54, 0x03}));
    // The monster's own variable is variable 36; a battle switch is a bit of variables 4 to 23.
    EXPECT_EQ(test::aiStepBytes(AiStep::addToVariable({.variable = AiVariable::own(), .amount = 1})),
              (Bytes{0xF8, 36, 0x81}));
    EXPECT_EQ(test::aiStepBytes(AiStep::subtractFromVariable({.variable = AiVariable::battle(1), .amount = 1})),
              (Bytes{0xF8, 1, 0xC1}));
    EXPECT_EQ(test::aiStepBytes(AiStep::setVariable({.variable = AiVariable::battle(3), .value = 0})),
              (Bytes{0xF8, 3, 0x00}));
    EXPECT_EQ(test::aiStepBytes(AiStep::setSwitch(AiSwitch::battleSwitch(73))), (Bytes{0xF9, 1, 13, 1}));
    EXPECT_EQ(test::aiStepBytes(AiStep::toggleSwitch(AiSwitch::battleVariable(0, 2))), (Bytes{0xF9, 0, 0, 2}));
    EXPECT_EQ(test::aiStepBytes(AiStep::clearSwitch(AiSwitch::own(7))), (Bytes{0xF9, 2, 36, 7}));
    EXPECT_EQ(test::aiStepBytes(AiStep::ifSwitchSet(AiSwitch::own(0))), (Bytes{0xFC, 0x14, 36, 0}));
    // Monsters are a mask of slots; no slots is the monster itself, and all is every bit.
    EXPECT_EQ(test::aiStepBytes(AiStep::ifMonstersDead(AiMonsters::self())), (Bytes{0xFC, 0x12, 0, 0}));
    EXPECT_EQ(test::aiStepBytes(AiStep::ifMonstersAlive(
                  AiMonsters::of(AiTargetSelector::MONSTER_SLOT_1, AiTargetSelector::MONSTER_SLOT_6))),
              (Bytes{0xFC, 0x11, 0x21, 0}));
    EXPECT_EQ(test::aiStepBytes(AiStep::showOrHideMonsters({.mode = MonsterVisibilityMode::Hide,
                                                            .monsters = AiMonsters::all(),
                                                            .type = MonsterEntranceType::BOSS_DEATH})),
              (Bytes{0xF5, 12, 1, 0xFF}));
    // The two counts share a condition; a sound effect holds its pan before its number.
    EXPECT_EQ(test::aiStepBytes(AiStep::ifPartyAtLeast(2)), (Bytes{0xFC, 0x13, 0, 2}));
    EXPECT_EQ(test::aiStepBytes(AiStep::ifMonstersAtMost(1)), (Bytes{0xFC, 0x13, 1, 1}));
    EXPECT_EQ(test::aiStepBytes(AiStep::soundEffect({.sound = 48, .pan = 241})), (Bytes{0xFA, 9, 241, 48}));
    EXPECT_EQ(test::aiStepBytes(AiStep::throwItemOneOf({.twoInThree = ItemId::DIRK, .oneInThree = kNoItem})),
              (Bytes{0xF6, 1, static_cast<std::uint8_t>(ItemId::DIRK), 0xFE}));
    EXPECT_EQ(test::aiStepBytes(AiStep::makeUntargetable(AiTargetSelector::SELF)), (Bytes{0xFB, 7, 54}));
    EXPECT_EQ(test::aiStepBytes(AiStep::setStatus(StatusId::HASTE)), (Bytes{0xFB, 11, 19}));
    EXPECT_EQ(test::aiStepBytes(AiStep::hideSelf()), (Bytes{0xFB, 13, 0}));
    EXPECT_EQ(test::aiStepBytes(AiStep::wait()), (Bytes{0xFD}));
    EXPECT_EQ(test::aiStepBytes(AiStep::endIf()), (Bytes{0xFE}));
    EXPECT_EQ(test::aiStepBytes(AiStep::end()), (Bytes{0xFF}));
}

TEST(AiScriptsTest, AStepIsReadThroughItsKind) {
    const AiStep attack = AiStep::attack(AttackId::FIRE);
    ASSERT_NE(attack.as<ai::Attack>(), nullptr);
    EXPECT_EQ(attack.as<ai::Attack>()->attack, AttackId::FIRE);
    EXPECT_EQ(attack.as<ai::Wait>(), nullptr);
    EXPECT_FALSE(attack.isCondition());

    const AiStep test = AiStep::ifHpAtMost({.target = AiTargetSelector::SELF, .hp = 1280});
    EXPECT_TRUE(test.isCondition());
    ASSERT_NE(test.condition<ai::HpAtMost>(), nullptr);
    EXPECT_EQ(test.condition<ai::HpAtMost>()->hp, 1280);
    EXPECT_EQ(test.condition<ai::MpAtMost>(), nullptr);
    EXPECT_EQ(attack.condition<ai::HpAtMost>(), nullptr);
}

// --- what the scripts hold, and what none of them does ------------------------------------------

// A single attack draws nothing and is always an attack below the first one written as three
// equal choices; the three-choice step holds every other.
TEST(AiScriptsTest, ASingleAttackIsNeverOneTheGameWritesAsThreeChoices) {
    int single = 0;
    int oneOfThree = 0;
    forEachStep([&](const AiScript&, const AiStep& step) {
        if (const auto* attack = step.as<ai::Attack>()) {
            ++single;
            EXPECT_LT(static_cast<int>(attack->attack), static_cast<int>(AttackId::RIOT_BLADE));
        }
        if (step.as<ai::AttackOneOf>() != nullptr) {
            ++oneOfThree;
        }
    });
    EXPECT_GT(single, 0);
    EXPECT_GT(oneOfThree, 0);
    EXPECT_EQ(single + oneOfThree, 1561);
}

// The steps and conditions no script holds: each is still a step the port carries out.
TEST(AiScriptsTest, TheStepsNoScriptHolds) {
    int stepsSeen = 0;
    forEachStep([&](const AiScript& script, const AiStep& step) {
        ++stepsSeen;
        const int id = static_cast<int>(script.monster);
        EXPECT_EQ(step.condition<ai::Never>(), nullptr) << id;
        EXPECT_EQ(step.condition<ai::Always>(), nullptr) << id;
        EXPECT_EQ(step.condition<ai::LevelBelow>(), nullptr) << id;
        EXPECT_EQ(step.condition<ai::GauNotRecruited>(), nullptr) << id;
        if (const auto* alive = step.condition<ai::MonstersAlive>()) {
            EXPECT_FALSE(alive->monsters.isSelf()) << id;
        }
        if (const auto* effect = step.as<ai::Effect>()) {
            EXPECT_NE(effect->effect, AiEffect::EndInvincible) << id;
            EXPECT_NE(effect->effect, AiEffect::FillGauge) << id;
            EXPECT_NE(effect->effect, AiEffect::Nothing) << id;
            EXPECT_NE(effect->effect, AiEffect::ClearStatus) << id;
        }
        if (const auto* animation = step.as<ai::Animate>()) {
            EXPECT_NE(animation->animation, MonsterAnimation::PartyRunsLeftToRight) << id;
            EXPECT_NE(animation->animation, MonsterAnimation::MoveForwardFar) << id;
        }
        if (const auto* monsters = step.as<ai::ShowOrHideMonsters>()) {
            EXPECT_NE(monsters->mode, MonsterVisibilityMode::HideAndKill) << id;
        }
        if (const auto* change = step.as<ai::ChangeSwitch>()) {
            const bool ownToggled = change->change == AiSwitchChange::Toggle && !change->which.isBattleSwitch() &&
                                    change->which.variable().isOwn();
            EXPECT_FALSE(ownToggled) << id;
        }
        if (const auto* item = step.as<ai::ItemOneOf>()) {
            // Dried Meat's value is the one a three-way choice reads as no choice.
            EXPECT_NE(item->twoInThree, std::optional<ItemId>{ItemId::DRIED_MEAT}) << id;
            EXPECT_NE(item->oneInThree, std::optional<ItemId>{ItemId::DRIED_MEAT}) << id;
        }
        if (const auto* dialog = step.as<ai::Dialog>()) {
            EXPECT_LT(dialog->dialog, 256) << id;
        }
    });
    EXPECT_EQ(stepsSeen, 5492);
}

// The amounts a variable step holds, and the battle switches the scripts name.
TEST(AiScriptsTest, VariableAmountsAndBattleSwitchesInUse) {
    std::set<int> amounts;
    std::set<int> switches;
    auto note = [&](const AiSwitch& which) {
        if (which.isBattleSwitch()) {
            switches.insert(which.number());
        }
    };
    forEachStep([&](const AiScript&, const AiStep& step) {
        if (const auto* change = step.as<ai::ChangeVariable>()) {
            amounts.insert(change->amount);
        }
        if (const auto* change = step.as<ai::ChangeSwitch>()) note(change->which);
        if (const auto* set = step.condition<ai::SwitchSet>()) note(set->which);
        if (const auto* clear = step.condition<ai::SwitchClear>()) note(clear->which);
    });
    EXPECT_EQ(amounts, (std::set<int>{0, 1, 4}));
    ASSERT_FALSE(switches.empty());
    // Variables 9, 10, 12 and 13 of the script source: switches 40-47, 48-55, 64-71 and 72-79.
    for (const int number : switches) {
        const int variable = number / 8;
        EXPECT_TRUE(variable == 5 || variable == 6 || variable == 8 || variable == 9) << number;
    }
    EXPECT_TRUE(switches.contains(73));
}

// A scripted battle gives a party member the script at 256 plus its setup's script byte.
TEST(AiScriptsTest, EveryScriptedPartyMembersScriptExists) {
    int scripted = 0;
    for (const CharAiSetupEntry& setup : charAiSetups()) {
        for (const CharAiSlot& slot : setup.record.slots) {
            if (slot.isEmpty() || slot.aiScript == 0xFF) {
                continue;
            }
            const std::size_t index = kCharacterScripts + slot.aiScript;
            ASSERT_LT(index, aiScripts().size());
            EXPECT_FALSE(aiScript(static_cast<std::uint16_t>(index)).turn.empty());
            ++scripted;
        }
    }
    EXPECT_GT(scripted, 0);
}

}  // namespace
}  // namespace ostinato
