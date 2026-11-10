// The attack frame on the oracle: ExecSelfAttack and ExecAttack are where the US cartridges hold them
// and an image without either is refused; the frame reads no cell a scenario leaves unwritten; the
// port's whole attack (battle::runAttack, battle::runSelfAttack) leaves every cell as the cartridge's
// own frame does, by hand and over drawn battles; and the routines the frame calls, traced while it
// runs, replay against the port with no difference.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "assets/cartridge.h"
#include "battle/attack.h"
#include "battle/battle_script.h"
#include "battle/battle_state.h"
#include "battle_test_support.h"
#include "damage_oracle.h"
#include "data/attack_properties.h"
#include "ostinato/attack_flags.h"
#include "ostinato/attack_id.h"
#include "ostinato/battle_command_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "ostinato/status_set.h"
#include "vanilla_rom.h"

namespace ostinato::test {
namespace {

using battle::BattlerSlot;
using trace::AttackResult;
using trace::DamageOracle;
using trace::DamageScenario;

class AttackOracleTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { image_ = new std::vector<std::uint8_t>{vanillaRom()}; }
    static void TearDownTestSuite() {
        delete image_;
        image_ = nullptr;
    }
    static std::span<const std::uint8_t> image() { return assets::stripCopierHeader(*image_); }

    // `body(oracle)` with the oracle started on the cartridge.
    template <typename Body>
    static void withOracle(Body body) {
        trace::runOnLoop([&] {
            DamageOracle oracle{image()};
            oracle.start();
            body(oracle);
        });
    }

    static std::vector<std::uint8_t>* image_;
};

std::vector<std::uint8_t>* AttackOracleTest::image_ = nullptr;

// A HiROM image holds bank $C2 from offset $020000.
constexpr std::size_t offsetOf(std::uint32_t at) {
    return at - 0xC00000;
}

constexpr std::uint32_t kExecSelfAttack = 0xC23167;
constexpr std::uint32_t kExecAttack = 0xC2317B;

constexpr BattlerSlot kHero = BattlerSlot::character(0);
constexpr BattlerSlot kFoe = BattlerSlot::monster(0);

// A party member and a monster on the field, 1,000 HP and 100 MP each, level 10, every status able
// to take hold, with the monster's attack: the plain hit at power 0, aimed at the party member.
DamageScenario frameBattle() {
    DamageScenario s;
    for (const BattlerSlot slot : {kHero, kFoe}) {
        battle::Battler& battler = s.state.at(slot);
        battler.present = true;
        battler.hp = 1000;
        battler.maxHp = 1000;
        battler.mp = 100;
        battler.maxMp = 100;
        battler.level = 10;
        battler.inflictableStatuses = StatusSet{{0xFF, 0xFF, 0xFF, 0xFF}};
        s.state.targetableOrPresent.add(slot);
        s.state.countedInBattle.add(slot);
        s.state.alive.add(slot);
    }
    s.attacker = kFoe;
    s.attack.command = BattleCommandId::FIGHT;
    s.attack.queuedCommand = BattleCommandId::FIGHT;
    s.attack.attack = AttackId::BATTLE;
    battle::loadAttackRecord(s.attack, AttackId::BATTLE);
    s.attack.targeting = s.attack.record.targeting;
    s.attack.targets = battle::BattlerSet::of(kHero);
    return s;
}

// A monster's spell at the party member: Fire, with its MP cost to pay.
DamageScenario fireAtTheHero() {
    DamageScenario s = frameBattle();
    s.attack.command = BattleCommandId::MAGIC;
    s.attack.queuedCommand = BattleCommandId::MAGIC;
    s.attack.attack = AttackId::FIRE;
    s.attack.queuedAttack = AttackId::FIRE;
    battle::loadAttackRecord(s.attack, AttackId::FIRE);
    s.attack.targeting = s.attack.record.targeting;
    s.attack.scratch.mpCost = s.attack.record.mpCost;
    s.attack.scratch.lastSpell = AttackId::FIRE;
    return s;
}

// The port's whole attack over `s`, with the cartridge's frame over the same battle leaving every
// cell the same.
AttackResult checked(DamageOracle& oracle, const DamageScenario& s, bool self = false) {
    const AttackResult port = trace::attackResult(self ? trace::portSelfAttack(s) : trace::portAttack(s));
    const AttackResult guest = self ? oracle.selfAttack(s) : oracle.attack(s);
    EXPECT_EQ(trace::differences(port, guest), "");
    return port;
}

const std::string& cell(const AttackResult& result, const std::string& name) {
    return result.cells.at(name);
}

std::string statusBytes(const StatusSet& set) {
    return std::to_string(set.bytes[0]) + "/" + std::to_string(set.bytes[1]) + "/" + std::to_string(set.bytes[2]) +
           "/" + std::to_string(set.bytes[3]);
}

// How many times `what` appears in `text`.
int countOf(const std::string& text, const std::string& what) {
    int count = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) {
        ++count;
    }
    return count;
}

