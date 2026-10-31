// The battle's presentation logic, frame by frame: scripts played command by command, the names and
// messages at the top of the screen and how long each stays up, the damage numerals, the monsters'
// deaths, the list of monster names and the party's poses.
//
// A real battle is started from the cartridge so the text and the battlers are the game's; the
// requests are built by hand. Fails without a cartridge, as every cartridge-reading test does.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "assets/cartridge.h"
#include "battle/action_executor.h"
#include "battle/battle_sim.h"
#include "battle/battle_start.h"
#include "battle/presentation.h"
#include "data/battle_gfx_tables.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/formation_id.h"
#include "ostinato/formation_ref.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"
#include "vanilla_rom.h"

namespace ostinato::battle {
namespace {

class BattlePresentationTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { cartridge_ = new test::IngestedCartridge{test::ingestVanilla()}; }
    static void TearDownTestSuite() {
        delete cartridge_;
        cartridge_ = nullptr;
    }

    // A battle with Lobo, the party Terra, Locke and Edgar, its scene opened.
    void SetUp() override {
        OSTINATO_REQUIRE_CARTRIDGE(*cartridge_);
        content_ = &*cartridge_->content;
        game::Party party;
        const CharacterPropId actors[]{CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR};
        for (std::size_t i = 0; i < 3; ++i) {
            party.characters[i] = game::startingRecord(actors[i]);
            party.membership[i] = {.party = 1, .slot = static_cast<std::uint8_t>(i)};
        }
        party.currentParty = 1;
        sim_.emplace(BattleStart{.formation = FormationRef::of(FormationId::LOBO)}, party, game::BattleSwitches{},
                     content_->text, content_->monsterSprites, executor_);
        tick({OpenBattleScene{}});
    }

    BattlePresentationContext context(std::uint8_t speed = kNewGameMessageSpeed) const {
        return {.text = content_->text, .metrics = content_->dialogFont.metrics, .messageSpeed = speed};
    }

    void tick(std::vector<BattleRequest> requests = {}, bool confirm = false, std::uint8_t speed = kNewGameMessageSpeed) {
        tickBattlePresentation(shown_, sim_->state(), requests, context(speed), confirm);
    }

    void play(std::vector<BattleScriptCommand> commands, std::uint8_t speed = kNewGameMessageSpeed) {
        tick({RunBattleScript{.commands = std::move(commands)}}, false, speed);
    }

    // Frames until the presentation is idle, counting the one just played.
    int framesUntilIdle(int limit = 1000) {
        int frames = 1;
        while (!shown_.idle && frames < limit) {
            tick();
            ++frames;
        }
        return frames;
    }

    std::vector<std::uint16_t> glyphsShown() const {
        std::vector<std::uint16_t> out;
        if (shown_.message) {
            for (const ShownGlyph& glyph : shown_.message->shown.glyphs) {
                out.push_back(glyph.glyph);
            }
        }
        return out;
    }

    BattleState& state() { return sim_->state(); }

