// A battle run from a frame tick: both clocks on one tick, the loop waiting for the presentation,
// the player's commands coming in, and whole scripted battles to a win, to a loss and through a
// stage of the final battle.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "battle/actions.h"
#include "battle/battle_end.h"
#include "battle/battle_sim.h"
#include "battle/morph.h"
#include "data/battle_magic_points.h"
#include "data/battle_tables.h"
#include "data/formations.h"
#include "game/battle_switches.h"
#include "ostinato/text_class.h"
#include "party_test_support.h"

namespace ostinato::test {
namespace {

using battle::BattleInput;
using battle::BattleRequest;
using battle::BattlerSet;
using battle::BattlerSlot;
using battle::BattleSim;
using battle::BattleState;
using battle::CommandAction;

const TextCorpus& noNames() {
    static const TextCorpus corpus = [] {
        std::array<std::vector<std::uint8_t>, kTextClassCount> buffers{};
        buffers[static_cast<std::size_t>(TextClass::MONSTER_NAME)].assign(3840, 0xFF);
        return TextCorpus{std::move(buffers)};
    }();
    return corpus;
}

// No cartridge sprites: every monster but the Ghost Train measures 0 tiles across, which places it
// by its formation position alone.
const MonsterSprites& noSprites() {
    static const MonsterSprites sprites;
    return sprites;
}

game::Party threeMembers() {
    return battleWithParty({CharacterPropId::TERRA, CharacterPropId::LOCKE, CharacterPropId::EDGAR}).party;
}

// A battle where each command hits: a party member's takes `partyDamage` from every monster it
// targets, a monster's AI turn is a Fight at the first party member standing that takes
// `monsterDamage`. A battler brought to 0 HP is Dead.
struct HitsExecutor : battle::ActionExecutor {
    int partyDamage = 0;
    int monsterDamage = 0;
    int stageEnds = 0;
    std::vector<battle::BattleOutcome> ends;

    static void hit(battle::Battler& battler, int damage) {
        battler.hp = static_cast<std::uint16_t>(std::max(0, battler.hp - damage));
        if (battler.hp == 0) {
            battler.status.set(StatusId::DEAD);
        }
    }

    std::vector<battle::BattleScriptCommand> execute(BattleState& state, std::optional<BattlerSlot> actor,
                                                     const battle::Action& action) override {
        const auto* command = std::get_if<CommandAction>(&action);
        if (command == nullptr || !actor) {
            return {};
        }
        for (int index = 0; index < battle::kBattlerSlots; ++index) {
            const BattlerSlot target = BattlerSlot::fromIndex(index);
            if (command->targets.has(target) && state.at(target).present) {
                hit(state.at(target), actor->isCharacter() ? partyDamage : monsterDamage);
            }
        }
        return {};
    }

    void runAi(BattleState& state, BattlerSlot actor) override {
        for (int position = 0; position < battle::kPartySlots; ++position) {
            const BattlerSlot target = BattlerSlot::character(position);
            if (state.alive.has(target)) {
                battle::addAction(state, actor,
                                  CommandAction{.command = BattleCommandId::FIGHT, .targets = BattlerSet::of(target)});
                return;
            }
        }
    }