TEST_F(AttackOracleTest, AnImageWithoutExecAttackIsRefused) {
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(kExecAttack)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

TEST_F(AttackOracleTest, AnImageWithoutExecSelfAttackIsRefused) {
    trace::runOnLoop([&] {
        const std::span<const std::uint8_t> original = image();
        std::vector<std::uint8_t> other(original.begin(), original.end());
        other[offsetOf(kExecSelfAttack)] ^= 0xFF;
        EXPECT_THROW(DamageOracle{other}, std::runtime_error);
    });
}

// Every cell the frame read came from the scenario or from the frame itself, but the cells left as
// the machine has them.
std::string readsUnwritten(DamageOracle& oracle) {
    std::string text;
    for (const trace::UnwrittenRead& read : oracle.unwrittenReads()) {
        if (!trace::leftAsTheMachineHasIt(read.at)) {
            char line[24];
            std::snprintf(line, sizeof line, "$%02X:%04X ", read.at >> 16 & 0xFF, read.at & 0xFFFF);
            text += line;
        }
    }
    return text;
}

TEST_F(AttackOracleTest, TheAttackFrameReadsNothingTheScenarioDoesNotWrite) {
    std::mt19937 rng{trace::kSweepSeed};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < 50; ++draw) {
            (void)oracle.attack(trace::drawAttackScenario(rng));
            EXPECT_EQ(readsUnwritten(oracle), "") << "draw " << draw;
        }
    });
}

TEST_F(AttackOracleTest, TheSelfAttackFrameReadsNothingTheScenarioDoesNotWrite) {
    std::mt19937 rng{trace::kSweepSeed};
    withOracle([&](DamageOracle& oracle) {
        for (int draw = 0; draw < 50; ++draw) {
            (void)oracle.selfAttack(trace::drawSelfAttackScenario(rng));
            EXPECT_EQ(readsUnwritten(oracle), "") << "draw " << draw;
        }
    });
}

// A self-attack that does nothing: no power, no statuses, shown as nothing. The battle reads back
// as the scenario wrote it, and battlers away from the field can be aimed at for the rest of the
// command.
TEST_F(AttackOracleTest, AQuietSelfAttackLeavesTheBattleAsWritten) {
    DamageScenario s = frameBattle();
    s.attack.command = battle::kNothingShown;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, true);
        EXPECT_EQ(cell(port, "HP of battler 4"), "1000");
        EXPECT_EQ(cell(port, "HP of battler 0"), "1000");
        EXPECT_EQ(cell(port, "statuses of battler 4"), "0/0/0/0");
        EXPECT_EQ(cell(port, "away from the field unreachable"), "0");
    });
}

// The attack a monster's script makes on itself to put a status on it: undodgeable, shown as nothing,
// the status taking effect before the attack is over; the monster marked as hit by itself.
TEST_F(AttackOracleTest, AStatusPutOnTheAttackerItself) {
    DamageScenario s = frameBattle();
    s.attack.command = battle::kNothingShown;
    s.attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    s.attack.record.statuses.set(StatusId::PROTECT);
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, true);
        EXPECT_EQ(cell(port, "statuses of battler 4"), statusBytes(StatusSet::of(StatusId::PROTECT)));
        EXPECT_EQ(cell(port, "HP of battler 4"), "1000");
        EXPECT_EQ(cell(port, "last attacker of battler 4"), std::to_string(4 | 0x80));
        EXPECT_EQ(countOf(cell(port, "script"), "animation 18 |"), 1);
    });
}