    static test::IngestedCartridge* cartridge_;
    const assets::IngestedContent* content_ = nullptr;
    ActionExecutor executor_;
    std::optional<BattleSim> sim_;
    BattlePresentationState shown_;
};

test::IngestedCartridge* BattlePresentationTest::cartridge_ = nullptr;

const BattlerSlot kLobo = BattlerSlot::monster(0);

// --- the scene ---------------------------------------------------------------------------------

TEST_F(BattlePresentationTest, OpeningTheSceneShowsTheMonstersStandingAndListsTheirNames) {
    EXPECT_TRUE(shown_.sceneOpen);
    EXPECT_FALSE(shown_.sceneClosed);
    EXPECT_TRUE(shown_.monstersShown.has(kLobo));
    EXPECT_EQ(shown_.monsterNames[0], MonsterId::LOBO);
    EXPECT_FALSE(shown_.monsterNames[1].has_value());
    EXPECT_TRUE(shown_.idle);
    tick({CloseBattleScene{}});
    EXPECT_TRUE(shown_.sceneClosed);
}

// --- names in the narrow window ------------------------------------------------------------------

// "Fire" past the spell's symbol, each letter 0x60 below its menu byte: F i r e = 85 a2 ab 9e.
TEST_F(BattlePresentationTest, ASpellsNameAppearsWholeInTheNarrowWindowAndStaysForTheMessageTime) {
    play({AttackNameShown{.attack = AttackId::FIRE}});
    ASSERT_TRUE(shown_.message);
    EXPECT_EQ(shown_.message->window, MessageWindow::Narrow);
    EXPECT_EQ(glyphsShown(), (std::vector<std::uint16_t>{0x25, 0x42, 0x4b, 0x3e}));
    EXPECT_FALSE(shown_.idle);
    // The frame it appears, then 64 more at the new game's speed; gone on the next.
    for (int frame = 1; frame <= 64; ++frame) {
        tick();
        ASSERT_TRUE(shown_.message) << frame;
    }
    tick();
    EXPECT_FALSE(shown_.message);
    EXPECT_TRUE(shown_.idle);
}

TEST_F(BattlePresentationTest, EachMessageSpeedHoldsANameForItsOwnTime) {
    for (std::uint8_t speed = 0; speed <= kSlowestMessageSpeed; ++speed) {
        play({CommandNameShown{.command = BattleCommandId::FIGHT}}, speed);
        EXPECT_EQ(framesUntilIdle(), 1 + messageHoldFrames(speed) + 1) << int{speed};
    }
}

// "Fight": 85 a2 a0 a1 ad.
TEST_F(BattlePresentationTest, ACommandsNameIsReadFromTheCommandNames) {
    play({CommandNameShown{.command = BattleCommandId::FIGHT}});
    EXPECT_EQ(glyphsShown(), (std::vector<std::uint16_t>{0x25, 0x42, 0x40, 0x41, 0x4d}));
}

// "Dispatch" is the first Bushido name: 83 a2 ac a9 9a ad 9c a1.
TEST_F(BattlePresentationTest, ABushidoTechniqueIsReadFromTheBushidoNamesFromDispatch) {
    play({BushidoNameShown{.technique = AttackId::DISPATCH}});
    EXPECT_EQ(glyphsShown(), (std::vector<std::uint16_t>{0x23, 0x42, 0x4c, 0x49, 0x3a, 0x4d, 0x3c, 0x41}));
}

// Summoning Ramuh shows his attack, "Bolt Fist": 81 a8 a5 ad fe 85 a2 ac ad.
TEST_F(BattlePresentationTest, AnEspersNameShowsItsAttacksName) {
    play({EsperNameShown{.esper = EsperId::RAMUH}});
    EXPECT_EQ(glyphsShown(), (std::vector<std::uint16_t>{0x21, 0x48, 0x45, 0x4d, 0x9e, 0x25, 0x42, 0x4c, 0x4d}));
}

// An attack in the espers' range is named by the esper: attack 0x36 is "Ramuh", 91 9a a6 ae a1.
TEST_F(BattlePresentationTest, AnAttackInTheEspersRangeIsNamedByItsEsper) {
    play({AttackNameShown{.attack = static_cast<AttackId>(0x36)}});
    EXPECT_EQ(glyphsShown(), (std::vector<std::uint16_t>{0x31, 0x3a, 0x46, 0x4e, 0x41}));
}

TEST_F(BattlePresentationTest, AnItemsNameSkipsItsSymbolAndNoItemIsARowOfBlanks) {
    play({ItemNameShown{.item = ItemId::DIRK}});
    const std::span<const std::uint8_t> dirk = content_->text.itemName(ItemId::DIRK);
    ASSERT_EQ(glyphsShown().size(), 4u);  // D i r k
    EXPECT_EQ(glyphsShown()[0], dirk[1] - 0x60);
    framesUntilIdle();
    play({ItemNameShown{.item = ItemId::EMPTY}});
    EXPECT_EQ(glyphsShown(), std::vector<std::uint16_t>(content_->text.itemName(ItemId::EMPTY).size(), 0x9f));
}

TEST_F(BattlePresentationTest, AMonstersSpecialAttackNameStays32FramesWhateverTheSpeed) {
    play({MonsterSpecialAttackNameShown{.monster = MonsterId::LOBO}}, 5);
    EXPECT_EQ(shown_.message->window, MessageWindow::Narrow);
    EXPECT_EQ(framesUntilIdle(), 1 + kSpecialAttackNameFrames + 1);
}

// The narrow window's text moves right by half of what it falls short of 104 pixels, and 2 more.
TEST_F(BattlePresentationTest, TheNarrowWindowCentersItsNameFromThePen) {
    play({AttackNameShown{.attack = AttackId::FIRE}});
    int pen = 0;
    for (const std::uint16_t glyph : glyphsShown()) {
        pen += content_->dialogFont.metrics.width(glyph);
    }
    EXPECT_EQ(shown_.message->shown.centering, (104 - pen) / 2 + 2);
    EXPECT_GT(shown_.message->shown.centering, 2);
}

// --- messages in the wide window -----------------------------------------------------------------

// Battle message 52 is "Level {var0}".
TEST_F(BattlePresentationTest, ABattleMessageFillsInItsNumberWithoutLeadingZeros) {
    play({BattleMessage{.message = kMessageLevel, .firstNumber = 37}});
    ASSERT_TRUE(shown_.message);
    EXPECT_EQ(shown_.message->window, MessageWindow::Wide);
    const std::vector<std::uint16_t> glyphs = glyphsShown();
    ASSERT_GE(glyphs.size(), 2u);
    EXPECT_EQ(glyphs[glyphs.size() - 2], text::kEnglishDigitGlyph + 3);
    EXPECT_EQ(glyphs.back(), text::kEnglishDigitGlyph + 7);
}

// Message 41 is "Annihilated" and waits for a key: it stays up until the player confirms and closes
// the frame after, without its hold.
TEST_F(BattlePresentationTest, AMessageThatWaitsForAKeyClosesTheFrameAfterTheConfirm) {
    play({BattleMessage{.message = kMessageAnnihilated}});
    for (int frame = 0; frame < 300; ++frame) {
        tick();
    }
    EXPECT_TRUE(awaitingKey(shown_));
    EXPECT_TRUE(shown_.message);
    tick({}, true);
    EXPECT_FALSE(awaitingKey(shown_));
    EXPECT_TRUE(shown_.message);
    tick();
    EXPECT_FALSE(shown_.message);
    EXPECT_TRUE(shown_.idle);
}

// Monster dialog 1 types out a letter a frame, waits a second, breaks its line, and goes on.
TEST_F(BattlePresentationTest, MonsterDialogTypesOutALetterAFrame) {
    play({MonsterDialogShown{.dialog = 1}});
    ASSERT_TRUE(shown_.message);
    EXPECT_EQ(shown_.message->window, MessageWindow::Wide);
    EXPECT_EQ(glyphsShown().size(), 1u);
    tick();
    EXPECT_EQ(glyphsShown().size(), 2u);
    tick();
    EXPECT_EQ(glyphsShown().size(), 3u);
}

TEST_F(BattlePresentationTest, ALineBreakScrollsTheLineUpAPixelAFrameForSixteenFrames) {
    const std::vector<MessageStep> steps = monsterDialogSteps(MonsterDialogShown{.dialog = 1}, state(), content_->text);
    std::size_t letters = 0;
    int pause = 0;
    for (const MessageStep& step : steps) {
        if (std::holds_alternative<MessageLineBreak>(step)) break;
        if (std::holds_alternative<MessageGlyph>(step)) ++letters;
        if (const auto* p = std::get_if<MessagePause>(&step)) pause += p->frames;
    }
    ASSERT_GT(letters, 0u);
    EXPECT_EQ(pause, 60);  // "{wait}" before the break
    play({MonsterDialogShown{.dialog = 1}});
    // The letters, one a frame, then the pause: the frame it begins and 60 more.
    for (std::size_t i = 0; i < letters + 60; ++i) {
        tick();
    }
    ASSERT_EQ(glyphsShown().size(), letters);
    EXPECT_EQ(shown_.message->shown.glyphs.front().rise, 0);
    tick();  // the break: the first frame of the scroll
    EXPECT_EQ(shown_.message->shown.glyphs.front().rise, 1);
    for (int frame = 2; frame < kLineScrollFrames; ++frame) {
        tick();
    }
    EXPECT_EQ(shown_.message->shown.glyphs.front().rise, kLineScrollFrames - 1);
    tick();
    EXPECT_TRUE(shown_.message->shown.glyphs.empty());
    tick();
    EXPECT_EQ(shown_.message->shown.glyphs.size(), 1u);
    EXPECT_EQ(shown_.message->shown.glyphs.front().x, 0);
}

// --- what takes no frames ----------------------------------------------------------------------

TEST_F(BattlePresentationTest, CommandsThatShowNothingRunStraightIntoTheNextInOneFrame) {
    play({ReadyStance{.actor = BattlerSlot::character(1), .command = BattleCommandId::MAGIC},
          AttackAnimation{}, StepBack{}, AttackNameShown{.attack = AttackId::FIRE}});
    EXPECT_TRUE(shown_.message);
    EXPECT_EQ(shown_.waitingPoses[1], std::nullopt);
}

TEST_F(BattlePresentationTest, AReadyStanceSetsTheWaitingPoseAndAStepBackOrPoseResetClearsIt) {
    play({ReadyStance{.actor = BattlerSlot::character(0), .command = BattleCommandId::FIGHT},
          ReadyStance{.actor = BattlerSlot::character(2), .command = BattleCommandId::MAGIC}});
    EXPECT_EQ(shown_.waitingPoses[0], commandWaitingPose(BattleCommandId::FIGHT));
    EXPECT_EQ(shown_.waitingPoses[2], commandWaitingPose(BattleCommandId::MAGIC));
    EXPECT_TRUE(shown_.idle);
    play({PoseReset{.battler = BattlerSlot::character(2)}});
    EXPECT_TRUE(shown_.waitingPoses[0].has_value());
    EXPECT_FALSE(shown_.waitingPoses[2].has_value());
    play({StepBack{}});
    EXPECT_FALSE(shown_.waitingPoses[0].has_value());
}

// --- damage numerals ---------------------------------------------------------------------------

TEST_F(BattlePresentationTest, NumeralsShownTogetherHoldTheScriptUntilAllHaveBouncedSixtyFourFrames) {
    play({DamageNumbersShown{.numbers = {{.target = kLobo, .amount = 1234},
                                         {.target = BattlerSlot::character(0), .miss = true},
                                         {.target = BattlerSlot::character(1), .amount = 7, .heals = true}}}});
    const auto& lobo = shown_.numeralsTogether[static_cast<std::size_t>(kLobo.index())];
    ASSERT_TRUE(lobo);
    EXPECT_EQ(lobo->frame, 0);
    EXPECT_EQ(lobo->offset, 16);  // four digits
    EXPECT_EQ(shown_.numeralsTogether[0]->offset, 0);  // "Miss" shown together
    EXPECT_EQ(shown_.numeralsTogether[1]->offset, 4);  // one digit
    EXPECT_TRUE(shown_.numeralsTogether[1]->heals);
    EXPECT_FALSE(shown_.idle);
    for (int frame = 1; frame < 64; ++frame) {
        tick();
    }
    ASSERT_TRUE(shown_.numeralsTogether[static_cast<std::size_t>(kLobo.index())]);
    EXPECT_EQ(shown_.numeralsTogether[static_cast<std::size_t>(kLobo.index())]->frame, 63);
    EXPECT_FALSE(shown_.idle);
    tick();
    EXPECT_FALSE(shown_.numeralsTogether[static_cast<std::size_t>(kLobo.index())]);
    EXPECT_TRUE(shown_.idle);
}

TEST_F(BattlePresentationTest, ANumeralShownOnItsOwnAppearsOnTheSecondFrameAndLetsTheScriptGoOn) {
    play({DamageNumbersShown{.numbers = {{.target = kLobo, .amount = 50}}, .wait = DamageNumberWait::CarryOn},
          AttackNameShown{.attack = AttackId::FIRE}});
    EXPECT_FALSE(shown_.singleNumerals[0]);
    EXPECT_FALSE(shown_.message);
    tick();
    ASSERT_TRUE(shown_.singleNumerals[0]);
    EXPECT_EQ(shown_.singleNumerals[0]->offset, 8);  // two digits
    EXPECT_EQ(shown_.singleNumerals[0]->frame, 0);
    tick();
    EXPECT_TRUE(shown_.message);  // the script went on while the numeral bounces
    EXPECT_EQ(shown_.singleNumerals[0]->frame, 1);
}

TEST_F(BattlePresentationTest, ANumeralOverABattlerThatAlreadyHasOneWaitsEightMoreFrames) {
    play({DamageNumbersShown{.numbers = {{.target = kLobo, .amount = 5}, {.target = kLobo, .miss = true}},
                             .wait = DamageNumberWait::CarryOn}});
    tick();
    ASSERT_TRUE(shown_.singleNumerals[0]);
    for (int frame = 0; frame < 1 + kSameTargetWaitFrames; ++frame) {
        tick();
        EXPECT_FALSE(shown_.singleNumerals[1]) << frame;
    }
    tick();
    ASSERT_TRUE(shown_.singleNumerals[1]);
    EXPECT_TRUE(shown_.singleNumerals[1]->miss);
    EXPECT_EQ(shown_.singleNumerals[1]->offset, 8);  // "Miss" on its own
    EXPECT_TRUE(shown_.idle);
}

TEST_F(BattlePresentationTest, AFifthNumeralOnItsOwnTakesTheFirstOnesPlace) {
    std::vector<DamageNumber> numbers;
    const BattlerSlot targets[]{BattlerSlot::character(0), BattlerSlot::character(1), BattlerSlot::character(2), kLobo,
                                BattlerSlot::character(1)};
    for (const BattlerSlot target : targets) {
        numbers.push_back({.target = target, .amount = 1});
    }
    play({DamageNumbersShown{.numbers = numbers, .wait = DamageNumberWait::CarryOn}});
    framesUntilIdle();
    ASSERT_TRUE(shown_.singleNumerals[0]);
    EXPECT_EQ(shown_.singleNumerals[0]->target, BattlerSlot::character(1));
    EXPECT_EQ(shown_.nextThread, 5u);
}

TEST_F(BattlePresentationTest, NoNumeralShowsOverAMonsterThatIsNotShown) {
    shown_.monstersShown = {};
    play({DamageNumbersShown{.numbers = {{.target = kLobo, .amount = 9}}}});
    EXPECT_FALSE(shown_.numeralsTogether[static_cast<std::size_t>(kLobo.index())]);
    tick();
    EXPECT_TRUE(shown_.idle);
}

// --- a monster's death -------------------------------------------------------------------------

TEST_F(BattlePresentationTest, AFallenMonsterFadesOverThirtyThreeFramesThenLeavesTheField) {
    state().monstersStanding.remove(kLobo);
    tick();
    ASSERT_TRUE(shown_.deathFade);
    EXPECT_TRUE(shown_.deathFade->dying.has(kLobo));
    EXPECT_FALSE(shown_.deathFade->inDeathPalette());
    EXPECT_FALSE(shown_.idle);
    for (int frame = 1; frame <= 32; ++frame) {
        tick();
        ASSERT_TRUE(shown_.deathFade) << frame;
        EXPECT_TRUE(shown_.deathFade->inDeathPalette());
        EXPECT_EQ(shown_.deathFade->darkening(), frame - 1);
        EXPECT_TRUE(shown_.monstersShown.has(kLobo));
    }
    tick();
    EXPECT_FALSE(shown_.deathFade);
    EXPECT_FALSE(shown_.monstersShown.has(kLobo));
    EXPECT_TRUE(shown_.idle);
}

TEST_F(BattlePresentationTest, ADeathWaitsForTheScriptPlaying) {
    play({AttackNameShown{.attack = AttackId::FIRE}});
    state().monstersStanding.remove(kLobo);
    tick();
    EXPECT_FALSE(shown_.deathFade);
    framesUntilIdle();
    EXPECT_FALSE(shown_.deathFade);
    tick();  // the first frame with no script
    ASSERT_TRUE(shown_.deathFade);
    framesUntilIdle();
    EXPECT_FALSE(shown_.monstersShown.has(kLobo));
}

TEST_F(BattlePresentationTest, AMonsterBackOnTheFieldIsShownAgainWithoutAFade) {
    shown_.monstersShown = {};
    tick();
    EXPECT_TRUE(shown_.monstersShown.has(kLobo));
    EXPECT_FALSE(shown_.deathFade);
}

TEST_F(BattlePresentationTest, MonstersLeavingAndEnteringChangeWhoIsShown) {
    play({MonstersEnterOrLeave{.hidden = BattlerSet::of(kLobo)}});
    EXPECT_FALSE(shown_.monstersShown.has(kLobo));
    play({MonstersEnterOrLeave{.revived = BattlerSet::of(kLobo)}});
    EXPECT_TRUE(shown_.monstersShown.has(kLobo));
}

// --- the list of monster names (battle_main :15194-15238) ---------------------------------------

TEST_F(BattlePresentationTest, TheNameListGivesEachNameGroupOneLineInSlotOrder) {
    BattleState battle = state();
    for (int position = 1; position < 4; ++position) {
        Battler& monster = battle.at(BattlerSlot::monster(position));
        monster = battle.at(kLobo);
    }
    battle.at(BattlerSlot::monster(2)).nameGroup = MonsterId::GUARD;
    battle.at(BattlerSlot::monster(3)).nameGroup = MonsterId::GUARD;
    const MonsterNameList list = monsterNameList(battle);
    EXPECT_EQ(list[0], MonsterId::LOBO);
    EXPECT_EQ(list[1], MonsterId::GUARD);
    EXPECT_FALSE(list[2].has_value());

    battle.at(kLobo).nameHidden = true;
    battle.at(BattlerSlot::monster(1)).status.set(StatusId::PETRIFY);
    EXPECT_EQ(monsterNameList(battle)[0], MonsterId::GUARD);
    battle.monstersFallen.add(BattlerSlot::monster(2));
    battle.countedInBattle.remove(BattlerSlot::monster(3));
    EXPECT_FALSE(monsterNameList(battle)[0].has_value());
}

TEST_F(BattlePresentationTest, ANameListRefreshReadsTheBattleAsItStandsThen) {
    state().at(kLobo).nameHidden = true;
    tick({RefreshMonsterNames{}});
    EXPECT_FALSE(shown_.monsterNames[0].has_value());
}

// --- the party's poses (btlgfx _c13071 :6816-6857, DrawCharSprite :7465-7500) -------------------

TEST_F(BattlePresentationTest, APartyMembersStatusPoseOutranksTheWaitingPose) {
    Battler member = state().at(BattlerSlot::character(0));
    EXPECT_EQ(partyMemberPose(member, std::nullopt), 6);
    EXPECT_EQ(partyMemberPose(member, std::uint8_t{11}), 11);
    member.status.set(StatusId::NEAR_FATAL);
    EXPECT_EQ(partyMemberPose(member, std::uint8_t{11}), 0x0a);
    member.status.set(StatusId::POISON);
    EXPECT_EQ(partyMemberPose(member, std::nullopt), 0x0a);
    member.status.set(StatusId::CHANT);
    EXPECT_EQ(partyMemberPose(member, std::nullopt), 9);
    member.status.set(StatusId::CONFUSE);
    EXPECT_EQ(partyMemberPose(member, std::nullopt), 0x25);
    member.status.set(StatusId::SLEEP);
    EXPECT_EQ(partyMemberPose(member, std::nullopt), 0x0a);
    member.status.set(StatusId::DEAD);
    EXPECT_EQ(partyMemberPose(member, std::nullopt), 1);
}

// The counters start 0, 2, 4 and 6 and count every frame; a pose steps every eight counts.
TEST_F(BattlePresentationTest, ThePoseCountersStartStaggeredAndStepEveryEightFrames) {
    BattlePresentationState fresh;
    EXPECT_EQ(fresh.poseCounters, (std::array<std::uint8_t, kPartySlots>{0, 2, 4, 6}));
    const std::uint8_t before = shown_.poseCounters[0];
    tick();
    EXPECT_EQ(shown_.poseCounters[0], static_cast<std::uint8_t>(before + 1));
    EXPECT_EQ(poseStep(7), 0);
    EXPECT_EQ(poseStep(8), 1);
    EXPECT_EQ(poseStep(31), 3);
    EXPECT_EQ(poseStep(32), 0);
}

// --- what the battle is told -------------------------------------------------------------------

TEST_F(BattlePresentationTest, TheBattleIsToldTimeRunsAndWhetherThePresentationIsIdle) {
    EXPECT_TRUE(battleInput(shown_).presentationIdle);
    EXPECT_TRUE(battleInput(shown_).timeRunning);
    play({AttackNameShown{.attack = AttackId::FIRE}});
    EXPECT_FALSE(battleInput(shown_).presentationIdle);
    EXPECT_TRUE(battleInput(shown_).timeRunning);
}

}  // namespace
}  // namespace ostinato::battle
