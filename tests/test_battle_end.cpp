// A battle's end (docs/contracts/battle_rewards.md § A-§ R), worked out by hand from the original:
// what each ending runs; the wrap-up every ending but two runs — the Morph supply kept, the menus
// closed last slot first, no menu opening again, 32 frames, every hand put away, the items obtained
// merged, and equipment the Item menu changed taking effect; and the win, whole, in its order — the
// celebration, the colosseum, the experience and gold, the Cat Hood and the Exp. Egg, the levels and
// what they teach, the lores, the dance, the drops and the gold — with the write-back's party byte.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "battle/battle_end.h"
#include "battle/battle_inventory.h"
#include "battle/battle_start.h"
#include "battle/items_obtained.h"
#include "data/battle_magic_points.h"
#include "data/formations.h"
#include "data/level_up.h"
#include "data/monster_items.h"
#include "data/rng_table.h"
#include "game/battle_switches.h"
#include "game/character_record.h"
#include "ostinato/attack_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/item_id.h"
#include "ostinato/status_id.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattleItem;
using battle::BattlerSlot;
using battle::BattleState;

BattleItem itemOf(ItemId id, std::uint8_t quantity) {
    static const BattleState noParty;
    return battle::battleItem(noParty, id, quantity);
}

BattleState wrappedUp(BattleState state) {
    battle::NullExecutor executor;
    battle::wrapUpBattle(state, executor);
    return state;
}

TEST(WrapUp, ItClosesEveryMenuLastSlotFirstAndWaits32Frames) {
    // _c24903 :12065-12080: slots 3 to 0, each meter shown emptied and its menu closed ($03), menus kept
    // from opening ($b1.7), then 32 waits for a frame.
    BattleState state;
    for (int position = 0; position < battle::kPartySlots; ++position) {
        state.at(BattlerSlot::character(position)).morphGauge = 200;
    }
    state = wrappedUp(state);
    ASSERT_EQ(state.requests.size(), 5u);
    for (int i = 0; i < 4; ++i) {
        const auto& closed = std::get<battle::CloseBattleMenu>(state.requests[static_cast<std::size_t>(i)]);
        EXPECT_EQ(closed.character, BattlerSlot::character(3 - i));
    }
    const auto& script = std::get<battle::RunBattleScript>(state.requests[4]);
    ASSERT_EQ(script.commands.size(), 1u);
    EXPECT_EQ(std::get<battle::Wait>(script.commands[0]).frames, 32);
    EXPECT_TRUE(state.menusDisabled);
    for (int position = 0; position < battle::kPartySlots; ++position) {
        EXPECT_EQ(state.at(BattlerSlot::character(position)).morphGauge, 0) << position;
    }
}

TEST(WrapUp, EveryHandIsPutAwayWhetherOrNotItWasMarked) {
    // `lda #$0f; tsb $3a8c` (:12081-12082) marks all four before _c262c7 runs: an item chosen for Item
    // or Throw and never used comes back. Last slot first.
    BattleState state;
    state.at(BattlerSlot::character(0)).itemInHand = ItemId::POTION;
    state.at(BattlerSlot::character(3)).itemInHand = ItemId::ETHER;
    state = wrappedUp(state);
    ASSERT_EQ(state.requests.size(), 7u);
    EXPECT_EQ(std::get<battle::AddItemObtained>(state.requests[5]).item, ItemId::ETHER);
    EXPECT_EQ(std::get<battle::AddItemObtained>(state.requests[6]).item, ItemId::POTION);
    EXPECT_FALSE(state.at(BattlerSlot::character(0)).itemInHand.has_value());
    EXPECT_FALSE(state.at(BattlerSlot::character(3)).itemInHand.has_value());
    EXPECT_TRUE(state.holdingItem.empty());
    // ... and the list is merged ($0A, :12084-12085): both are in the inventory.
    EXPECT_EQ(state.inventory.items[0].item, ItemId::ETHER);
    EXPECT_EQ(state.inventory.items[1].item, ItemId::POTION);
    EXPECT_EQ(state.itemsObtained.merged, 2);
}