    bool continueBattleEnd(BattleState&, const battle::BattleOutcome& outcome) override {
        ends.push_back(outcome);
        return true;
    }
};

std::string nameOf(const BattleRequest& request) {
    return std::visit(
        [](const auto& r) -> std::string {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, battle::OpenBattleMenu>) return "OpenBattleMenu";
            else if constexpr (std::is_same_v<T, battle::CloseBattleMenu>) return "CloseBattleMenu";
            else if constexpr (std::is_same_v<T, battle::RefreshBattleMenu>) return "RefreshBattleMenu";
            else if constexpr (std::is_same_v<T, battle::RunBattleScript>) return "RunBattleScript";
            else if constexpr (std::is_same_v<T, battle::RefreshMonsterNames>) return "RefreshMonsterNames";
            else if constexpr (std::is_same_v<T, battle::OpenBattleScene>) return "OpenBattleScene";
            else if constexpr (std::is_same_v<T, battle::ScrollFinalBattleBackground>) return "ScrollFinalBattleBackground";
            else return "CloseBattleScene";
        },
        request);
}

// Drive the battle as a presentation that plays every script at once and a player who answers
// every menu with a Fight at the first monster standing. Returns every request, in order.
std::vector<BattleRequest> playOut(BattleSim& battle, int frames) {
    std::vector<BattleRequest> log;
    std::vector<battle::PlayerAction> answers;
    for (int frame = 0; frame < frames && !battle.result(); ++frame) {
        battle.tick(BattleInput{.playerActions = std::exchange(answers, {})});
        for (BattleRequest& request : battle.takeRequests()) {
            if (const auto* menu = std::get_if<battle::OpenBattleMenu>(&request)) {
                for (int position = 0; position < battle::kMonsterSlots; ++position) {
                    const BattlerSlot monster = BattlerSlot::monster(position);
                    if (battle.state().alive.has(monster)) {
                        answers.push_back({.character = menu->character,
                                           .command = BattleCommandId::FIGHT,
                                           .targets = BattlerSet::of(monster)});
                        break;
                    }
                }
            }
            log.push_back(std::move(request));
        }
    }
    return log;
}

battle::BattleStart startFor(FormationId formation) {
    battle::BattleStart start;
    start.formation = FormationRef::of(formation);
    return start;
}

TEST(BattleSim, TimeRunsThroughAnAnimationWhileTheLoopWaits) {
    // ExecBtlGfx $04 blocks the loop while UpdateBattleTime keeps running from the graphics
    // program's frame wait (:392-439).
    battle::NullExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    battle.takeRequests();
    const std::uint16_t turnsBefore = battle.state().turnCounter;
    const std::uint8_t rngBefore = battle.state().rng.position();
    for (int frame = 0; frame < 20; ++frame) {
        battle.tick(BattleInput{.timeRunning = true, .presentationIdle = false});
    }
    EXPECT_EQ(battle.state().turnCounter, turnsBefore + 10);  // one time update every two frames
    // The loop took no step: its once-a-step advance of the random numbers never ran, and only the
    // time updates' own draws (none in ten updates of an opening battle) could move it.
    EXPECT_EQ(battle.state().rng.position(), rngBefore);
}

TEST(BattleSim, StoppedTimeHoldsTheGaugesButTheLoopStillSteps) {
    battle::NullExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    const std::uint16_t turnsBefore = battle.state().turnCounter;
    const std::uint8_t rngBefore = battle.state().rng.position();
    battle.tick(BattleInput{.timeRunning = false, .presentationIdle = true});
    battle.tick(BattleInput{.timeRunning = false, .presentationIdle = true});
    EXPECT_EQ(battle.state().turnCounter, turnsBefore);
    EXPECT_NE(battle.state().rng.position(), rngBefore);
}

TEST(BattleSim, AScriptedBattleToAWin) {
    HitsExecutor executor;
    executor.partyDamage = 9999;
    executor.monsterDamage = 1;
    game::Party party = threeMembers();
    BattleSim battle(startFor(FormationId::LOBO), party, {}, noNames(), noSprites(), executor);
    const auto log = playOut(battle, 6000);
    ASSERT_TRUE(battle.result().has_value());
    EXPECT_TRUE(std::holds_alternative<battle::BattleWon>(battle.result()->outcome));
    ASSERT_EQ(executor.ends.size(), 1u);

    // The request log: the scene opens, menus open as gauges fill, every turn plays its script,
    // and the scene closes last.
    std::vector<std::string> names;
    std::string sequence;
    for (const auto& request : log) {
        names.push_back(nameOf(request));
        sequence += names.back() + " ";
    }
    RecordProperty("requests", sequence);
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.front(), "OpenBattleScene");
    EXPECT_EQ(names.back(), "CloseBattleScene");
    const auto firstMenu = std::find(names.begin(), names.end(), "OpenBattleMenu");
    ASSERT_NE(firstMenu, names.end());
    EXPECT_NE(std::find(firstMenu, names.end(), "RunBattleScript"), names.end());

    // The party is handed back with the HP the monsters' hits left.
    const BattleState& state = battle.state();
    for (int position = 0; position < 3; ++position) {
        const battle::Battler& member = state.at(BattlerSlot::character(position));
        EXPECT_EQ(battle.result()->party.characters[*member.record].hp, member.hp);
    }
}