TEST_F(AttackOracleTest, AStatusTakenOffTheAttackerItself) {
    DamageScenario s = frameBattle();
    s.state.at(kFoe).status.set(StatusId::REFLECT);
    s.attack.command = battle::kNothingShown;
    s.attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    s.attack.record.flags2.set(AttackFlag2::REMOVE_STATUS);
    s.attack.record.statuses.set(StatusId::REFLECT);
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s, true);
        EXPECT_EQ(cell(port, "statuses of battler 4"), "0/0/0/0");
        EXPECT_EQ(cell(port, "HP of battler 4"), "1000");
    });
}

// A monster short of the MP its spell costs: shown as Mimic with no one aimed at, the "not enough
// MP" message, and nothing else.
TEST_F(AttackOracleTest, ASpellShortOfMpShowsTheMessageAndDoesNothing) {
    DamageScenario s = fireAtTheHero();
    s.state.at(kFoe).mp = 2;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s);
        EXPECT_EQ(cell(port, "script").substr(0, 13), "animation 18 ");
        EXPECT_EQ(countOf(cell(port, "script"), "message 40"), 1);
        EXPECT_EQ(cell(port, "HP of battler 0"), "1000");
        EXPECT_EQ(cell(port, "MP of battler 4"), "2");
    });
}

// A Muted caster's spell fizzles: the caster goes back to standing, and that is all.
TEST_F(AttackOracleTest, AMutedCastersSpellFizzles) {
    DamageScenario s = fireAtTheHero();
    s.state.at(kFoe).status.set(StatusId::SILENCE);
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s);
        EXPECT_EQ(cell(port, "script"), "pose reset 4; ");
        EXPECT_EQ(cell(port, "HP of battler 0"), "1000");
    });
}

// Air Anchor's mark: the attacker dies after its attack, marked as hit by itself, waiting to answer
// only after an attack on MP.
TEST_F(AttackOracleTest, AnAirAnchoredAttackerDiesAfterItsAttack) {
    DamageScenario s = frameBattle();
    s.state.at(kFoe).airAnchored = true;
    withOracle([&](DamageOracle& oracle) {
        AttackResult port = checked(oracle, s);
        EXPECT_EQ(cell(port, "statuses of battler 4"), statusBytes(StatusSet::of(StatusId::DEAD)));
        EXPECT_EQ(cell(port, "last attacker of battler 4"), "4");
        EXPECT_EQ(cell(port, "Air Anchor of battler 4"), "0");
        s.attack.record.flags1.set(AttackFlag1::AFFECT_MP);
        port = checked(oracle, s);
        EXPECT_EQ(cell(port, "last attacker of battler 4"), std::to_string(4 | 0x80));
    });
}

// A party member's blow that can be critical, aimed at the other side: a draw below 8 makes it
// critical, and the screen flashes; any other draw leaves it a plain hit.
TEST_F(AttackOracleTest, ACriticalHitOnADrawBelowEight) {
    DamageScenario s = frameBattle();
    s.attacker = kHero;
    s.attack.targets = battle::BattlerSet::of(kFoe);
    s.attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    s.attack.canBeCritical = true;
    s.attack.rules.opposingSideOnly = true;
    // The first positions whose draws lead the port to a critical hit and to a plain one.
    std::optional<int> critical;
    std::optional<int> plain;
    for (int position = 0; position < 256 && !(critical && plain); ++position) {
        DamageScenario at = s;
        at.state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
        const bool flashed =
            countOf(trace::attackResult(trace::portAttack(at)).cells.at("script"), "critical 1") == 1;
        if (flashed && !critical) {
            critical = position;
        }
        if (!flashed && !plain) {
            plain = position;
        }
    }
    ASSERT_TRUE(critical && plain);
    withOracle([&](DamageOracle& oracle) {
        s.state.rng = battle::BattleRng(static_cast<std::uint8_t>(*critical));
        AttackResult port = checked(oracle, s);
        EXPECT_EQ(countOf(cell(port, "script"), "critical 1"), 1);
        s.state.rng = battle::BattleRng(static_cast<std::uint8_t>(*plain));
        port = checked(oracle, s);
        EXPECT_EQ(countOf(cell(port, "script"), "critical 1"), 0);
    });
}