TEST(WrapUp, ItemsStillWaitingInTheListReachTheInventory) {
    BattleState state;
    state.inventory.items[0] = itemOf(ItemId::POTION, 98);
    battle::obtainItem(state, ItemId::POTION, 3);
    state = wrappedUp(state);
    EXPECT_EQ(state.inventory.items[0].quantity, 99);
    EXPECT_EQ(state.itemsObtained.merged, 1);
}

TEST(WrapUp, TheMorphSupplyIsKeptAsTheMeterStands) {
    // SaveMorphCounter :1762-1771: the supply times the meter's high byte, over 256.
    BattleState state;
    state.party.morphSupply = 200;
    state.morphedBattler = BattlerSlot::character(1);
    state.morphCounter = 0x80FF;
    EXPECT_EQ(wrappedUp(state).party.morphSupply, 100);
    state.morphedBattler.reset();
    EXPECT_EQ(wrappedUp(state).party.morphSupply, 200);
}

TEST(WrapUp, EquipmentTheItemMenuChangedTakesEffect) {
    // UpdateCharProp :5595-5631: the record takes the hands' items.
    BattleState state = battleWithParty({CharacterPropId::TERRA});
    battle::Battler& terra = state.at(BattlerSlot::character(0));
    terra.record = 0;
    terra.actor = CharacterPropId::TERRA;
    terra.present = true;
    terra.equipmentChanged = true;
    state.inventory.weaponHands[0] = itemOf(ItemId::MITHRILBLADE, 1);
    state = wrappedUp(state);
    EXPECT_EQ(state.party.characters[0].weapon, ItemId::MITHRILBLADE);
    EXPECT_FALSE(state.at(BattlerSlot::character(0)).equipmentChanged);
}


// --- what each ending runs -----------------------------------------------------------------------

std::vector<std::string> shown(const BattleState& state) {
    std::vector<std::string> out;
    for (const battle::BattleRequest& request : state.requests) {
        if (const auto* close = std::get_if<battle::CloseBattleMenu>(&request)) {
            out.push_back("close " + std::to_string(close->character.position()));
        } else if (std::holds_alternative<battle::VictoryFanfare>(request)) {
            out.push_back("fanfare");
        } else if (const auto* obtained = std::get_if<battle::AddItemObtained>(&request)) {
            out.push_back("put away " + std::to_string(static_cast<int>(obtained->item)));
        } else if (const auto* script = std::get_if<battle::RunBattleScript>(&request)) {
            for (const battle::BattleScriptCommand& command : script->commands) {
                if (const auto* wait = std::get_if<battle::Wait>(&command)) {
                    out.push_back("wait " + std::to_string(wait->frames));
                } else if (std::holds_alternative<battle::StepBack>(command)) {
                    out.push_back("step back");
                } else if (const auto* event = std::get_if<battle::BattleEventPlays>(&command)) {
                    out.push_back("event " + std::to_string(event->event));
                } else if (const auto* message = std::get_if<battle::BattleMessage>(&command)) {
                    std::string text = "message " + std::to_string(message->message);
                    if (message->firstNumber) text += " first " + std::to_string(*message->firstNumber);
                    if (message->secondNumber) text += " second " + std::to_string(*message->secondNumber);
                    out.push_back(text);
                } else {
                    out.push_back("other script");
                }
            }
        } else {
            out.push_back("other");
        }
    }
    return out;
}

const std::vector<std::string> kWrapUpShown{"close 3", "close 2", "close 1", "close 0", "wait 32"};

std::vector<std::string> ended(battle::BattleOutcome outcome) {
    BattleState state;
    battle::NullExecutor executor;
    battle::playBattleEnd(state, executor, outcome);
    return shown(state);
}

TEST(BattleEnd, ALossRunsTheWrapUpThenItsMessage) {
    // LoseBattle :15588-15593 and ShowMsg :15602-15615: no message for $FF.
    std::vector<std::string> expected = kWrapUpShown;
    EXPECT_EQ(ended(battle::BattleLost{}), expected);
    expected.push_back("message 41");
    EXPECT_EQ(ended(battle::BattleLost{.message = battle::kMessageAnnihilated}), expected);
}