TEST(BattleSim, AScriptedBattleToALoss) {
    HitsExecutor executor;
    executor.partyDamage = 0;
    executor.monsterDamage = 9999;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    const auto log = playOut(battle, 20000);
    ASSERT_TRUE(battle.result().has_value());
    const auto* lost = std::get_if<battle::BattleLost>(&battle.result()->outcome);
    ASSERT_NE(lost, nullptr);
    EXPECT_EQ(lost->message, battle::kMessageAnnihilated);
    EXPECT_TRUE(battle.result()->switches.isSet(game::kPartyDefeatedSwitch));
    EXPECT_EQ(nameOf(log.back()), "CloseBattleScene");
    for (int position = 0; position < 3; ++position) {
        const auto& member = battle.state().at(BattlerSlot::character(position));
        EXPECT_TRUE(battle.result()->party.characters[*member.record].status.has(StatusId::DEAD));
    }
}

TEST(BattleSim, AWonFinalBattleStageScrollsOnToTheNext) {
    // CheckFinalBattle :12245-12262 into BattleEnd_06 :12204-12239: the battle does not end; the
    // background scrolls, and the next stage arrives with its own script.
    HitsExecutor executor;
    executor.partyDamage = 65535;
    executor.monsterDamage = 0;
    battle::BattleStart start = startFor(kFinalBattleFormations[0]);
    start.finalBattleOrder = {0, 1, 2};
    BattleSim battle(start, threeMembers(), {}, noNames(), noSprites(), executor);
    std::vector<std::string> names;
    std::vector<battle::PlayerAction> answers;
    for (int frame = 0; frame < 20000 && battle.state().formation == kFinalBattleFormations[0]; ++frame) {
        battle.tick(BattleInput{.playerActions = std::exchange(answers, {})});
        for (BattleRequest& request : battle.takeRequests()) {
            if (const auto* menu = std::get_if<battle::OpenBattleMenu>(&request)) {
                answers.push_back({.character = menu->character, .command = BattleCommandId::FIGHT,
                                   .targets = battle.state().alive.monstersOnly()});
            }
            names.push_back(nameOf(request));
        }
    }
    EXPECT_FALSE(battle.result().has_value());
    EXPECT_EQ(battle.state().formation, kFinalBattleFormations[1]);
    ASSERT_GE(names.size(), 2u);
    std::string tail;
    for (std::size_t i = names.size() > 6 ? names.size() - 6 : 0; i < names.size(); ++i) tail += names[i] + " ";
    EXPECT_EQ(names[names.size() - 2], "ScrollFinalBattleBackground") << tail;
    EXPECT_EQ(names.back(), "RunBattleScript");
    ASSERT_EQ(executor.ends.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<battle::BattleEnded>(executor.ends[0]));
}

// HitsExecutor's hits, with the Morph meter run down on every time update and the battle's end played by
// the battle's own rules (battle/morph.h, battle/battle_end.h).
struct EndingExecutor : HitsExecutor {
    void tickMorph(BattleState& state, BattlerSlot slot) override { battle::runDownMorph(state, slot); }
    bool continueBattleEnd(BattleState& state, const battle::BattleOutcome& outcome) override {
        return battle::continueBattleEnd(state, *this, outcome);
    }
};

// The frames a script holding one Wait and nothing else asks for; nothing for any other script.
std::optional<int> waitFrames(const battle::RunBattleScript& script) {
    if (script.commands.size() != 1) {
        return std::nullopt;
    }
    const auto* wait = std::get_if<battle::Wait>(&script.commands.front());
    return wait != nullptr ? std::optional<int>{wait->frames} : std::nullopt;
}

TEST(BattleSim, TheNextFinalBattleStageCountsOnFromThePlayClockTheEndLeft) {
    // RestartBattle runs InitBattle again (:63-64): the next stage's random numbers start from the play
    // clock's frames as the stage's end left them (:6088-6090), and the frame counter counts on.
    EndingExecutor executor;
    executor.partyDamage = 65535;
    battle::BattleStart start = startFor(kFinalBattleFormations[0]);
    start.finalBattleOrder = {0, 1, 2};
    BattleSim battle(start, threeMembers(), {}, noNames(), noSprites(), executor);
    std::vector<battle::PlayerAction> answers;
    int playing = 0;
    std::uint8_t clockBefore = 0;
    std::uint8_t framesBefore = 0;
    for (int frame = 0; frame < 20000 && battle.state().formation == kFinalBattleFormations[0]; ++frame) {
        clockBefore = battle.state().playClockFrames;
        framesBefore = battle.state().frameCount;
        const bool idle = playing <= 1;
        playing = std::max(0, playing - 1);
        battle.tick(BattleInput{.presentationIdle = idle, .playerActions = std::exchange(answers, {})});
        for (BattleRequest& request : battle.takeRequests()) {
            if (const auto* menu = std::get_if<battle::OpenBattleMenu>(&request)) {
                answers.push_back({.character = menu->character, .command = BattleCommandId::FIGHT,
                                   .targets = battle.state().alive.monstersOnly()});
            } else if (const auto* script = std::get_if<battle::RunBattleScript>(&request)) {
                playing += waitFrames(*script).value_or(2);
            }
        }
    }
    ASSERT_EQ(battle.state().formation, kFinalBattleFormations[1]);
    // The stage that finished stepped both on the tick it ended; the next counts on from there.
    const auto stepped = [](std::uint8_t clock) { return static_cast<std::uint8_t>(clock == 60 ? 1 : clock + 1); };
    EXPECT_EQ(battle.state().playClockFrames, stepped(clockBefore));
    EXPECT_EQ(battle.state().frameCount, static_cast<std::uint8_t>(framesBefore + 1));
    EXPECT_GT(battle.state().playClockFrames, 1) << "the end's frames are on the clock";
}