// A party member with Runic up draws the monster's Fire in: the Runic animation plays first, the
// spell restores the party member's MP by its cost, and Runic ends.
TEST_F(AttackOracleTest, RunicDrawsTheSpellIn) {
    DamageScenario s = fireAtTheHero();
    s.state.at(kHero).mp = 50;
    s.state.at(kHero).triggers.runic = true;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s);
        EXPECT_EQ(cell(port, "script").substr(0, 13), "animation 37 ");
        EXPECT_EQ(cell(port, "MP of battler 0"), std::to_string(50 + s.attack.record.mpCost));
        EXPECT_EQ(cell(port, "HP of battler 0"), "1000");
    });
}

// A weapon's spell, cast one time in four after the hit: a draw below $40 makes a second attack, the
// spell at the hit's targets.
TEST_F(AttackOracleTest, TheWeaponsSpellIsCastAfterTheHit) {
    DamageScenario s = frameBattle();
    s.attacker = kHero;
    s.attack.targets = battle::BattlerSet::of(kFoe);
    s.attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    s.attack.scratch.weaponSpellcast.raw = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(SpellCastMode::RANDOM_ON_ATTACK) | static_cast<std::uint8_t>(AttackId::FIRE));
    // The position whose draws lead the port to cast it.
    int position = 0;
    for (; position < 256; ++position) {
        DamageScenario at = s;
        at.state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
        if (countOf(trace::attackResult(trace::portAttack(at)).cells.at("script"), "animation") == 2) {
            break;
        }
    }
    ASSERT_LT(position, 256);
    s.state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s);
        EXPECT_EQ(countOf(cell(port, "script"), "animation 2 0 "), 1);
    });
}

// A party member's Fight as its body hands it to the frame: two swings, both hands with power, each
// loaded as it comes, the hit undodgeable through the wielder's relic.
DamageScenario heroFights(BattleCommandId command) {
    DamageScenario s = frameBattle();
    s.attacker = kHero;
    s.attack.targets = battle::BattlerSet::of(kFoe);
    s.attack.command = command;
    s.attack.queuedCommand = command;
    s.attack.scratch.fightCommand = command;
    s.attack.scratch.attacksLeft = 1;
    s.attack.canBeCritical = true;
    s.attack.rules.opposingSideOnly = true;
    battle::Battler& hero = s.state.at(kHero);
    hero.relicEffects3.set(RelicEffect3::HIT_100_IGNORE_MBLOCK);
    for (battle::Hand* hand : {&hero.weaponHand, &hero.shieldHand}) {
        hand->item = ItemId::DIRK;
        hand->battlePower = 50;
        hand->hitRate = 180;
    }
    return s;
}

// The weapon hand swings first and the shield hand second; the attack byte the animation reads
// carries the hand in its top bit, the plain hit's number turned down a bit at each swing.
TEST_F(AttackOracleTest, AFightSwingsEachHandWithTheHandInTheAttacksTopBit) {
    const DamageScenario s = heroFights(BattleCommandId::FIGHT);
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s);
        EXPECT_EQ(countOf(cell(port, "script"), "animation 0 "), 2);
        EXPECT_EQ(countOf(cell(port, "script"), "animation 0 119 "), 1);  // $EE turned: the weapon hand
        EXPECT_EQ(countOf(cell(port, "script"), "animation 0 187 "), 1);  // and again: the shield hand
    });
}