TEST(BattleEnd, TheSpecialEndingsRunTheWrapUpOrNot) {
    using battle::BattleEndReason;
    for (const BattleEndReason reason : {BattleEndReason::RanOutOfTime, BattleEndReason::EndNow,
                                         BattleEndReason::TimerExpired, BattleEndReason::EatenByZoneEater}) {
        EXPECT_EQ(ended(battle::BattleEnded{.reason = reason}), kWrapUpShown) << static_cast<int>(reason);
    }
    // BattleEnd_06 :12205-12206: the step back first.
    std::vector<std::string> stage{"step back"};
    stage.insert(stage.end(), kWrapUpShown.begin(), kWrapUpShown.end());
    EXPECT_EQ(ended(battle::BattleEnded{.reason = BattleEndReason::FinalBattleNextStage}), stage);
}

TEST(BattleEnd, ALeapAndTheRagesLearnedRunNoWrapUp) {
    // BattleEnd_02 :12023-12027 and BattleEnd_05 :12034-12036: the rages, and for a Leap Gau out of the
    // party; a hand stays full and the list unmerged.
    for (const battle::BattleEndReason reason : {battle::BattleEndReason::GauLeapt, battle::BattleEndReason::RagesLearned}) {
        BattleState state;
        state.at(BattlerSlot::character(1)).actor = CharacterPropId::GAU;
        state.at(BattlerSlot::character(0)).itemInHand = ItemId::POTION;
        state.at(BattlerSlot::monster(0)).monster = static_cast<MonsterId>(7);
        state.at(BattlerSlot::monster(1)).monster = static_cast<MonsterId>(300);
        battle::obtainItem(state, ItemId::ETHER, 1);
        battle::NullExecutor executor;
        battle::playBattleEnd(state, executor, battle::BattleEnded{.reason = reason});
        EXPECT_TRUE(state.requests.empty());
        EXPECT_EQ(state.at(BattlerSlot::character(0)).itemInHand, ItemId::POTION);
        EXPECT_EQ(state.itemsObtained.merged, 0);
        EXPECT_TRUE(state.party.ragesKnown.test(7));
        const bool leapt = reason == battle::BattleEndReason::GauLeapt;
        EXPECT_EQ(state.takenFromTheParty.has(BattlerSlot::character(1)), leapt);
        EXPECT_EQ(state.gauLeftParty, leapt);
    }
}

// --- the win ---------------------------------------------------------------------------------------

// A formation whose fanfare plays, or one whose fanfare is off.
FormationId formationWithFanfare(bool off) {
    for (int id = 0; id < 576; ++id) {
        if (getFormationAux(static_cast<FormationId>(id)).fanfareDisabled() == off) {
            return static_cast<FormationId>(id);
        }
    }
    ADD_FAILURE() << "no formation";
    return FormationId{};
}

// A won battle: the party's `actors` in slots 0 on, records 0 on, each alive and present; its monsters
// fallen in the slots given, giving `experience` and `gold` each; the RNG at `position`.
struct Win {
    BattleState state;

    explicit Win(std::initializer_list<CharacterPropId> actors) : state(battleWithParty(actors)) {
        int position = 0;
        for (CharacterPropId actor : actors) {
            const BattlerSlot slot = BattlerSlot::character(position);
            battle::Battler& member = state.at(slot);
            member.present = true;
            member.actor = actor;
            member.record = static_cast<std::size_t>(position);
            state.alive.add(slot);
            ++position;
        }
        state.allyCount = position;
        state.formation = formationWithFanfare(false);
        state.startingFormation = FormationId{};
    }

    Win& monster(int position, MonsterId id, std::uint16_t experience, std::uint16_t gold) {
        battle::Battler& m = state.at(BattlerSlot::monster(position));
        m.present = true;
        m.monster = id;
        m.experience = experience;
        m.gold = gold;
        m.status.set(StatusId::DEAD);
        return *this;
    }

    std::vector<std::string> play() {
        battle::NullExecutor executor;
        battle::playBattleEnd(state, executor, battle::BattleWon{});
        return shown(state);
    }
};

// The messages a win showed, after its wrap-up.
std::vector<std::string> messages(const std::vector<std::string>& all) {
    std::vector<std::string> out;
    for (const std::string& line : all) {
        if (line.rfind("message", 0) == 0) out.push_back(line);
    }
    return out;
}

