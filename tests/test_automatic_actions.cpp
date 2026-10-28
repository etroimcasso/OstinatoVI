// What a battler does when no one chooses for it: the party member's own pick, the dancer, the
// rager, Magitek armor, a monster that picks its own attack, and the three menu choices that come
// down to chance.
// Behaviors and their sources: docs/contracts/battle_commands.md § Automatic actions.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "battle/automatic_actions.h"
#include "battle/command_executor.h"
#include "battle/commands.h"
#include "battle_test_support.h"
#include "data/dance_properties.h"
#include "data/monster_attacks.h"
#include "ostinato/weapon_flags.h"

namespace ostinato::test {
namespace {

using battle::Battler;
using battle::BattlerSlot;
using battle::BattleState;
using battle::CommandAction;
using battle::MenuCommand;
using battle::SpellListEntry;

const BattlerSlot kTerra = BattlerSlot::character(0);
const BattlerSlot kMonster = BattlerSlot::monster(0);

// A draw that RandA(n) turns into `k`: (r * n) >> 8 == k.
std::function<bool(std::uint8_t)> drawsTo(int n, int k) {
    return [n, k](std::uint8_t r) { return ((r * n) >> 8) == k; };
}

BattleState ownCommandBattle(std::array<BattleCommandId, 4> commands) {
    BattleState state = makeAimableBattle();
    Battler& terra = state.at(kTerra);
    for (std::size_t row = 0; row < commands.size(); ++row) {
        terra.commands[row] = MenuCommand{.command = commands[row]};
    }
    return state;
}

// Give Terra spells at these positions of her list.
void learn(BattleState& state, std::initializer_list<std::pair<std::size_t, AttackId>> spells) {
    Battler& terra = state.at(kTerra);
    for (const auto& [position, spell] : spells) {
        terra.spells.magic[position] = SpellListEntry{.spell = spell};
    }
    terra.spellCount = static_cast<int>(spells.size());
}

std::vector<CommandAction> chosenActions(const BattleState& state, BattlerSlot slot) {
    std::vector<CommandAction> found;
    for (const battle::Action& action : state.at(slot).actions) {
        found.push_back(std::get<CommandAction>(action));
    }
    return found;
}

// --- a party member's own pick ------------------------------------------------------------------

TEST(OwnCommand, AnExtraFightLeadsTheCandidatesAheadOfTheRowsLastFirst) {
    // RandCharAction :656-745: $fe/$ff hold a Fight that is never cleared, counted fifth in $f5 and
    // first by the pick at :732-743.
    const std::array<BattleCommandId, 5> order{BattleCommandId::FIGHT, BattleCommandId::ROW, BattleCommandId::JUMP,
                                               BattleCommandId::CAPTURE, BattleCommandId::STEAL};
    for (int k = 0; k < 5; ++k) {
        BattleState state = ownCommandBattle(
            {BattleCommandId::STEAL, BattleCommandId::CAPTURE, BattleCommandId::JUMP, BattleCommandId::ROW});
        state.at(kTerra).status.set(StatusId::CONFUSE);
        rngWhere(state, {drawsTo(5, k)});
        EXPECT_EQ(battle::chooseOwnCommand(state, kTerra).command, order[static_cast<std::size_t>(k)]) << k;
    }
}

TEST(OwnCommand, EmptyRowsAndCommandsAConfusedFighterCannotUseDropOut) {
    // ConfusedCmdTbl :754-755 leaves out Item, Revert, Throw, Control, Slot, Leap, Def, Summon,
    // Possess.
    BattleState state = ownCommandBattle(
        {BattleCommandId::ITEM, BattleCommandId::STEAL, BattleCommandId::NONE, BattleCommandId::SUMMON});
    state.at(kTerra).status.set(StatusId::CONFUSE);
    rngWhere(state, {drawsTo(2, 1)});
    EXPECT_EQ(battle::chooseOwnCommand(state, kTerra).command, BattleCommandId::STEAL);
    rngWhere(state, {drawsTo(2, 0)});
    EXPECT_EQ(battle::chooseOwnCommand(state, kTerra).command, BattleCommandId::FIGHT);
}

TEST(OwnCommand, TheBerserkRulesApplyToTheBerserkAndToAZombieWhoIsNeitherConfusedCharmedNorInTheColosseum) {
    // :674-703: $f4 is 0 when Berserk is set; otherwise status 2 << 2 with the charm and colosseum
    // bits, negative (no berserk mask) when confused, charmed or in the colosseum.
    struct Case {
        const char* name;
        void (*make)(BattleState&);
        bool stealAllowed;
    };
    const Case cases[] = {
        {"berserk", [](BattleState& s) { s.at(kTerra).status.set(StatusId::BERSERK); }, false},
        {"berserk and confused",
         [](BattleState& s) {
             s.at(kTerra).status.set(StatusId::BERSERK);
             s.at(kTerra).status.set(StatusId::CONFUSE);
         },
         false},
        {"zombie", [](BattleState& s) { s.at(kTerra).status.set(StatusId::ZOMBIE); }, false},
        {"confused", [](BattleState& s) { s.at(kTerra).status.set(StatusId::CONFUSE); }, true},
        {"charmed", [](BattleState& s) { s.at(kTerra).charmedBy = kMonster; }, true},
        {"colosseum", [](BattleState& s) { s.colosseum = true; }, true},
    };
    for (const Case& c : cases) {
        BattleState state = ownCommandBattle(
            {BattleCommandId::STEAL, BattleCommandId::NONE, BattleCommandId::NONE, BattleCommandId::NONE});
        c.make(state);
        rngWhere(state, {drawsTo(2, 1)});
        const BattleCommandId picked = battle::chooseOwnCommand(state, kTerra).command;
        EXPECT_EQ(picked, c.stealAllowed ? BattleCommandId::STEAL : BattleCommandId::FIGHT) << c.name;
    }
}

TEST(OwnCommand, ACommandWithNothingToMakeDropsOut) {
    struct Case {
        const char* name;
        BattleCommandId command;
        void (*make)(BattleState&);
    };
    const Case cases[] = {
        {"mute Magic", BattleCommandId::MAGIC,
         [](BattleState& s) {
             learn(s, {{0, AttackId::FIRE}});
             s.at(kTerra).status.set(StatusId::SILENCE);
         }},
        {"Magic with no spells", BattleCommandId::MAGIC, [](BattleState&) {}},
        {"mute Lore", BattleCommandId::LORE,
         [](BattleState& s) {
             s.loresKnown = 3;
             s.at(kTerra).status.set(StatusId::SILENCE);
         }},
        {"Lore with none known", BattleCommandId::LORE, [](BattleState&) {}},
        {"Morph with 15 supply", BattleCommandId::MORPH, [](BattleState& s) { s.party.morphSupply = 15; }},
        {"Bushido with no Bushido weapon", BattleCommandId::BUSHIDO, [](BattleState&) {}},
    };
    for (const Case& c : cases) {
        BattleState state = ownCommandBattle({c.command, BattleCommandId::NONE, BattleCommandId::NONE,
                                              BattleCommandId::NONE});
        state.at(kTerra).status.set(StatusId::CONFUSE);
        c.make(state);
        rngWhere(state, {drawsTo(1, 0)});
        EXPECT_EQ(battle::chooseOwnCommand(state, kTerra).command, BattleCommandId::FIGHT) << c.name;
    }
}

TEST(OwnCommand, MorphNeedsSixteenSupplyAndBushidoAWeaponInEitherHand) {
    BattleState state = ownCommandBattle(
        {BattleCommandId::MORPH, BattleCommandId::BUSHIDO, BattleCommandId::NONE, BattleCommandId::NONE});
    state.at(kTerra).status.set(StatusId::CONFUSE);
    state.party.morphSupply = 16;
    state.at(kTerra).shieldHand.weaponFlags.set(WeaponFlags::SWDTECH);
    state.swdtechsKnown = 4;
    // Bushido (row 1) draws its technique first, then the pick among Fight, Bushido, Morph.
    rngWhere(state, {drawsTo(4, 2), drawsTo(3, 1)});
    const CommandAction bushido = battle::chooseOwnCommand(state, kTerra);
    EXPECT_EQ(bushido.command, BattleCommandId::BUSHIDO);
    EXPECT_EQ(bushido.attack, static_cast<AttackId>(static_cast<int>(AttackId::DISPATCH) + 2));
    rngWhere(state, {drawsTo(4, 0), drawsTo(3, 2)});
    EXPECT_EQ(battle::chooseOwnCommand(state, kTerra).command, BattleCommandId::MORPH);
}

TEST(OwnCommand, ARandomSpellIsCountedFromTheEndOfTheList) {
    // RandMagic :807-837: the walk starts at entry 54 and counts down over the filled ones.
    BattleState state = ownCommandBattle(
        {BattleCommandId::MAGIC, BattleCommandId::NONE, BattleCommandId::NONE, BattleCommandId::NONE});
    state.at(kTerra).status.set(StatusId::CONFUSE);
    learn(state, {{2, AttackId::FIRE}, {9, AttackId::ICE}, {40, AttackId::BOLT}});
    const std::array<AttackId, 3> fromTheEnd{AttackId::BOLT, AttackId::ICE, AttackId::FIRE};
    for (int k = 0; k < 3; ++k) {
        rngWhere(state, {drawsTo(3, k), drawsTo(2, 1)});
        const CommandAction magic = battle::chooseOwnCommand(state, kTerra);
        EXPECT_EQ(magic.command, BattleCommandId::MAGIC) << k;
        EXPECT_EQ(magic.attack, fromTheEnd[static_cast<std::size_t>(k)]) << k;
    }
}

TEST(OwnCommand, ARandomLoreIsCountedFromTheLastLoreBack) {
    BattleState state = ownCommandBattle(
        {BattleCommandId::LORE, BattleCommandId::NONE, BattleCommandId::NONE, BattleCommandId::NONE});
    state.at(kTerra).status.set(StatusId::CONFUSE);
    state.at(kTerra).spells.lores[0] = SpellListEntry{.spell = AttackId::CONDEMNED};
    state.at(kTerra).spells.lores[5] = SpellListEntry{.spell = AttackId::ROULETTE};
    state.loresKnown = 2;
    rngWhere(state, {drawsTo(2, 0), drawsTo(2, 1)});
    EXPECT_EQ(battle::chooseOwnCommand(state, kTerra).attack, AttackId::ROULETTE);
    rngWhere(state, {drawsTo(2, 1), drawsTo(2, 1)});
    EXPECT_EQ(battle::chooseOwnCommand(state, kTerra).attack, AttackId::CONDEMNED);
}

TEST(OwnCommand, BlitzToolsAndMagitekDrawFromWhatThePartyHas) {
    BattleState state = ownCommandBattle(
        {BattleCommandId::BLITZ, BattleCommandId::TOOLS, BattleCommandId::MAGITEK, BattleCommandId::NONE});
    state.at(kTerra).status.set(StatusId::CONFUSE);
    state.party.blitzesKnown = AbilityLearnedSet(0b0010'0110);   // blitzes 1, 2 and 5
    state.inventory.tools.add(ItemId::DRILL);
    state.inventory.tools.add(ItemId::AUTOCROSSBOW);
    // Row 2 Magitek, row 1 Tools, row 0 Blitz draw in that order, then the pick among four.
    rngWhere(state, {drawsTo(3, 2), drawsTo(2, 1), drawsTo(3, 2), drawsTo(4, 3)});
    const CommandAction blitz = battle::chooseOwnCommand(state, kTerra);
    EXPECT_EQ(blitz.command, BattleCommandId::BLITZ);
    EXPECT_EQ(blitz.attack, static_cast<AttackId>(static_cast<int>(AttackId::PUMMEL) + 5));
    rngWhere(state, {drawsTo(3, 2), drawsTo(2, 1), drawsTo(3, 0), drawsTo(4, 2)});
    const CommandAction tools = battle::chooseOwnCommand(state, kTerra);
    EXPECT_EQ(tools.command, BattleCommandId::TOOLS);
    EXPECT_EQ(tools.attack, static_cast<AttackId>(static_cast<int>(ItemId::AUTOCROSSBOW)));
    rngWhere(state, {drawsTo(3, 2), drawsTo(2, 0), drawsTo(3, 0), drawsTo(4, 1)});
    const CommandAction magitek = battle::chooseOwnCommand(state, kTerra);
    EXPECT_EQ(magitek.command, BattleCommandId::MAGITEK);
    EXPECT_EQ(magitek.attack, static_cast<AttackId>(static_cast<int>(AttackId::FIRE_BEAM) + 2));
}

TEST(OwnCommand, NoBlitzKnownIsPummelWithoutADraw) {
    // RandBit :13495-13515 draws only when a bit is set.
    BattleState state = ownCommandBattle(
        {BattleCommandId::BLITZ, BattleCommandId::NONE, BattleCommandId::NONE, BattleCommandId::NONE});
    state.at(kTerra).status.set(StatusId::CONFUSE);
    rngWhere(state, {drawsTo(2, 1)});
    const CommandAction blitz = battle::chooseOwnCommand(state, kTerra);
    EXPECT_EQ(blitz.command, BattleCommandId::BLITZ);
    EXPECT_EQ(blitz.attack, AttackId::PUMMEL);
}

// --- the automatic action -------------------------------------------------------------------------

TEST(AutomaticAction, DanceRageAndMagitekComeFirst) {
    // QueueAction :537-545.
    BattleState state = ownCommandBattle(
        {BattleCommandId::STEAL, BattleCommandId::NONE, BattleCommandId::NONE, BattleCommandId::NONE});
    Battler& terra = state.at(kTerra);
    terra.dance = DanceId{};
    terra.rage = MonsterId::GUARD;
    terra.status.set(StatusId::MAGITEK);
    terra.status.set(StatusId::RAGE);
    terra.status.set(StatusId::DANCE);
    RecordingExecutor executor;
    battle::chooseAutomaticAction(state, executor, kTerra);
    terra.status.clear(StatusId::DANCE);
    battle::chooseAutomaticAction(state, executor, kTerra);
    terra.status.clear(StatusId::RAGE);
    battle::chooseAutomaticAction(state, executor, kTerra);
    const auto chosen = chosenActions(state, kTerra);
    ASSERT_EQ(chosen.size(), 3u);
    EXPECT_EQ(chosen[0].command, BattleCommandId::DANCE);
    EXPECT_EQ(chosen[1].command, BattleCommandId::RAGE);
    EXPECT_EQ(chosen[2].command, BattleCommandId::MAGITEK);
    EXPECT_EQ(executor.aimed.size(), 3u);
}

TEST(AutomaticAction, XMagicCastsASecondSpellAsMagicFirst) {
    // :546-565: the X-Magic answer is kept on the stack while RandMagic draws again for a Magic
    // added first.
    BattleState state = ownCommandBattle(
        {BattleCommandId::X_MAGIC, BattleCommandId::NONE, BattleCommandId::NONE, BattleCommandId::NONE});
    state.at(kTerra).status.set(StatusId::CONFUSE);
    learn(state, {{0, AttackId::FIRE}, {1, AttackId::ICE}});
    rngWhere(state, {drawsTo(2, 0), drawsTo(2, 1), drawsTo(2, 1)});
    RecordingExecutor executor;
    battle::chooseAutomaticAction(state, executor, kTerra);
    const auto chosen = chosenActions(state, kTerra);
    ASSERT_EQ(chosen.size(), 2u);
    EXPECT_EQ(chosen[0].command, BattleCommandId::MAGIC);
    EXPECT_EQ(chosen[0].attack, AttackId::FIRE);
    EXPECT_EQ(chosen[1].command, BattleCommandId::X_MAGIC);
    EXPECT_EQ(chosen[1].attack, AttackId::ICE);
}

TEST(AutomaticAction, LoresRouletteBecomesTheRouletteCommandWithNoWaitAndNoAim) {
    // FixRoulette :574-582; CalcCmdDelay :621-622 skips commands from $1e.
    BattleState state = ownCommandBattle(
        {BattleCommandId::LORE, BattleCommandId::NONE, BattleCommandId::NONE, BattleCommandId::NONE});
    state.at(kTerra).status.set(StatusId::CONFUSE);
    state.at(kTerra).spells.lores[5] = SpellListEntry{.spell = AttackId::ROULETTE};
    state.loresKnown = 1;
    state.at(kTerra).advanceWait = 0;
    rngWhere(state, {drawsTo(1, 0), drawsTo(2, 1)});
    RecordingExecutor executor;
    battle::chooseAutomaticAction(state, executor, kTerra);
    const auto chosen = chosenActions(state, kTerra);
    ASSERT_EQ(chosen.size(), 1u);
    EXPECT_EQ(chosen[0].command, battle::kRouletteCommand);
    EXPECT_EQ(chosen[0].attack, AttackId::ROULETTE);
    EXPECT_TRUE(executor.aimed.empty());
    EXPECT_EQ(state.at(kTerra).advanceWait, 0);
}

// --- the menu choosers ----------------------------------------------------------------------------

TEST(DanceStep, TheStepsComeSevenThreeOneAndOneInSixteenths) {
    // RandDance :920-947, DanceRateTbl $10/$30/$90.
    struct Case {
        int roll;
        std::size_t step;
    };
    for (const Case c : {Case{0, 3}, Case{15, 3}, Case{16, 2}, Case{47, 2}, Case{48, 1}, Case{143, 1},
                         Case{144, 0}, Case{255, 0}}) {
        BattleState state = makeAimableBattle();
        state.at(kTerra).dance = static_cast<DanceId>(2);
        rngWhere(state, {[c](std::uint8_t r) { return r == c.roll; }});
        EXPECT_EQ(battle::chooseDanceStep(state, kTerra), getDanceProperties(static_cast<DanceId>(2)).attacks[c.step])
            << c.roll;
    }
}

TEST(DanceStep, ADancerWithNoDanceTakesOneThePartyKnows) {
    BattleState state = makeAimableBattle();
    state.dancesKnown[1] = static_cast<DanceId>(1);
    state.dancesKnown[6] = static_cast<DanceId>(6);
    rngWhere(state, {drawsTo(2, 1), any});
    battle::chooseDanceStep(state, kTerra);
    EXPECT_EQ(state.at(kTerra).dance, static_cast<DanceId>(6));
}

TEST(RageAttack, OneOfTheMonstersTwoRageAttacksOnACoin) {
    // RandRage :961-993.
    BattleState state = makeAimableBattle();
    state.at(kTerra).rage = MonsterId::SOLDIER;
    rngWhere(state, {tails});
    EXPECT_EQ(battle::chooseRageAttack(state, kTerra), getMonsterRage(MonsterId::SOLDIER).attacks[0]);
    rngWhere(state, {heads});
    EXPECT_EQ(battle::chooseRageAttack(state, kTerra), getMonsterRage(MonsterId::SOLDIER).attacks[1]);
}

TEST(RageAttack, ARagerWithNoMonsterTakesOneOfTheKnownRages) {
    BattleState state = makeAimableBattle();
    state.ragesKnown = {MonsterId::GUARD, MonsterId::SOLDIER, MonsterId::TEMPLAR};
    rngWhere(state, {drawsTo(3, 2), tails});
    EXPECT_EQ(battle::chooseRageAttack(state, kTerra), getMonsterRage(MonsterId::TEMPLAR).attacks[0]);
    EXPECT_EQ(state.at(kTerra).rage, MonsterId::TEMPLAR);
}

TEST(RandomEsper, OdinAndRaidenAreNeverCalled) {
    // RandGenju :8968-8977: RandA(25), +2 from the eleventh.
    BattleState state = makeAimableBattle();
    rngWhere(state, {drawsTo(25, 10)});
    EXPECT_EQ(battle::chooseRandomEsper(state), static_cast<AttackId>(static_cast<int>(EsperId::TRITOCH)));
    rngWhere(state, {drawsTo(25, 11)});
    EXPECT_EQ(battle::chooseRandomEsper(state), static_cast<AttackId>(static_cast<int>(EsperId::BAHAMUT)));
    rngWhere(state, {drawsTo(25, 24)});
    EXPECT_EQ(battle::chooseRandomEsper(state), static_cast<AttackId>(static_cast<int>(EsperId::PHOENIX)));
}

// --- a monster's own pick -------------------------------------------------------------------------

// A monster whose Control attacks are known to have empty slots.
std::optional<MonsterId> monsterWithEmptyControlSlot() {
    for (const MonsterControlEntry& entry : monsterControls()) {
        int filled = 0;
        for (AttackId attack : entry.record.attacks) {
            filled += attack != AttackId::NONE ? 1 : 0;
        }
        if (filled == 2 && entry.record.attacks[0] != AttackId::NONE && entry.record.attacks[3] == AttackId::NONE) {
            return entry.id;
        }
    }
    return std::nullopt;
}

TEST(MonsterAction, FromTheDrawnSlotTheFirstFilledOneCountingDownAndRoundAgain) {
    // RandMonsterAction :1021-1055.
    const std::optional<MonsterId> id = monsterWithEmptyControlSlot();
    ASSERT_TRUE(id.has_value());
    const MonsterControl& control = getMonsterControl(*id);
    for (int drawn = 0; drawn < 4; ++drawn) {
        BattleState state = makeAimableBattle();
        state.at(kMonster).monster = *id;
        state.at(kMonster).status.set(StatusId::CONFUSE);
        rngWhere(state, {[drawn](std::uint8_t r) { return (r & 3) == drawn; }});
        RecordingExecutor executor;
        battle::chooseMonsterAction(state, executor, kMonster);
        int index = drawn;
        while (control.attacks[static_cast<std::size_t>(index)] == AttackId::NONE) {
            index = (index + 3) % 4;
        }
        const auto chosen = chosenActions(state, kMonster);
        ASSERT_EQ(chosen.size(), 1u);
        EXPECT_EQ(chosen[0].attack, control.attacks[static_cast<std::size_t>(index)]) << drawn;
        EXPECT_EQ(chosen[0].command, battle::commandForAttack(chosen[0].attack)) << drawn;
    }
}

TEST(MonsterAction, ABerserkMonsterMakesThePlainHit) {
    // :1046-1051 tests status 2 bit 4, Berserk: whatever slot is drawn, the attack becomes Battle.
    std::optional<std::pair<MonsterId, int>> found;
    for (const MonsterControlEntry& entry : monsterControls()) {
        for (int index = 0; index < 4 && !found; ++index) {
            const AttackId attack = entry.record.attacks[static_cast<std::size_t>(index)];
            if (attack != AttackId::NONE && attack != AttackId::BATTLE) {
                found = std::pair{entry.id, index};
            }
        }
        if (found) break;
    }
    ASSERT_TRUE(found.has_value());
    const auto [id, index] = *found;
    BattleState state = makeAimableBattle();
    state.at(kMonster).monster = id;
    state.at(kMonster).status.set(StatusId::BERSERK);
    rngWhere(state, {[index](std::uint8_t r) { return (r & 3) == index; }});
    RecordingExecutor executor;
    battle::chooseMonsterAction(state, executor, kMonster);
    const auto chosen = chosenActions(state, kMonster);
    ASSERT_EQ(chosen.size(), 1u);
    EXPECT_EQ(chosen[0].attack, AttackId::BATTLE);
    EXPECT_EQ(chosen[0].command, battle::commandForAttack(AttackId::BATTLE));
}

TEST(MonsterAction, OnlyAColosseumCharmedBerserkOrConfusedMonsterPicksItsOwn) {
    // ExecAction :213-224.
    BattleState state = makeAimableBattle();
    state.at(kMonster).monster = MonsterId::GUARD;
    EXPECT_FALSE(battle::choosesItsOwnAttack(state, kMonster));
    battle::AttackHooks hooks;
    battle::CommandExecutor executor(hooks, blankMonsterNames(), noMonsterSprites());
    executor.runAi(state, kMonster);
    EXPECT_TRUE(state.at(kMonster).actions.empty());
    for (auto make : {+[](BattleState& s) { s.colosseum = true; },
                      +[](BattleState& s) { s.at(kMonster).charmedBy = kTerra; },
                      +[](BattleState& s) { s.at(kMonster).status.set(StatusId::BERSERK); },
                      +[](BattleState& s) { s.at(kMonster).status.set(StatusId::CONFUSE); }}) {
        BattleState picked = makeAimableBattle();
        picked.at(kMonster).monster = MonsterId::GUARD;
        make(picked);
        EXPECT_TRUE(battle::choosesItsOwnAttack(picked, kMonster));
        executor.runAi(picked, kMonster);
        EXPECT_EQ(picked.at(kMonster).actions.size(), 1u);
    }
}

}  // namespace
}  // namespace ostinato::test