TEST(BattleSim, ThePlayersChoicesComeInWithTheTick) {
    battle::NullExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    battle.tick(BattleInput{.playerActions = {{.character = BattlerSlot::character(1),
                                               .command = BattleCommandId::FIGHT}}});
    EXPECT_TRUE(battle.state().chosenActions.empty());
    ASSERT_EQ(battle.state().at(BattlerSlot::character(1)).actions.size(), 1u);
    EXPECT_EQ(std::get<CommandAction>(battle.state().at(BattlerSlot::character(1)).actions[0]).command,
              BattleCommandId::FIGHT);
}

// Tick `battle` as a presentation that plays each script over frames of its own — a Wait for its frames,
// any other script for `scriptFrames` — battle time running on every tick when `timeRunning`, the last
// frame of the last script playing on an idle tick. Ticks until the party is handed back, at most `ticks`
// times, calling `watch(tick, framesAsked)` after each with the frames the tick's scripts asked for;
// returns every request.
template <typename Watch>
std::vector<BattleRequest> playScripts(BattleSim& battle, int scriptFrames, bool timeRunning, int ticks, Watch watch) {
    std::vector<BattleRequest> log;
    int playing = 0;
    for (int tick = 0; tick < ticks && !battle.result(); ++tick) {
        const bool idle = playing <= 1;
        playing = std::max(0, playing - 1);
        battle.tick(BattleInput{.timeRunning = timeRunning, .presentationIdle = idle});
        int asked = 0;
        for (BattleRequest& request : battle.takeRequests()) {
            if (const auto* script = std::get_if<battle::RunBattleScript>(&request)) {
                asked += waitFrames(*script).value_or(scriptFrames);
            }
            log.push_back(std::move(request));
        }
        playing += asked;
        watch(tick, asked);
    }
    return log;
}

// The battle's end begun by hand: the battle has just been won.
void won(BattleSim& battle) {
    battle.takeRequests();
    battle.state().outcome = battle::BattleWon{};
}

// Terra (party slot 0) in Morph with `counter` left on its meter, run down by kMorphSpeed on every time
// update, from a supply of kMorphSupply.
constexpr std::uint16_t kMorphSpeed = 0x0100;
constexpr std::uint8_t kMorphSupply = 200;
void morphTerra(BattleSim& battle, std::uint16_t counter) {
    BattleState& state = battle.state();
    const BattlerSlot terra = BattlerSlot::character(0);
    state.at(terra).status.set(StatusId::MORPH);
    state.morphedBattler = terra;
    state.morphCounter = counter;
    state.morphCounterSpeed = kMorphSpeed;
    state.party.morphSupply = kMorphSupply;
}

bool partPlayed(const BattleState& state, battle::BattleEndPart part) {
    return state.ending && static_cast<int>(state.ending->next) > static_cast<int>(part);
}