// An RNG position whose next draw is below 32, or not.
std::uint8_t drawingRare(bool rare) {
    for (int position = 0; position < 256; ++position) {
        if ((rngByte(static_cast<std::uint8_t>(position + 1)) < battle::kRareDropBelow) == rare) {
            return static_cast<std::uint8_t>(position);
        }
    }
    return 0;
}

// A monster with no drops at all.
MonsterId monsterWithNoDrops() {
    for (int id = 0; id < 384; ++id) {
        const MonsterItems& items = getMonsterItems(static_cast<MonsterId>(id));
        if (items.rareDrop == ItemId::EMPTY && items.commonDrop == ItemId::EMPTY) {
            return static_cast<MonsterId>(id);
        }
    }
    ADD_FAILURE() << "no monster without drops";
    return MonsterId{};
}

// A monster whose rare and common drops are two different items.
MonsterId monsterWithTwoDrops() {
    for (int id = 0; id < 384; ++id) {
        const MonsterItems& items = getMonsterItems(static_cast<MonsterId>(id));
        if (items.rareDrop != ItemId::EMPTY && items.commonDrop != ItemId::EMPTY && items.rareDrop != items.commonDrop) {
            return static_cast<MonsterId>(id);
        }
    }
    ADD_FAILURE() << "no monster with two drops";
    return MonsterId{};
}

TEST(Win, ItStepsBackCelebratesAndWrapsUpBeforeAnythingElse) {
    // WinBattle :15285-15296; BtlGfx_08 G:243-274.
    Win win({CharacterPropId::LOCKE});
    std::vector<std::string> all = win.play();
    const std::vector<std::string> opening{"step back", "close 0", "close 1", "close 2", "close 3", "fanfare", "event 1",
                                           "close 3",   "close 2", "close 1", "close 0", "wait 32"};
    ASSERT_GE(all.size(), opening.size());
    EXPECT_EQ(std::vector<std::string>(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(opening.size())), opening);
}

TEST(Win, NoFanfareWhenTheFieldsMusicPlaysOnAndNoEventWhenItIsOff) {
    Win music({CharacterPropId::LOCKE});
    music.state.keepFieldMusic = true;
    const std::vector<std::string> withMusic = music.play();
    EXPECT_EQ(withMusic[5], "event 1");
    for (const bool off : {true, false}) {
        Win quiet({CharacterPropId::LOCKE});
        if (off) {
            quiet.state.formation = formationWithFanfare(true);
        } else {
            quiet.state.noVictoryFanfare = true;  // Shadow has just left
        }
        const std::vector<std::string> all = quiet.play();
        EXPECT_EQ(all[5], "close 3") << off;
    }
}

TEST(Win, TheColosseumGivesThePrizeAndNothingElse) {
    // :15297-15310.
    Win win({CharacterPropId::LOCKE});
    win.monster(0, monsterWithTwoDrops(), 500, 500);
    win.state.colosseum = true;
    win.state.colosseumPrize = ItemId::ELIXIR;
    const std::vector<std::string> all = win.play();
    EXPECT_EQ(messages(all), (std::vector<std::string>{"message 32 first " + std::to_string(static_cast<int>(ItemId::ELIXIR))}));
    EXPECT_EQ(win.state.party.characters[0].experience, game::startingRecord(CharacterPropId::LOCKE).experience);
    EXPECT_EQ(win.state.inventory.items[0].item, ItemId::ELIXIR);
}

TEST(Win, TheExperienceAndGoldAreSharedToldAndAdded) {
    // :15326-15363, :15531-15563: 301 experience between two allies is 150 each; the gold is told of
    // and added to the gil.
    Win win({CharacterPropId::LOCKE, CharacterPropId::EDGAR});
    win.monster(0, monsterWithNoDrops(), 200, 30).monster(3, monsterWithNoDrops(), 101, 12);
    win.state.party.gil = 1000;
    const std::vector<std::string> said = messages(win.play());
    ASSERT_FALSE(said.empty());
    EXPECT_EQ(said.front(), "message 39 first 150");
    EXPECT_EQ(said.back(), "message 38 second 42");
    EXPECT_EQ(win.state.party.gil, 1042u);
    EXPECT_EQ(win.state.party.characters[1].experience, game::startingRecord(CharacterPropId::EDGAR).experience + 150);
}