// An Offering swings eight times, the hands in turn, and the attack byte turns at each.
TEST_F(AttackOracleTest, AnOfferingSwingsEightTimes) {
    DamageScenario s = heroFights(BattleCommandId::FIGHT);
    s.state.at(kHero).relicEffects4.set(RelicEffect4::FIGHT_TO_X_FIGHT);
    s.attack.scratch.attacksLeft = 7;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s);
        EXPECT_EQ(countOf(cell(port, "script"), "animation 0 "), 8);
        EXPECT_EQ(countOf(cell(port, "script"), "animation 0 170 "), 1);  // the eighth, the shield hand
    });
}

// Capture's hits each carry Steal: every hit that lands shows Steal's message.
TEST_F(AttackOracleTest, CapturesHitsSteal) {
    DamageScenario s = heroFights(BattleCommandId::CAPTURE);
    s.state.at(kFoe).commonSteal = ItemId::POTION;
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s);
        EXPECT_EQ(countOf(cell(port, "script"), "animation 6 "), 2);
        EXPECT_GE(countOf(cell(port, "script"), "message "), 1);
    });
}

// A spell bounced off Reflect leaves the bounce's rule on the rest of the command, so its next attack
// is aimed back at the caster its last params block names, half the time. A bounce off the first
// party slot names no one there, and the next attack takes two entries of the battle's lore list
// for its targets: Revenge's, not known, as every party bit; Pearl Wind's, known, as every monster.
// Fire then keeps one of those bits at random, and a bit that names no battler can be the one.
TEST_F(AttackOracleTest, TheAttackAfterABounceTakesTheLoreListForItsTargets) {
    DamageScenario s = fireAtTheHero();
    s.state.at(kHero).status.set(StatusId::REFLECT);
    s.attack.rules.opposingSideOnly = true;
    s.attack.scratch.attacksLeft = 1;
    s.state.party.loresKnown.set(static_cast<std::size_t>(AttackId::PEARL_WIND) -
                                 static_cast<std::size_t>(AttackId::CONDEMNED));
    // Party bit 4, beyond the four party slots.
    const std::string noBattler = "targets " + std::to_string(0x10) + " ";
    // The position whose draws lead the port's second attack to that bit.
    int position = 0;
    for (; position < 256; ++position) {
        DamageScenario at = s;
        at.state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
        if (countOf(trace::attackResult(trace::portAttack(at)).cells.at("script"), noBattler) == 1) {
            break;
        }
    }
    ASSERT_LT(position, 256);
    s.state.rng = battle::BattleRng(static_cast<std::uint8_t>(position));
    withOracle([&](DamageOracle& oracle) {
        const AttackResult port = checked(oracle, s);
        EXPECT_EQ(countOf(cell(port, "script"), "bounced attack"), 1);
        EXPECT_EQ(countOf(cell(port, "script"), noBattler), 1);
    });
}

// The routines the frame calls, traced while it runs, are closed at the frame's own return sites
// and replay against the port with no difference.
TEST_F(AttackOracleTest, TheFramesInnerCallsReplayAsTheCartridgeWorkedThemOut) {
    std::mt19937 rng{trace::kSweepSeed};
    int calls = 0;
    withOracle([&](DamageOracle& oracle) {
        oracle.trace(true);
        for (int draw = 0; draw < 20; ++draw) {
            (void)oracle.attack(trace::drawAttackScenario(rng));
            std::stringstream log;
            for (const std::string& line : oracle.takeTrace()) {
                log << line << '\n';
            }
            for (const trace::TracedCall& call : trace::parseTrace(log)) {
                ++calls;
                EXPECT_EQ(oracle.replay(call), "") << "draw " << draw << ", " << call.routine;
            }
        }
        oracle.trace(false);
    });
    EXPECT_GE(calls, 100);
}

// What a sweep reached, read from the port's side.
struct Reached {
    int differing = 0;
    int named = 0;
    int shortOfMp = 0;
    int fizzled = 0;
    int runic = 0;
    int critical = 0;
    int bounced = 0;
    int twoAttacks = 0;
    int anchored = 0;
    int lore = 0;
    int fought = 0;
    int shieldHand = 0;
    int offering = 0;
    int captured = 0;
    int emptyHand = 0;
    std::string first;
};