TEST(BattleSim, ABattlesEndTakesTheFramesItsScriptsTake) {
    // The wrap-up's 32 frame waits are 32 frames of the battle's clock (BtlGfx_01 btlgfx_main.asm:392-409,
    // _c24903 :12076-12080): the hands are put away 32 frames after the menus close, battle time running on
    // each.
    EndingExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    won(battle);
    std::optional<std::uint64_t> framesAtTheWait;
    std::optional<std::uint64_t> framesPutAway;
    std::optional<std::uint16_t> turnsAtTheWait;
    std::optional<std::uint16_t> turnsPutAway;
    playScripts(battle, 1, true, 400, [&](int, int) {
        const BattleState& state = battle.state();
        if (!framesAtTheWait && partPlayed(state, battle::BattleEndPart::CloseMenus)) {
            framesAtTheWait = state.frameCount;
            turnsAtTheWait = state.turnCounter;
        }
        if (!framesPutAway && partPlayed(state, battle::BattleEndPart::PutAway)) {
            framesPutAway = state.frameCount;
            turnsPutAway = state.turnCounter;
        }
    });
    ASSERT_TRUE(battle.result().has_value());
    ASSERT_TRUE(framesAtTheWait && framesPutAway);
    EXPECT_EQ(*framesPutAway - *framesAtTheWait, 32u);
    EXPECT_EQ(*turnsPutAway - *turnsAtTheWait, 16) << "a time update every two frames";
}

TEST(BattleSim, ThePartyIsHandedBackOnlyOnceTheEndsLastScriptHasPlayed) {
    EndingExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    won(battle);
    int lastAskedTick = -1;
    int lastAsked = 0;
    std::vector<bool> handedBack;
    playScripts(battle, 3, true, 600, [&](int tick, int asked) {
        handedBack.push_back(battle.result().has_value());
        if (asked > 0) {
            lastAskedTick = tick;
            lastAsked = asked;
        }
    });
    ASSERT_TRUE(battle.result().has_value());
    ASSERT_GE(lastAskedTick, 0);
    // The party is handed back on the tick the end's last script plays its last frame, and not before.
    for (int tick = 0; tick < static_cast<int>(handedBack.size()) - 1; ++tick) {
        EXPECT_FALSE(handedBack[static_cast<std::size_t>(tick)]) << "tick " << tick;
    }
    EXPECT_TRUE(handedBack.back());
    EXPECT_EQ(static_cast<int>(handedBack.size()) - 1, lastAskedTick + lastAsked);
}

TEST(BattleSim, AMorphRunningOutInTheWrapUpsWaitLeavesTheSupplyAtNothing) {
    // SaveMorphCounter keeps the supply as the meter stands at the wrap-up (:12064, :1762-1771); a meter
    // that then runs out in the wait saves it again by a meter of nothing (DecMorphCounter :2815-2829).
    EndingExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    won(battle);
    morphTerra(battle, 12 * kMorphSpeed);
    bool morphedAtTheWait = false;
    bool morphedWhenPutAway = true;
    std::uint8_t savedAtTheWait = 0;
    bool waited = false;
    bool putAway = false;
    playScripts(battle, 4, true, 400, [&](int, int) {
        const BattleState& state = battle.state();
        if (!waited && partPlayed(state, battle::BattleEndPart::CloseMenus)) {
            waited = true;
            morphedAtTheWait = state.morphedBattler.has_value();
            savedAtTheWait = state.party.morphSupply;
        }
        if (!putAway && partPlayed(state, battle::BattleEndPart::PutAway)) {
            putAway = true;
            morphedWhenPutAway = state.morphedBattler.has_value();
        }
    });
    ASSERT_TRUE(battle.result().has_value());
    ASSERT_TRUE(morphedAtTheWait) << "the meter ran out before the wrap-up";
    EXPECT_GT(savedAtTheWait, 0);
    EXPECT_FALSE(morphedWhenPutAway) << "the meter ran out in the wait";
    EXPECT_EQ(battle.result()->party.morphSupply, 0);
}

// The first battle that gives magic points.
FormationId aFormationWithMagicPoints() {
    for (std::uint16_t battle = 0; battle < kBattleMagicPoints.size(); ++battle) {
        if (magicPointsForBattle(battle) != 0) {
            return static_cast<FormationId>(battle);
        }
    }
    return FormationId::LOBO;
}