TEST(Win, TheFirstCatHoodDoublesTheGoldAndASecondDoesNothing) {
    Win win({CharacterPropId::LOCKE, CharacterPropId::EDGAR});
    win.monster(0, monsterWithNoDrops(), 0, 100);
    win.state.at(BattlerSlot::character(0)).relicEffects5.bits = static_cast<std::uint8_t>(RelicEffect5::DOUBLE_GP);
    win.state.at(BattlerSlot::character(1)).relicEffects5.bits = static_cast<std::uint8_t>(RelicEffect5::DOUBLE_GP);
    EXPECT_EQ(messages(win.play()).back(), "message 38 second 200");
}

TEST(Win, AnExpEggAddsTheShareTwice) {
    Win win({CharacterPropId::LOCKE});
    win.monster(0, monsterWithNoDrops(), 7, 0);
    win.state.at(BattlerSlot::character(0)).relicEffects5.bits = static_cast<std::uint8_t>(RelicEffect5::DOUBLE_EXPERIENCE);
    win.play();
    EXPECT_EQ(win.state.party.characters[0].experience, game::startingRecord(CharacterPropId::LOCKE).experience + 14);
}

TEST(Win, ThreeLevelsAreToldOfOnceAndGogoLearnsNothing) {
    // CheckLevelUp :15744-15747: one message per member; :15395-15397 and :15431-15433: Gogo learns
    // no spells.
    Win win({CharacterPropId::GOGO});
    game::CharacterRecord& gogo = win.state.party.characters[0];
    gogo.level = 10;
    std::uint32_t needed = 0;
    for (int level = 2; level <= 13; ++level) needed += levelUpExp(static_cast<std::uint8_t>(level));
    gogo.experience = needed * 8 - 5;
    win.monster(0, monsterWithNoDrops(), 5, 0);
    const std::vector<std::string> said = messages(win.play());
    EXPECT_EQ(gogo.level, 13);  // 10 to 13: three levels
    EXPECT_EQ(std::count(said.begin(), said.end(), "message 46 second 0"), 1);
}

TEST(Win, TheLoresAreToldOfLastFirst) {
    Win win({CharacterPropId::STRAGO});
    win.state.loresLearned.set(0);
    win.state.loresLearned.set(5);
    const std::vector<std::string> said = messages(win.play());
    const auto lore = [](int n) { return "message 45 first " + std::to_string(static_cast<int>(AttackId::CONDEMNED) + n); };
    EXPECT_EQ(said, (std::vector<std::string>{lore(5), lore(0)}));
    EXPECT_TRUE(win.state.party.loresKnown.test(5));
}

TEST(Win, MogLearnsTheDanceFallen) {
    Win win({CharacterPropId::LOCKE, CharacterPropId::MOG});
    win.state.alive.remove(BattlerSlot::character(1));
    win.state.allyCount = 1;
    const std::vector<std::string> said = messages(win.play());
    EXPECT_EQ(said, (std::vector<std::string>{"message 64"}));
}

TEST(Win, DropsComeByOneDrawPerFallenMonsterHighestSlotFirst) {
    // :15474-15530: below 32 the rare drop; places gather the same item; the highest slot's shows
    // first.
    const MonsterId two = monsterWithTwoDrops();
    const MonsterItems& items = getMonsterItems(two);
    for (const bool rare : {true, false}) {
        Win win({CharacterPropId::LOCKE});
        win.monster(1, two, 0, 0);
        win.state.rng = battle::BattleRng(drawingRare(rare));
        const std::uint8_t before = win.state.rng.position();
        const std::vector<std::string> said = messages(win.play());
        const ItemId dropped = rare ? items.rareDrop : items.commonDrop;
        EXPECT_EQ(said, (std::vector<std::string>{"message 32 first " + std::to_string(static_cast<int>(dropped))}));
        EXPECT_EQ(win.state.rng.position(), static_cast<std::uint8_t>(before + 1));
    }
    // Two monsters dropping the same item: one message, two of it.
    Win same({CharacterPropId::LOCKE});
    same.monster(0, two, 0, 0).monster(4, two, 0, 0);
    same.state.rng = battle::BattleRng(drawingRare(false));
    const std::uint8_t start = same.state.rng.position();
    if (rngByte(static_cast<std::uint8_t>(start + 2)) >= battle::kRareDropBelow) {
        const std::vector<std::string> said = messages(same.play());
        EXPECT_EQ(said, (std::vector<std::string>{"message 33 first " + std::to_string(static_cast<int>(items.commonDrop)) +
                                                  " second 2"}));
    }
}