void sweep(DamageOracle& oracle, bool self, Reached& reached) {
    std::mt19937 rng{trace::kSweepSeed};
    for (int draw = 0; draw < 500; ++draw) {
        const DamageScenario s = self ? trace::drawSelfAttackScenario(rng) : trace::drawAttackScenario(rng);
        const trace::AttackRun run = self ? trace::portSelfAttack(s) : trace::portAttack(s);
        const AttackResult port = trace::attackResult(run);
        const std::string difference = trace::differences(port, self ? oracle.selfAttack(s) : oracle.attack(s));
        if (!difference.empty() && ++reached.differing == 1) {
            reached.first = "draw " + std::to_string(draw) + " (" + trace::describe(s) + "):\n" + difference;
        }
        const std::string& script = port.cells.at("script");
        reached.named += countOf(script, "name ") > 0 ? 1 : 0;
        reached.shortOfMp += countOf(script, "message 40;") > 0 ? 1 : 0;
        reached.fizzled += countOf(script, "pose reset") > 0 ? 1 : 0;
        reached.runic += countOf(script, "animation 37 ") > 0 ? 1 : 0;
        reached.critical += countOf(script, "critical 1") > 0 ? 1 : 0;
        reached.bounced += countOf(script, "bounced attack") + countOf(script, "scattered attack") > 0 ? 1 : 0;
        reached.twoAttacks += countOf(script, "animation") >= 2 ? 1 : 0;
        reached.anchored += s.state.at(s.attacker).airAnchored ? 1 : 0;
        reached.lore += run.after.state.loresLearned != s.state.loresLearned ? 1 : 0;
        if (const std::optional<BattleCommandId> fight = s.attack.scratch.fightCommand) {
            const battle::Battler& attacker = s.state.at(s.attacker);
            ++reached.fought;
            reached.captured += *fight == BattleCommandId::CAPTURE ? 1 : 0;
            reached.offering += attacker.relicEffects4.has(RelicEffect4::FIGHT_TO_X_FIGHT) ? 1 : 0;
            reached.emptyHand += attacker.shieldHand.battlePower == 0 ? 1 : 0;
            for (const battle::BattleScriptCommand& command : run.script.commands()) {
                const auto* animation = std::get_if<battle::AttackAnimation>(&command);
                if (animation != nullptr && animation->offHand) {
                    ++reached.shieldHand;
                    break;
                }
            }
        }
    }
}

void expectEveryPathReached(const Reached& reached) {
    EXPECT_EQ(reached.differing, 0) << reached.differing << " of 500 differ; the first:\n" << reached.first;
    EXPECT_GT(reached.named, 0);
    EXPECT_GT(reached.shortOfMp, 0);
    EXPECT_GT(reached.fizzled, 0);
    EXPECT_GT(reached.runic, 0);
    EXPECT_GT(reached.critical, 0);
    EXPECT_GT(reached.bounced, 0);
    EXPECT_GT(reached.twoAttacks, 0);
    EXPECT_GT(reached.anchored, 0);
    EXPECT_GT(reached.lore, 0);
}

// The paths only a command that loads a hand takes: a Fight, each hand swung, an Offering's eight
// swings, Capture's hit and a hand with no power.
void expectEveryHandPathReached(const Reached& reached) {
    EXPECT_GT(reached.fought, 0);
    EXPECT_GT(reached.shieldHand, 0);
    EXPECT_GT(reached.offering, 0);
    EXPECT_GT(reached.captured, 0);
    EXPECT_GT(reached.emptyHand, 0);
}

TEST_F(AttackOracleTest, FiveHundredDrawnAttacksMatchTheCartridge) {
    Reached reached;
    withOracle([&](DamageOracle& oracle) { sweep(oracle, false, reached); });
    expectEveryPathReached(reached);
    expectEveryHandPathReached(reached);
}

TEST_F(AttackOracleTest, FiveHundredDrawnSelfAttacksMatchTheCartridge) {
    Reached reached;
    withOracle([&](DamageOracle& oracle) { sweep(oracle, true, reached); });
    expectEveryPathReached(reached);
}

}  // namespace
}  // namespace ostinato::test