TEST(BattleSim, AMorphRunningOutAfterTheMagicPointsAreAddedLeavesTheSupplyAtNothing) {
    // A won battle adds Terra's share of the magic points to the supply (:15377-15388); a meter running out
    // in the frames of the messages after it saves the supply by a meter of nothing.
    EndingExecutor executor;
    game::BattleSwitches switches;
    switches.set(game::kMagicPointsEarnedSwitch);
    const FormationId formation = aFormationWithMagicPoints();
    BattleSim battle(startFor(formation), threeMembers(), switches, noNames(), noSprites(), executor);
    ASSERT_NE(magicPointsForBattle(static_cast<std::uint16_t>(formation)), 0);
    won(battle);
    // Through the step back, the celebration and the wait (40, 40 and 32 frames: 56 time updates), and out
    // in the 40 frames of the magic points' message.
    constexpr int kScriptFrames = 40;
    morphTerra(battle, 64 * kMorphSpeed);
    std::optional<std::uint8_t> afterTheMagicPoints;
    bool morphedAfterTheMagicPoints = false;
    playScripts(battle, kScriptFrames, true, 1000, [&](int, int) {
        const BattleState& state = battle.state();
        if (!afterTheMagicPoints && partPlayed(state, battle::BattleEndPart::Rewards)) {
            afterTheMagicPoints = state.party.morphSupply;
            morphedAfterTheMagicPoints = state.morphedBattler.has_value();
        }
    });
    ASSERT_TRUE(battle.result().has_value());
    ASSERT_TRUE(afterTheMagicPoints.has_value());
    ASSERT_TRUE(morphedAfterTheMagicPoints) << "the meter ran out before the magic points were added";
    EXPECT_GT(*afterTheMagicPoints, 0);
    EXPECT_EQ(battle.result()->party.morphSupply, 0);
}

TEST(BattleSim, AMorphThatDoesNotRunOutKeepsTheSupplyTheWrapUpSaved) {
    EndingExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    won(battle);
    morphTerra(battle, 0xFF00);
    std::optional<std::uint8_t> saved;
    std::optional<std::uint16_t> meterAtTheWait;
    playScripts(battle, 4, true, 400, [&](int, int) {
        const BattleState& state = battle.state();
        if (!saved && partPlayed(state, battle::BattleEndPart::CloseMenus)) {
            saved = state.party.morphSupply;
            meterAtTheWait = state.morphCounter;
        }
    });
    ASSERT_TRUE(battle.result().has_value());
    ASSERT_TRUE(saved && meterAtTheWait);
    EXPECT_EQ(*saved, kMorphSupply * (*meterAtTheWait >> 8) / 256);
    EXPECT_EQ(battle.result()->party.morphSupply, *saved);
}

std::string describe(const BattleRequest& request) {
    std::string text = nameOf(request);
    if (const auto* script = std::get_if<battle::RunBattleScript>(&request)) {
        text += " of " + std::to_string(script->commands.size());
        if (const std::optional<int> wait = waitFrames(*script)) {
            text += ", a wait of " + std::to_string(*wait);
        }
    } else if (const auto* close = std::get_if<battle::CloseBattleMenu>(&request)) {
        text += " " + std::to_string(close->character.position());
    }
    return text;
}

TEST(BattleSim, TheEndPlayedPartByPartAsksForWhatTheWholeEndAsksFor) {
    // With battle time stopped nothing changes between the parts: the end played a part a tick asks for the
    // same requests, in the same order, as playBattleEnd() playing it whole.
    EndingExecutor executor;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    won(battle);
    BattleState whole = battle.state();
    battle::playBattleEnd(whole, executor, battle::BattleWon{});
    const std::vector<BattleRequest> parts = playScripts(battle, 2, false, 400, [](int, int) {});
    ASSERT_TRUE(battle.result().has_value());
    ASSERT_FALSE(parts.empty());
    EXPECT_EQ(nameOf(parts.back()), "CloseBattleScene");
    ASSERT_EQ(whole.requests.size(), parts.size() - 1);
    for (std::size_t i = 0; i < whole.requests.size(); ++i) {
        EXPECT_EQ(describe(parts[i]), describe(whole.requests[i])) << "request " << i;
    }
    EXPECT_EQ(battle.result()->party.characters[0].experience, whole.party.characters[0].experience);
}

TEST(BattleSim, NothingRunsOnceTheBattleIsOver) {
    HitsExecutor executor;
    executor.partyDamage = 9999;
    BattleSim battle(startFor(FormationId::LOBO), threeMembers(), {}, noNames(), noSprites(), executor);
    playOut(battle, 6000);
    ASSERT_TRUE(battle.result().has_value());
    const std::uint16_t turns = battle.state().turnCounter;
    battle.tick(BattleInput{});
    EXPECT_EQ(battle.state().turnCounter, turns);
    EXPECT_TRUE(battle.takeRequests().empty());
}

}  // namespace
}  // namespace ostinato::test