TEST(Win, NoGoldEndsTheWinUntoldAndTheGilIsHeldToItsCap) {
    Win none({CharacterPropId::LOCKE});
    none.monster(0, monsterWithNoDrops(), 0, 0);
    for (const std::string& line : messages(none.play())) {
        EXPECT_NE(line.rfind("message 38", 0), 0u) << line;
    }
    Win rich({CharacterPropId::LOCKE});
    rich.monster(0, monsterWithNoDrops(), 0, 60'000);
    rich.state.party.gil = 9'990'000;
    rich.play();
    EXPECT_EQ(rich.state.party.gil, 9'999'999u);
}

TEST(Win, MagicPointsAreToldOfWhenTerraHoldsMagicite) {
    // :15377-15388, :15408-15417.
    Win win({CharacterPropId::TERRA});
    win.state.startingFormation = FormationId{};
    for (int battle = 0; battle < 512; ++battle) {
        if (magicPointsForBattle(static_cast<std::uint16_t>(battle)) != 0) {
            win.state.startingFormation = static_cast<FormationId>(battle);
            break;
        }
    }
    const std::uint8_t points = magicPointsForBattle(static_cast<std::uint16_t>(win.state.startingFormation));
    win.state.switches.set(game::kMagicPointsEarnedSwitch);
    const std::vector<std::string> said = messages(win.play());
    EXPECT_NE(std::find(said.begin(), said.end(), "message 53 first " + std::to_string(points)), said.end());
}

// --- the write-back ---------------------------------------------------------------------------------

TEST(WriteBack, ATakenPartyMembersWholePartyByteIsCleared) {
    // RemoveChar :11879-11885: `stz $1850,x`.
    BattleState state = battleWithParty({CharacterPropId::LOCKE, CharacterPropId::EDGAR});
    state.party.membership[1].backRow = true;
    battle::Battler& edgar = state.at(BattlerSlot::character(1));
    edgar.actor = CharacterPropId::EDGAR;
    edgar.record = 1;
    state.takenFromTheParty.add(BattlerSlot::character(1));
    state.outcome = battle::BattleWon{};
    const battle::BattleResult result = battle::writeBack(state);
    EXPECT_EQ(result.party.membership[1].party, 0);
    EXPECT_EQ(result.party.membership[1].slot, 0);
    EXPECT_FALSE(result.party.membership[1].backRow);
}

TEST(WriteBack, AWagerHeldInTwoSlotsIsTakenFromTheLastUntilItRunsOut) {
    // UpdateSRAM :12135-12150: last slot first; the slot that runs out is emptied with `lda #$ff`, and
    // the search goes on for empty slots, so slot 2's Elixirs are kept.
    BattleState state;
    state.colosseum = true;
    state.colosseumWager = ItemId::ELIXIR;
    state.inventory.items[2] = itemOf(ItemId::ELIXIR, 5);
    state.inventory.items[9] = itemOf(ItemId::ELIXIR, 1);
    state.inventory.items[4] = itemOf(ItemId::POTION, 3);
    state.outcome = battle::BattleWon{};
    const battle::BattleResult result = battle::writeBack(state);
    EXPECT_EQ(result.party.inventory[9].item, ItemId::EMPTY);
    EXPECT_EQ(result.party.inventory[2].quantity, 5);
    EXPECT_EQ(result.party.inventory[4].quantity, 3);
    // One slot, more than one: one is taken.
    state.inventory.items[9] = BattleItem{};
    EXPECT_EQ(battle::writeBack(state).party.inventory[2].quantity, 4);
}

}  // namespace
}  // namespace ostinato::test
